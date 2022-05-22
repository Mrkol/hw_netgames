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
  }

  void resetGame(size_t bots)
  {
    stateHistory_.clear();
    botTargets_.clear();

    auto& initial = stateHistory_.emplace_back();
    initial.sequence = 0;
    initial.entities.reserve(bots);
    botTargets_.reserve(bots);
    for (size_t i = 0; i < bots; ++i)
    {
      auto entity = Entity::create();
      auto id = initial.entities.emplace_back(entity).id;
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

  void handlePacket(ENetPeer* peer, const PStartServerGame& packet)
  {
    resetGame(packet.botCount);
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

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE, PSendKey{
      .key = TOP_SECRET_KEY,
    });

    setKeyFor(peer, TOP_SECRET_KEY);

    auto id = idCounter_++;

    auto& newState = stateHistory_.emplace_front(stateHistory_.front());


    auto& playerEntity = newState.entities.emplace_back(Entity::create());

    clients_.emplace(peer, ClientData{
        .id = id,
        .entityId = playerEntity.id,
        .lastSequenceAck = ++newState.sequence,
      });

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PPossesEntity{
        .id = playerEntity.id,
      });

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE, PSnapshot{},
      std::span(newState.entities.data(), newState.entities.size()));

    for (auto&[client, data] : clients_)
    {
      if (client == peer) continue;

      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id =  id });

      send(client, 0, ENET_PACKET_FLAG_RELIABLE, PSpawnEntity{ .entity = playerEntity });
      
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id = data.id });
    }
  }

  Entity* entityById(id_t id)
  {
    auto& entities = stateHistory_.front().entities;
    auto it = std::find_if(entities.begin(), entities.end(),
      [id](const Entity& e) { return e.id == id; });
    if (it == entities.end())
      return nullptr;
    
    return &*it;
  }

  void handlePacket(ENetPeer* peer, const PPlayerInput& packet)
  {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return;

    auto* entity = entityById(it->second.entityId);

    if (entity == nullptr) return;

    auto desiredSpeed = glm::unpackSnorm2x16(packet.desiredSpeed);

    float len = glm::length(desiredSpeed);
    
    if (len < 1e-3)
    {
      entity->vel = {0, 0};
      return;
    }
    
    entity->vel = desiredSpeed / len * std::clamp(len, 0.f, 1.f);
  }

  void handlePacket(ENetPeer* peer, const PSnapshotDeltaAck& packet)
  {
    uint64_t minAck = packet.sequence;
    for (auto[p, data] : clients_)
    {
      if (p == peer)
      {
        data.lastSequenceAck = packet.sequence;
      }

      minAck = std::min(minAck, data.lastSequenceAck);
    }

    while (stateHistory_.size() > 2 && stateHistory_.back().sequence < minAck)
    {
      stateHistory_.pop_back();
    }
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
      stateHistory_.clear();
      botTargets_.clear();
      registerInLobby(nullptr, 0);
    }
  }

  void updateLogic(float delta)
  {
    auto& newState = stateHistory_.emplace_front(stateHistory_.front());
    ++newState.sequence;

    for (auto& entity : newState.entities)
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

    for (auto& e1 : newState.entities)
    {
      for (auto& e2 : newState.entities)
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

    for (size_t i = 0; i < newState.entities.size();)
    {
      if (newState.entities[i].size < 1e-3)
      {
        auto id = newState.entities[i].id;
        std::swap(newState.entities[i], newState.entities.back());
        newState.entities.pop_back();

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

  auto& stateBySequence(uint64_t s)
  {
    auto it = stateHistory_.rbegin();
    while (it->sequence < s) { ++it; }
    return *it;
  }

  void broadcastDeltas()
  {
    if (stateHistory_.empty()) {
      return;
    }

    const auto currentSequence = stateHistory_.front().sequence;
    auto& newEntities = stateHistory_.front().entities;

    std::vector<EntityDelta> deltas;
    deltas.reserve(newEntities.size());
    for (auto&[to, clientData] : clients_)
    {
      deltas.clear();
      auto& oldEntities = stateBySequence(clientData.lastSequenceAck).entities;
      
      zipById(
        // Dear Clang, f'ck you.
        std::span(newEntities.data(), newEntities.size()),
        std::span(oldEntities.data(), oldEntities.size()),
        [&deltas](const Entity& n, const Entity o)
        {
          if (glm::length(n.pos - o.pos) > 1e-3)
          {
            deltas.emplace_back(
              EntityDelta {
                .id = n.id,
                .field = EntityField::Pos,
                .value = std::bit_cast<uint64_t>(n.pos),
              });
          }
          
          if (glm::abs(n.size - o.size) > 1e-3 || n.color != o.color)
          {
            deltas.emplace_back(
              EntityDelta {
                .id = n.id,
                .field = EntityField::SizeColor,
                .value = static_cast<uint64_t>(std::bit_cast<uint32_t>(n.size)) << 32
                  | static_cast<uint64_t>(n.color),
              });
          }
        });

      send(to, 1, {}, PSnapshotDelta{ .sequence = currentSequence },
        std::span(deltas.data(), deltas.size()));
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
  // front is latest
  std::deque<GameState> stateHistory_;

  std::unordered_map<id_t, glm::vec2> botTargets_;


  struct ClientData
  {
    uint32_t id;
    id_t entityId;
    uint64_t lastSequenceAck;
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

  ServerService server(address);

  server.registerInLobby(argv[2], static_cast<uint16_t>(std::atoi(argv[3])));

  server.run();

  return 0;
}
