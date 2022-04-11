#include <iostream>
#include <enet/enet.h>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <chrono>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/proto.hpp"

#include "game/Entity.hpp"


using namespace std::chrono_literals;


class ServerService
  : public Service<ServerService, true>
{
  using Clock = std::chrono::steady_clock;
 public:
  ServerService(ENetAddress addr)
    : Service(&addr, 32, 2)
  {
    resetBots();
  }

  void resetBots()
  {
    constexpr size_t kBots = 10;

    bots_.clear();
    botTargets_.clear();

    bots_.reserve(kBots);
    botTargets_.reserve(kBots);
    for (size_t i = 0; i < kBots; ++i)
    {
      bots_.emplace_back(Entity::create());
      botTargets_.emplace_back(Entity::randomPos());
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
    const auto& created = clients_.emplace(peer, ClientData{
        .id = id,
        .entity = Entity::create(),
      }).first->second.entity;

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PPossesEntity{
        .id = created.id,
      });


    auto entityToSpawnPacket =
      [](const Entity& entity)
      {
        return PSpawnEntity{
            .x = entity.x,
            .y = entity.y,
            .size = entity.size,
            .color = entity.color,
            .id = entity.id,
          };
      };

    for (auto&[client, data] : clients_)
    {
      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id =  id });

      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        entityToSpawnPacket(created));
      


      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id = data.id });
      
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        entityToSpawnPacket(data.entity));
    }
    
    for (auto& bot : bots_)
    {
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE, entityToSpawnPacket(bot));
    }
  }

  void handlePacket(ENetPeer* peer, const PEntityUpdate& packet)
  {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return;

    auto& entity = it->second.entity;

    if (packet.id != entity.id) return;

    entity.x = packet.x;
    entity.y = packet.y;
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
      resetBots();
      registerInLobby(nullptr, 0);
    }
  }

  void updateLogic(float delta)
  {
    for (size_t i = 0; i < bots_.size(); ++i)
    {
      float dx = botTargets_[i][0] - bots_[i].x;
      float dy = botTargets_[i][1] - bots_[i].y;

      if (std::abs(dx) < 0.01f && std::abs(dy) < 0.01f)
      {
        botTargets_[i] = Entity::randomPos();
        continue;
      }

      const float len = std::sqrtf(dx*dx + dy*dy);
      dx /= len;
      dy /= len;

      bots_[i].x += 0.05f * dx * delta;
      bots_[i].y += 0.05f * dy * delta;
    }

    std::vector<Entity*> allEntities;
    allEntities.reserve(bots_.size() + clients_.size());
    for (auto& bot : bots_) allEntities.emplace_back(&bot);
    for (auto&[_, data] : clients_) allEntities.emplace_back(&data.entity);

    for (auto e1 : allEntities)
    {
      for (auto e2 : allEntities)
      {
        if (e1 == e2) continue;

        if (entityDistance(*e1, *e2) + e1->size < e2->size)
        {
          e2->size += e1->size/2;
          e1->size /= 2;

          auto pos = Entity::randomPos();
          e1->x = pos[0];
          e1->y = pos[1];
        }
      }
    }
  }

  void run()
  {
    constexpr auto kSendRate = 60ms;

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

        auto entityToUpdate =
          [](const Entity& entity)
          {
            return PEntityUpdate{
                .x = entity.x,
                .y = entity.y,
                .size = entity.size,
                .id = entity.id,
              };
          };

        for (auto&[to, _] : clients_)
        {
          for (auto&[from, data] : clients_)
          {
            send(to, 1, {}, entityToUpdate(data.entity));
          }

          for (auto& bot : bots_)
          {
            send(to, 1, {}, entityToUpdate(bot));
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
    Entity entity;
  };

  std::unordered_map<ENetPeer*, ClientData> clients_;
  uint32_t idCounter_{1};
  std::vector<Entity> bots_;
  std::vector<std::array<float, 2>> botTargets_;
};

int main(int argc, char** argv)
{
  if (argc != 4)
  {
    spdlog::error("Usage: {} <server port> <lobby address> <lobby port>\n", argv[0]);
    return -1;
  }

  NG_VERIFY(enet_initialize() == 0);
  Defer deinit{[](){ enet_deinitialize(); }};

  ENetAddress address{
    .host = ENET_HOST_ANY,
    .port = static_cast<uint16_t>(std::atoi(argv[1])),
  };

  ServerService server(address);

  server.registerInLobby(argv[2], static_cast<uint16_t>(std::atoi(argv[3])));

  server.run();

  return 0;
}
