#include <iostream>
#include <enet/enet.h>
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <chrono>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/proto.hpp"


using namespace std::chrono_literals;

struct Lobby
{
  std::string name;
  uint32_t botCount;
  std::vector<ENetPeer*> players;
};

class LobbyService
  : public Service<LobbyService, true>
{
  using Clock = std::chrono::steady_clock;
 public:
  LobbyService(ENetAddress addr)
    : Service(&addr, 32, 2)
  {
    
  }

  std::vector<LobbyEntry> collectEntries()
  {
    std::vector<LobbyEntry> lobbies;
    lobbies.reserve(lobbies_.size());
    for (auto[id, lobby] : lobbies_)
    {
      auto& name = lobbies.emplace_back(LobbyEntry{
          .name = {0},
          .id = id,
          .playerCount = static_cast<uint32_t>(lobby.players.size()),
        }).name;
      std::strncpy(name.data(), lobby.name.c_str(), name.size() - 1);
    }

    return lobbies;
  }

  void broadcastLobbies()
  {
    auto lobbies = collectEntries();
    for (auto peer : clients_)
    {
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PLobbyListUpdate{}, std::span{lobbies.data(), lobbies.size()});
    }
  }

  bool removeFromLobby(ENetPeer* peer, uint32_t id, Lobby& lobby)
  {
    auto it = std::find(lobby.players.begin(), lobby.players.end(), peer);
    if (it == lobby.players.end())
    {
      return false;
    }
  
    std::swap(*it, lobby.players.back());
    lobby.players.pop_back();

    lobbies_.erase(id);
    
    return true;
  }

  void removeFromLobby(ENetPeer* peer)
  {
    for (auto[id, lobby] : lobbies_)
    {
      if (removeFromLobby(peer, id, lobby))
      {
        break;
      }
    }
  }

  void handlePacket(ENetPeer* peer, const PCreateLobby& packet)
  {
    removeFromLobby(peer);

    auto lobby = lobbies_.emplace(lobbyIdCounter_++, Lobby{
      .name = std::string(packet.name.data(), packet.name.size()),
      .botCount = packet.botCount,
      .players = {peer},
    }).first;
    spdlog::info("Creating lobby {}", lobby->second.name);

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PJoinedLobby{ .id = lobby->first, });

    broadcastLobbies();
  }

  void handlePacket(ENetPeer* peer, const PJoinLobby& packet)
  {
    auto it = lobbies_.find(packet.id);
    if (it == lobbies_.end()) return;
    auto& players = it->second.players;

    if (std::find(players.begin(), players.end(), peer) != players.end())
    {
      return;
    }

    removeFromLobby(peer);

    it->second.players.push_back(peer);

    spdlog::info("Player joined lobby {}", it->second.name);

    send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
      PJoinedLobby{ .id = packet.id, });

    broadcastLobbies();
  }

  void handlePacket(ENetPeer* peer, const PLeaveLobby& packet)
  {
    auto it = lobbies_.find(packet.id);
    if (it == lobbies_.end()) return;
    
    removeFromLobby(peer, packet.id, it->second);

    broadcastLobbies();
  }

  void handlePacket(ENetPeer*, const PStartLobby& packet)
  {
    if (servers_.empty())
    {
      spdlog::error("No servers to send clients to!");
      return;
    }

    auto it = lobbies_.find(packet.id);
    if (it == lobbies_.end())
    {
      spdlog::error("Trying to start a non-existing lobby!");
      return;
    }

    Lobby lobby = std::move(it->second);
    lobbies_.erase(it);


    auto server = servers_.back();
    servers_.pop_back();

    send(server, 0, ENET_PACKET_FLAG_RELIABLE, PStartServerGame{ .botCount = lobby.botCount });
    disconnect(server, [](){});

    spdlog::info("Sending {} clients from lobby {} (id {}) to server {}:{}!",
      lobby.players.size(), lobby.name, packet.id, server->address.host, server->address.port);

    for (auto peer : lobby.players)
    {
      send(peer, 0, ENET_PACKET_FLAG_RELIABLE,
        PLobbyStarted{
          .serverAddress = server->address,
        });
    }
  }

  void handlePacket(ENetPeer* client, const PRegisterClientInLobby&)
  {
    spdlog::info("Client {}:{} registered", client->address.host, client->address.port);
    clients_.emplace(client);


    auto lobbies = collectEntries();
    send(client, 0, ENET_PACKET_FLAG_RELIABLE,
      PLobbyListUpdate{}, std::span{lobbies.data(), lobbies.size()});
  }

  void handlePacket(ENetPeer* server, const PRegisterServerInLobby&)
  {
    spdlog::info("Server {}:{} registered", server->address.host, server->address.port);
    servers_.push_back(server);
  }

  void disconnected(ENetPeer* peer)
  {
    if (clients_.erase(peer) > 0)
    {
      removeFromLobby(peer);
    }
    else if (auto it = std::find(servers_.begin(), servers_.end(), peer); it != servers_.end())
    {
      std::swap(*it, servers_.back());
      servers_.pop_back();
    }
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
  std::vector<ENetPeer*> servers_;

  std::unordered_map<uint32_t, Lobby> lobbies_;
  uint32_t lobbyIdCounter_{0};
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
