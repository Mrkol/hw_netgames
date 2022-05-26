#include <iostream>
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <chrono>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/proto.hpp"


using namespace std::chrono_literals;

class LobbyService
  : public Service<LobbyService, true>
{
  using Clock = std::chrono::steady_clock;
 public:
  LobbyService(ENetAddress addr)
    : Service(&addr, 32, 2)
  {
    
  }

  void handlePacket(ENetPeer*, const PStartLobby&)
  {
    if (servers_.empty())
    {
      spdlog::error("No servers to send clients to!");
      return;
    }

    auto server = servers_.front();
    servers_.pop_front();

    spdlog::info("Sending {} clients to server {}:{}!", clients_.size(), server.host, server.port);

    for (auto peer : clients_)
    {
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PLobbyStarted{
          .serverAddress = server,
        });
    }

  }

  void handlePacket(ENetPeer* client, const PRegisterClientInLobby&)
  {
    spdlog::info("Client {}:{} registered", client->address.host, client->address.port);
    clients_.emplace(client);
  }


  void handlePacket(ENetPeer* server, const PRegisterServerInLobby&)
  {
    spdlog::info("Server {}:{} registered", server->address.host, server->address.port);
    servers_.push_back(server->address);
  }

  void disconnected(ENetPeer* peer)
  {
    clients_.erase(peer);
  }

  void run()
  {
    auto lastPollTime = Clock::now();
    while (true)
    {
      auto now = Clock::now();
      auto delta = now - std::exchange(lastPollTime, now);

      UNUSED(delta);

      Service::poll();
    }
  }

 private:
  std::unordered_set<ENetPeer*> clients_;
  std::deque<ENetAddress> servers_;
};

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    spdlog::error("Usage: {} <lobby port>\n", argv[0]);
    return -1;
  }

  NG_VERIFY(enet_initialize() == 0);
  std::atexit(enet_deinitialize);

  ENetAddress address{
    .host = ENET_HOST_ANY,
    .port = static_cast<uint16_t>(std::atoi(argv[1])),
  };

  LobbyService lobby(address);

  lobby.run();

  return 0;
}
