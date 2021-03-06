#include <iostream>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <chrono>

#include "common/assert.hpp"
#include "common/Service.hpp"
#include "common/Replication.hpp"
#include "common/AsyncInput.hpp"
#include "common/proto.hpp"

#include "game/Entity.hpp"
#include "game/gameProto.hpp"


using namespace std::chrono_literals;


class Server
  : public Service<Server, true>
  , public Replication<Server>
{
  using Clock = std::chrono::steady_clock;
 public:
  Server(ENetAddress addr)
    : Service(&addr, 32, 2)
  {
  }

  void resetGame(size_t bots)
  {
    state_.clear();
    botTargets_.clear();

    state_.reserve(bots);
    botTargets_.reserve(bots);
    for (size_t i = 0; i < bots; ++i)
    {
      auto entity = Entity::create();
      auto id = state_.emplace_back(entity).id;
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
      });
  }

  using Replication::handlePacket;

  void handlePacket(ENetPeer* peer, enet_uint8, const PStartServerGame& packet)
  {
    resetGame(packet.botCount);
    spdlog::info("Starting game with {} bots as per external command!", packet.botCount);
  }


  void handlePacket(ENetPeer* peer, enet_uint8, PChat packet)
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

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE, PSendKey{
      .key = TOP_SECRET_KEY,
    });

    setKeyFor(peer, TOP_SECRET_KEY);
    setupReplication(peer, 1);

    auto id = idCounter_++;


    auto& playerEntity = state_.emplace_back(Entity::create());

    clients_.emplace(peer, ClientData{
        .id = id,
        .entityId = playerEntity.id,
      });

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PPossesEntity{
        .id = playerEntity.id,
      });


    for (auto&[client, data] : clients_)
    {
      if (client == peer) continue;

      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id =  id });

      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id = data.id });
    }

    broadcastDeltas();
  }

  Entity* entityById(id_t id)
  {
    auto& entities = state_;
    auto it = std::find_if(entities.begin(), entities.end(),
      [id](const Entity& e) { return e.id == id; });
    if (it == entities.end())
      return nullptr;
    
    return &*it;
  }

  void handleReplication(ENetPeer* peer, enet_uint8, std::span<const std::byte> bytes)
  {
    NG_ASSERT(bytes.size() == sizeof(glm::uint));

    auto it = clients_.find(peer);
    if (it == clients_.end()) return;

    auto* entity = entityById(it->second.entityId);

    if (entity == nullptr) return;

    auto desiredSpeed = glm::unpackSnorm2x16(*reinterpret_cast<const glm::uint*>(bytes.data()));

    float len = glm::length(desiredSpeed);
    
    if (len < 1e-3)
    {
      entity->vel = {0, 0};
      return;
    }
    
    entity->vel = desiredSpeed / len * std::clamp(len, 0.f, 1.f);
  }

  void disconnected(ENetPeer* peer)
  {
    spdlog::info("{}:{} left", peer->address.host, peer->address.port);
    
    auto it = clients_.find(peer);
    if (it == clients_.end())
    {
      return;
    }

    ClientData erasedData = std::move(it->second);
    clients_.erase(it);
    stopReplication(peer, 1);

    for (auto&[client, data] : clients_)
    {
      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerLeft{ .id = erasedData.id });
    }

    if (clients_.empty())
    {
      spdlog::info("All players left, requeueing in lobby");
      state_.clear();
      botTargets_.clear();
      registerInLobby(nullptr, 0);
    }
  }

  void updateLogic(float delta)
  {
    auto& entities = state_;

    for (auto& entity : entities)
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

    for (auto& e1 : entities)
    {
      for (auto& e2 : entities)
      {
        if (&e1 == &e2) continue;

        if (glm::length(e1.pos - e2.pos) + e1.size < e2.size)
        {
          e2.size += e1.size/2;
          e1.size /= 2;

          e1.pos = Entity::randomPos();
        }
      }
    }

    for (size_t i = 0; i < entities.size();)
    {
      if (entities[i].size < 1e-3)
      {
        std::swap(entities[i], entities.back());
        entities.pop_back();
      }
      else
      {
        ++i;
      }
    }
  }

  void broadcastDeltas()
  {
    if (clients_.empty()) return;

    std::vector<std::byte> state(state_.size()*sizeof(Entity));
    std::memcpy(state.data(), state_.data(), state.size());

    for (auto&[to, clientData] : clients_)
    {
      replicate(to, 1, {state.data(), state.size()});
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

        broadcastDeltas();
      }

      Service::poll();
    }
  }

 private:
  GameState state_;

  std::unordered_map<id_t, glm::vec2> botTargets_;


  struct ClientData
  {
    uint32_t id;
    id_t entityId;
  };

  std::unordered_map<ENetPeer*, ClientData> clients_;
  uint32_t idCounter_{1};

  constexpr static XorKey TOP_SECRET_KEY { '\xDE', '\xAD', '\xBE', '\xEF' };
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

  Server server(address);

  server.registerInLobby(argv[2], static_cast<uint16_t>(std::atoi(argv[3])));

  server.run();

  return 0;
}
