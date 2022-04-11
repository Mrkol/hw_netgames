#include <iostream>
#include <enet/enet.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <unordered_set>

#include "common/assert.hpp"
#include "common/common.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/Allegro.hpp"
#include "common/proto.hpp"

#include "game/Entity.hpp"


using namespace std::chrono_literals;



ALLEGRO_COLOR colorToAllegro(uint32_t color)
{
  return al_map_rgba
    ( (color >> 0)  & 0xff
    , (color >> 8)  & 0xff
    , (color >> 16) & 0xff
    , (color >> 24) & 0xff
    );
}


class ClientService
  : public Service<ClientService>
  , public AsyncInput<ClientService>
  , public Allegro<ClientService>
{
  using Clock = std::chrono::steady_clock;
 public:
  ClientService()
    : Service(nullptr, 2, 2)
  {
    
  }

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

  void handlePacket(ENetPeer*, const PPossesEntity& packet)
  {
    player_.id = packet.id;
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

  void handlePacket(ENetPeer*, const PEntityUpdate& packet)
  {
    if (packet.id == player_.id)
    {
      player_.size = packet.size;

      if (entityDistance(player_, packet) > 0.05f)
      {
        player_.x = packet.x;
        player_.y = packet.y;
      }

      return;
    }

    accumulatedUpdates_.emplace_back(packet);
  }

  void handlePacket(ENetPeer*, const PSpawnEntity& packet)
  {
    if (packet.id == player_.id)
    {
      player_.x = packet.x;
      player_.y = packet.y;
      player_.size = packet.size;
      player_.color = packet.color;
      return;
    }

    entities_.emplace_back(Entity {
      .x = packet.x,
      .y = packet.y,
      .size = packet.size,
      .color = packet.color,
      .id = packet.id,
    });
  }

  void begin()
  {
    if (lobby_peer_ != nullptr)
    {
      send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE,
        PStartLobby{});
    }
    
  }

  void handleLine(std::string_view line)
  {
    if (line == "/begin")
    {
      begin();
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
      close();
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

  void close()
  {
    auto cb =
      [this] ()
      {
        shouldStop_ = true;
      };
    if (server_peer_ != nullptr)
    {
      disconnect(std::exchange(server_peer_, nullptr), cb);
    }
    else if (lobby_peer_ != nullptr)
    {
      disconnect(std::exchange(lobby_peer_, nullptr), cb);
    }
    else
    {
      cb();
    }
  }

  void keyDown(int keycode)
  {
    if (keycode == ALLEGRO_KEY_ESCAPE)
    {
      close();
    }
    else if (keycode == ALLEGRO_KEY_B)
    {
      begin();
    }
  }

  void keyUp(int) {}

  void mouse(int x, int y)
  {
    float dx = static_cast<float>(x - kWidth/2);
    float dy = static_cast<float>(y - kHeight/2);
    float coeff = std::sqrtf(dx*dx + dy*dy);
    coeff = std::clamp(coeff - 30, 0.f, 100.f)/100.f / coeff;
    dx *= coeff;
    dy *= coeff;

    playerTarget_.x = dx;
    playerTarget_.y = dy;
  }

  void draw()
  {
    al_draw_text(getFont(), al_map_rgb(255, 255, 255), 0, 0, 0, "ESC = /exit; B = /begin");
    
    auto scale = (kWidth + kHeight) / 2;

    for (auto& entity : entities_)
    {
      al_draw_filled_circle(
        kWidth/2 + (entity.x - player_.x) * scale,
        kHeight/2 + (entity.y - player_.y) * scale,
        entity.size*scale,
        colorToAllegro(entity.color));
    }

    if (player_.id != kInvalidId)
    {
      al_draw_filled_circle(kWidth/2, kHeight/2, player_.size*scale,
        colorToAllegro(player_.color));
    }
  }

  void run()
  {
    constexpr auto kSendRate = 60ms;

    auto startTime = Clock::now();
    auto currentTime = startTime;
    auto lastSendTime = startTime;

    while (!shouldStop_)
    {
      auto now = Clock::now();
      float delta =
        std::chrono::duration_cast<std::chrono::duration<float>>(
          now - std::exchange(currentTime, now)).count();

      AsyncInput::poll();
      Allegro::poll();
      Service::poll();

      if (server_peer_ != nullptr)
      {
        auto cmp = [](const auto& l, const auto& r) { return l.id < r.id; };
        std::sort(accumulatedUpdates_.begin(), accumulatedUpdates_.end(), cmp);
        std::sort(entities_.begin(), entities_.end(), cmp);

        size_t i = 0;
        size_t j = 0;
        while (i < entities_.size() && j < accumulatedUpdates_.size())
        {
          if (entities_[i].id < accumulatedUpdates_[j].id) 
          {
            ++i;
            continue;
          }
          if (entities_[i].id > accumulatedUpdates_[j].id)
          {
            ++j;
            continue;
          }
          entities_[i].x = accumulatedUpdates_[j].x;
          entities_[i].y = accumulatedUpdates_[j].y;
          entities_[i].size = accumulatedUpdates_[j].size;
          ++j;
        }

        accumulatedUpdates_.clear();
      }

      player_.x += 0.5f * playerTarget_.x * delta;
      player_.y += 0.5f * playerTarget_.y * delta;

      if (server_peer_ != nullptr && player_.id != kInvalidId
        && (now - lastSendTime) > kSendRate)
      {
        lastSendTime = now;
        send(server_peer_, 1, {}, PEntityUpdate{
            .x = player_.x,
            .y = player_.y,
            .size = 0,
            .id = player_.id,
          });
      }
    }

    Allegro::stop();
    std::cout << "Press ENTER to exit..." << std::endl;
    AsyncInput::stop();
  }

 private:
  ENetPeer* lobby_peer_{nullptr};
  ENetPeer* server_peer_{nullptr};

  std::unordered_set<uint32_t> otherIds_;
  bool shouldStop_{false};

  Entity player_;
  std::vector<Entity> entities_;
  std::vector<PEntityUpdate> accumulatedUpdates_;

  struct { float x, y; } playerTarget_;
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
