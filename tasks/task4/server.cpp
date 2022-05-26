#include <iostream>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <chrono>

#include "common/assert.hpp"
#include "common/Service.hpp"
#include "common/proto.hpp"

#include "game/Entity.hpp"
#include "game/gameProto.hpp"


using namespace std::chrono_literals;


class ServerService
  : public Service<ServerService, true>
{
  using Clock = std::chrono::steady_clock;
 public:
  ServerService(ENetAddress addr)
    : Service(&addr, 32, 2)
  {
    resetGame();
  }

  void resetGame()
  {
    constexpr size_t kBots = 10;

    entities_.clear();
    botTargets_.clear();

    entities_.reserve(kBots);
    botTargets_.reserve(kBots);
    for (size_t i = 0; i < kBots; ++i)
    {
      auto id = entities_.emplace_back(Entity::create()).id;
      botTargets_.emplace(id, Entity::randomPos());
    }
  }

  void registerInLobby(char* addr, uint16_t port)
  {
    // kostyl
    static ENetAddress address;
    if (addr != nullptr)
    {
      enet_address_set_host(&address, addr);
      address.port = port;
    }
    
    connect(address,
      [this](ENetPeer* lobby)
      {
        NG_VERIFY(lobby != nullptr);
        send(lobby, 0, ENET_PACKET_FLAG_RELIABLE,
          PRegisterServerInLobby{});
        disconnect(lobby, [](){});
      });
  }

  void handlePacket(ENetPeer* peer, PChat packet)
  {
    packet.player = clients_.at(peer).id;
    for (auto&[client, data] : clients_)
    {
      if (client == peer) continue;

      send(client, 1, {}, packet);
    }
  }

  void connected(ENetPeer* peer)
  {
    spdlog::info("{}:{} joined", peer->address.host, peer->address.port);

    auto id = idCounter_++;

    auto& created = entities_.emplace_back(Entity::create());

    clients_.emplace(peer, ClientData{
        .id = id,
        .entityId = created.id,
      });

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PPossesEntity{
        .id = created.id,
      });

    for (auto&[client, data] : clients_)
    {
      if (client == peer) continue;

      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id =  id });

      send(client, 0, ENET_PACKET_FLAG_RELIABLE, PSpawnEntity{ .entity = created });
      
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id = data.id });
    }
    
    for (auto& entity : entities_)
    {
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE, PSpawnEntity{ .entity = entity });
    }
  }

  Entity* entityById(id_t id)
  {
    auto it = std::find_if(entities_.begin(), entities_.end(),
      [id](const Entity& e) { return e.id == id; });
    if (it == entities_.end())
      return nullptr;
    
    return &*it;
  }

  void handlePacket(ENetPeer* peer, const PPlayerInput& packet)
  {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return;

    auto* entity = entityById(it->second.entityId);

    if (entity == nullptr) return;

    float len = glm::length(packet.desiredSpeed);
    
    if (len < 1e-3)
    {
      entity->vel = {0, 0};
      return;
    }
    
    entity->vel = packet.desiredSpeed / len * std::clamp(len, 0.f, 1.f);
  }

  void disconnected(ENetPeer* peer)
  {
    spdlog::info("{}:{} left", peer->address.host, peer->address.port);
    
    auto it = clients_.find(peer);
    if (it == clients_.end())
    {
      return;
    }

    auto erasedData = std::move(it->second);
    clients_.erase(it);

    for (auto&[client, data] : clients_)
    {
      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerLeft{ .id = erasedData.id });
    }

    if (clients_.empty())
    {
      spdlog::info("All players left, requeueing in lobby");
      resetGame();
      registerInLobby(nullptr, 0);
    }
  }

  void broadcastEntityChange(const Entity& entity, bool teleport)
  {
    for (auto&[client, _] : clients_)
    {
      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PEntityPropsChanged{
          .id = entity.id,
          .size = entity.size,
          .color = entity.color,
        });
      if (teleport)
      {
        send(client, 0, ENET_PACKET_FLAG_RELIABLE,
          PEntityTeleport{
            .id = entity.id,
            .pos = entity.pos,
          });
      }
    }
  }

  void updateLogic(float delta)
  {
    for (auto& entity : entities_)
    {
      if (botTargets_.contains(entity.id))
      {
        auto v = botTargets_[entity.id] - entity.pos;
        auto len = glm::length(v);

        if (len < 1e-3)
        {
          botTargets_[entity.id] = Entity::randomPos();
          continue;
        }

        entity.vel = v / len * 0.2f;
      }

      entity.simulate(delta);
    }

    for (auto& e1 : entities_)
    {
      for (auto& e2 : entities_)
      {
        if (&e1 == &e2) continue;

        if (glm::length(e1.pos - e2.pos) + e1.size < e2.size)
        {
          e2.size += e1.size/2;
          e1.size /= 2;

          e1.pos = Entity::randomPos();

          broadcastEntityChange(e1, true);
          broadcastEntityChange(e2, false);
        }
      }
    }

    for (size_t i = 0; i < entities_.size();)
    {
      if (entities_[i].size < 1e-3)
      {
        auto id = entities_[i].id;
        std::swap(entities_[i], entities_.back());
        entities_.pop_back();

        for (auto&[peer, _] : clients_)
        {
          send(peer, 0, ENET_PACKET_FLAG_RELIABLE, PDestroyEntity{ .id = id });
        }
      }
      else
      {
        ++i;
      }
    }
  }

  void run()
  {
    constexpr auto kSendRate = 100ms;

    auto startTime = Clock::now();
    auto currentTime = startTime;
    auto lastSendTime = startTime;

    while (true)
    {
      auto now = Clock::now();
      float delta =
        std::chrono::duration_cast<std::chrono::duration<float>>(
          now - std::exchange(currentTime, now)).count();

      if (clients_.size() > 0)
      {
        updateLogic(delta);
      }

      if ((now - lastSendTime) > kSendRate)
      {
        lastSendTime = now;


        for (auto&[to, _] : clients_)
        {
          for (auto& entity : entities_)
          {
            send(to, 1, {}, PEntitySnapshot{
              .id = entity.id,
              .pos = entity.pos,
            });
          }
        }
      }

      Service::poll();
    }
  }

 private:
  struct ClientData
  {
    uint32_t id;
    id_t entityId;
  };

  std::unordered_map<ENetPeer*, ClientData> clients_;
  uint32_t idCounter_{1};
  std::vector<Entity> entities_;

  std::unordered_map<id_t, glm::vec2> botTargets_;
};

int main(int argc, char** argv)
{
  if (argc != 4)
  {
    spdlog::error("Usage: {} <server port> <lobby address> <lobby port>\n", argv[0]);
    return -1;
  }

  NG_VERIFY(enet_initialize() == 0);
  std::atexit(enet_deinitialize);

  ENetAddress address{
    .host = ENET_HOST_ANY,
    .port = static_cast<uint16_t>(std::atoi(argv[1])),
  };

  ServerService server(address);

  server.registerInLobby(argv[2], static_cast<uint16_t>(std::atoi(argv[3])));

  server.run();

  return 0;
}
