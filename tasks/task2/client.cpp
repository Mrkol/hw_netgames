#include <iostream>
#include <enet/enet.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <unordered_set>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/proto.hpp"


using namespace std::chrono_literals;

class ClientService
  : public Service<ClientService>
  , public AsyncInput<ClientService>
{
  using Clock = std::chrono::steady_clock;
 public:
  ClientService()
    : Service(nullptr, 2, 2)
  {
    
  }


  void handlePacket(ENetPeer*, const PTime& packet)
  {
    serverTime_ = packet.time;
  };

  void handlePacket(ENetPeer*, const PChat& packet)
  {
    std::string line{packet.message.data()};
    std::cout << packet.player << ": " << line << std::endl;
  }

  void handlePacket(ENetPeer*, const PPlayerJoined& packet)
  {
    otherIds_.emplace(packet.id);
  }

  void handlePacket(ENetPeer*, const PPlayerLeft& packet)
  {
    otherIds_.erase(packet.id);
  }

  void handlePacket(ENetPeer*, const PLobbyStarted& packet)
  {
    disconnect(std::exchange(lobby_peer_, nullptr), []() {});
    connect(packet.serverAddress,
      [this](ENetPeer* server)
      {
        NG_VERIFY(server != nullptr);
        server_peer_ = server;
      });
  }

  void handleLine(std::string_view line)
  {
    if (line == "/begin" && lobby_peer_ != nullptr)
    {
      send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE,
        PStartLobby{});
    }
    else if (line == "/list")
    {
      for (auto id : otherIds_)
      {
        std::cout << id << " ";
      }
      std::cout << std::endl;
    }
    else if (line == "/exit" && server_peer_ != nullptr)
    {
      disconnect(std::exchange(server_peer_, nullptr),
        [this](){
          shouldStop_ = true;
        });
    }
    else if (server_peer_ != nullptr)
    {
      PChat packet { .player = 0, .message = {0} };
      std::strncpy(packet.message.data(), line.data(),
        std::min(packet.message.size() - 1, line.size()));
      send(server_peer_, 1, {}, packet);
    }
  }

  void joinLobby(char* addr, uint16_t port)
  {
    ENetAddress address;
    enet_address_set_host(&address, addr);
    address.port = port;

    connect(address,
      [this](ENetPeer* lobby)
      {
        NG_VERIFY(lobby != nullptr);
        send(lobby, 0, ENET_PACKET_FLAG_RELIABLE, PRegisterClientInLobby{});
        lobby_peer_ = lobby;
      });
  }

  void run()
  {
    auto startTime = Clock::now();
    auto lastPollTime = startTime;

    auto lastServerTimePrint = startTime;
    constexpr auto kServerTimePrintInterval = 5s;

    while (!shouldStop_)
    {
      auto now = Clock::now();
      auto delta = now - std::exchange(lastPollTime, now);
      UNUSED(delta);

      if (now - lastServerTimePrint > kServerTimePrintInterval && serverTime_ != 0)
      {
        lastServerTimePrint = now;
        std::cout << "Server time is " << serverTime_ << "ms." << std::endl;
      }

      AsyncInput::poll();
      Service::poll();
    }

    AsyncInput::stop();
  }

 private:
  ENetPeer* lobby_peer_{nullptr};
  ENetPeer* server_peer_{nullptr};

  std::unordered_set<uint32_t> otherIds_;
  uint64_t serverTime_{0};
  bool shouldStop_{false};
};


int main(int argc, char** argv)
{
  if (argc != 3)
  {
    spdlog::error("Usage: {} <lobby addr> <lobby port>\n", argv[0]);
    return -1;
  }

  NG_VERIFY(enet_initialize() == 0);
  Defer deinit{[](){ enet_deinitialize(); }};

  ClientService client;

  client.joinLobby(argv[1], static_cast<enet_uint16>(std::atoi(argv[2])));

  client.run();

  return 0;
}
