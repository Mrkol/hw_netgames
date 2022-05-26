#include <iostream>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <chrono>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/proto.hpp"


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

  void registerInLobby(char* addr, uint16_t port)
  {
    ENetAddress address;
    enet_address_set_host(&address, addr);
    address.port = port;

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

    for (auto&[client, data] : clients_)
    {
      send(client, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id =  id });
      
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PPlayerJoined{ .id = data.id });
    }

    clients_.emplace(peer, ClientData{
        .id = id,
      });
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
  }

  void run()
  {
    auto startTime = Clock::now();
    auto lastPollTime = startTime;
    while (true)
    {
      auto now = Clock::now();
      auto delta = now - std::exchange(lastPollTime, now);

      UNUSED(delta);

      auto timeSecs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count());
      for (auto&[client, _] : clients_)
      {
        send(client, 1, {}, PTime{ .time = timeSecs });
      }

      Service::poll();
    }
  }

 private:
  struct ClientData
  {
    uint32_t id;
  };

  std::unordered_map<ENetPeer*, ClientData> clients_;
  uint32_t idCounter_{1};
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
