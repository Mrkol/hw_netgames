#include <iostream>
#include <spdlog/spdlog.h>
#include <chrono>
#include <unordered_set>

#include "common/assert.hpp"
#include "common/Service.hpp"
#include "common/AsyncInput.hpp"
#include "common/Allegro.hpp"
#include "common/Replication.hpp"
#include "common/proto.hpp"

#include "game/Entity.hpp"
#include "game/gameProto.hpp"


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


class Client
  : public Service<Client>
  , public Replication<Client>
  , public AsyncInput<Client>
  , public Allegro<Client>
{
  using Clock = std::chrono::steady_clock;

  struct Snapshot
  {
    GameState state;
    Clock::time_point time;
  };

  struct PlayerInputSnapshot
  {
    glm::vec2 vel;
    Clock::time_point time; 
  };

 public:
  Client()
    : Service(nullptr, 2, 2)
  {
    
  }

  Entity* entityById(id_t id)
  {
    auto it = std::find_if(state_.begin(), state_.end(),
      [id](const Entity& e) { return e.id == id; });
    if (it == state_.end())
      return nullptr;
    
    return &*it;
  }
  
  using Replication::handlePacket;

  void handlePacket(ENetPeer*, enet_uint8, const PLobbyListUpdate&, std::span<LobbyEntry> cont)
  {
    lobbies_.assign(cont.begin(), cont.end());
  }

  void handlePacket(ENetPeer*, enet_uint8, const PJoinedLobby& packet)
  {
    currentLobbyId_ = packet.id;
  }

  void handlePacket(ENetPeer* peer, enet_uint8, const PSendKey& packet)
  {
    setKeyFor(peer, packet.key);
  }

  void handlePacket(ENetPeer*, enet_uint8, const PChat& packet)
  {
    std::string line{packet.message.data()};
    std::cout << packet.player << ": " << line << std::endl;
  }

  void handlePacket(ENetPeer*, enet_uint8, const PPlayerJoined& packet)
  {
    otherIds_.emplace(packet.id);
  }

  void handlePacket(ENetPeer*, enet_uint8, const PPlayerLeft& packet)
  {
    otherIds_.erase(packet.id);
  }

  void handlePacket(ENetPeer*, enet_uint8, const PPossesEntity& packet)
  {
    playerEntityId_ = packet.id;
  }

  void handlePacket(ENetPeer*, enet_uint8, const PLobbyStarted& packet)
  {
    disconnect(std::exchange(lobby_peer_, nullptr), []() {});
    connect(packet.serverAddress,
      [this](ENetPeer* server)
      {
        NG_VERIFY(server != nullptr);
        setupReplication(server, 1);
        snapshotHistory_.emplace_back(Snapshot{ .time = Clock::now() });
        server_peer_ = server;
      });
  }

  void handleReplication(ENetPeer*, enet_uint8, std::span<std::byte> bytes)
  {
    auto& newSnapshot = snapshotHistory_.emplace_back(snapshotHistory_.back());
    newSnapshot.time = Clock::now();

    const auto count = bytes.size() / sizeof(Entity);
    NG_ASSERT(bytes.size() % sizeof(Entity) == 0);
    newSnapshot.state.resize(count);
    std::memcpy(newSnapshot.state.data(), bytes.data(), bytes.size());

    if (snapshotHistory_.size() > 10)
    {
      snapshotHistory_.pop_front();
    }
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
    if (line == "/list")
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
  }

  void keyUp(int) {}

  void mouse(int x, int y)
  {
    playerDesiredSpeed_ = {
        static_cast<float>(x - kWidth/2),
        static_cast<float>(y - kHeight/2)
      };
    const float len = glm::length(playerDesiredSpeed_);

    if (len < 1e-3) return;

    playerDesiredSpeed_ /= len;
    playerDesiredSpeed_ *= std::clamp(len - 30, 0.f, 100.f)/100.f;
  }

  void drawGui()
  {
    if (lobby_peer_ != nullptr)
    {
      ImGui::Begin("Lobby browser");
      {
        static std::array<char, 128> lobbyName{0};
        ImGui::InputTextWithHint("##lobbyname", "Lobby name", lobbyName.data(), lobbyName.size() - 1);
        ImGui::SameLine();
        static int botCount = 10;
        ImGui::InputInt("Bots", &botCount);
        if (botCount > 50) botCount = 50;
        if (botCount < 0) botCount = 0;

        if (ImGui::Button("Create lobby")
          && std::strlen(lobbyName.data()) > 0)
        {
          send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE, PCreateLobby{
            .name = lobbyName,
            .botCount = static_cast<uint32_t>(botCount),
          });
        }
      }
      
      static constexpr uint32_t NONE = static_cast<uint32_t>(-1);
      static uint32_t selectedId = NONE;
      if (ImGui::BeginListBox("Lobbies"))
      {
        for (auto& entry : lobbies_)
        {
          if (ImGui::Selectable(entry.name.data(), entry.id == selectedId))
          {
            selectedId = entry.id;
          }
          ImGui::SameLine();
          ImGui::Text("P: %d", entry.playerCount);
          ImGui::SameLine();
          ImGui::Text("B: %d", entry.botCount);
          if (currentLobbyId_.has_value() && currentLobbyId_.value() == entry.id)
          {
            ImGui::SameLine();
            ImGui::Text("(JOINED)");
          }
        }
        ImGui::EndListBox();
      }

      bool currentSelected = currentLobbyId_.has_value() && currentLobbyId_.value() == selectedId;

      if (currentSelected) ImGui::BeginDisabled();

      if (ImGui::Button("Join") && selectedId != NONE)
      {
        send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE, PJoinLobby{ .id = selectedId });
      }

      if (currentSelected) ImGui::EndDisabled();

      ImGui::SameLine();

      if (!currentLobbyId_.has_value()) ImGui::BeginDisabled();

      if (ImGui::Button("Leave") && currentLobbyId_.has_value())
      {
        send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE, PLeaveLobby{ .id = currentLobbyId_.value() });
      }

      ImGui::SameLine();

      if (ImGui::Button("Start") && currentLobbyId_.has_value())
      {
        send(lobby_peer_, 0, ENET_PACKET_FLAG_RELIABLE, PStartLobby{ .id = currentLobbyId_.value() });
      }

      if (!currentLobbyId_.has_value()) ImGui::EndDisabled();

      ImGui::End();
    }
  }

  void draw()
  {
    glm::vec2 playerPos{0, 0};
    for (auto& entity : state_)
    {
      if (entity.id == playerEntityId_)
      {
        playerPos = entity.pos;
        break;
      }
    }

    float scale = static_cast<float>(kWidth + kHeight) / 2.f;

    auto worldToScreen =
      [playerPos, scale](glm::vec2 v)
      {
        glm::vec2 screen{kWidth, kHeight};
        return screen/2.f + (v - playerPos) * scale;
      };

    if (server_peer_ != nullptr)
    {
      int bars = 10;
      for (int i = 0; i <= bars; ++i)
      {
        float x = static_cast<float>(i)/static_cast<float>(bars); 
        auto a = worldToScreen({0, x});
        auto b = worldToScreen({1, x});
        al_draw_line(a.x, a.y, b.x, b.y, al_map_rgba(100, 100, 100, 128), 2);
        auto c = worldToScreen({x, 0});
        auto d = worldToScreen({x, 1});
        al_draw_line(c.x, c.y, d.x, d.y, al_map_rgba(100, 100, 100, 128), 2);
      }

    }

    for (auto& entity : state_)
    {
      auto p = worldToScreen(entity.pos);
      al_draw_filled_circle(
        p.x,
        p.y,
        entity.size*scale,
        colorToAllegro(entity.color));
    }

    al_draw_text(getFont(), al_map_rgb(255, 255, 255), 0, 0, 0, "ESC = /exit; B = /begin");
  }

  static float durationToSecs(Clock::duration d)
  {
    return std::chrono::duration_cast<std::chrono::duration<float>>(d).count();
  }
  
  std::vector<Entity> interpolate(Clock::time_point time)
  {
    while (snapshotHistory_.size() > 2 && snapshotHistory_[1].time < time)
    {
      snapshotHistory_.pop_front();
    }
    
    if (snapshotHistory_.size() == 1)
    {
      return snapshotHistory_.front().state;
    }
    else if (snapshotHistory_.size() >= 2)
    {
      auto& old = snapshotHistory_[0];
      auto& recent = snapshotHistory_[1];

      std::vector<Entity> result;
      result.reserve(recent.state.size());

      zipById(
        std::span{old.state.data(), old.state.size()},
        std::span{recent.state.data(), recent.state.size()},
        [&result, to = old.time, tr = recent.time, time]
        (const Entity& o, const Entity& r)
        {
          auto h = durationToSecs(time - to)
            / durationToSecs(tr - to);
          
          result.emplace_back(Entity{
              .pos = r.pos*h + (1 - h)*o.pos,
              .size = r.size*h + (1 - h)*o.size,
              .color = r.color,
              .id = r.id,
            });
        });

      return result;
    }

    return state_;
  }

  void interpolatePlayer(Clock::time_point now, float delta)
  {
    auto player = entityById(playerEntityId_);
    if (player == nullptr)
    {
      return;
    }

    auto& entity = *player;

    playerVelHistory_.emplace_back(PlayerInputSnapshot{
      .vel = playerDesiredSpeed_,
      .time = now,
    });
    entity.vel = playerDesiredSpeed_;
    entity.simulate(delta);
    
    if (playerServerPredicted.has_value())
    {
      playerServerPredicted->vel = playerDesiredSpeed_;
      playerServerPredicted->simulate(delta);

      glm::vec2 compensation =
        (playerServerPredicted->pos - entity.pos) * delta;
      if (glm::length(compensation) > entity.size/100.f)
      {
        entity.pos += compensation;
        playerServerPredicted->pos -= compensation;
      }
    }


    const auto& snapshot = snapshotHistory_.back();

    auto it = std::find_if(snapshot.state.begin(), snapshot.state.end(),
      [this](const Entity& e) { return e.id == playerEntityId_; });

    if (it == snapshot.state.end()) {
      return;
    }

    // std::chrono is f'n awesome
    Clock::time_point last = snapshot.time - server_peer_->roundTripTime/2 * 1ms;
    while (!playerVelHistory_.empty()
      && playerVelHistory_.front().time < last)
    {
      playerVelHistory_.pop_front();
    }

    Entity predicted = entity;
    predicted.pos = it->pos;
    for (auto&[vel, time] : playerVelHistory_)
    {
      predicted.vel = vel;
      predicted.simulate(durationToSecs(time - last));
      last = time;
    }

    playerServerPredicted = predicted;
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
      float delta = durationToSecs(now - std::exchange(currentTime, now));

      AsyncInput::poll();
      Allegro::poll();
      Service::poll();

      if (server_peer_ != nullptr)
      {
        constexpr auto forcedLagMs = 250ms;
        auto time = now - forcedLagMs;

        std::optional<Entity> playerBackup;
        if (auto player = entityById(playerEntityId_)) playerBackup = *player;

        state_ = interpolate(time);
        
        if (auto player = entityById(playerEntityId_); player && playerBackup)
        {
            // keep local simulation position
            player->pos = playerBackup->pos;
        }
        

        interpolatePlayer(now, delta);
      }

      if (server_peer_ != nullptr
        && playerEntityId_ != kInvalidId
        && (now - lastSendTime) > kSendRate)
      {
        auto sz = playerVelHistory_.size();
        if (sz < 2
          || glm::length(playerVelHistory_[sz - 1].vel - playerVelHistory_[sz - 2].vel) > 1e-3)
        {
          lastSendTime = now;
          auto packed = glm::packSnorm2x16(playerDesiredSpeed_);
          replicate(server_peer_, 1, {reinterpret_cast<std::byte*>(&packed), sizeof(packed)});
        }
      }
    }

    Allegro::stop();
    std::cout << "Press ENTER to exit..." << std::endl;
    AsyncInput::stop();
  }

 private:
  ENetPeer* lobby_peer_{nullptr};
  ENetPeer* server_peer_{nullptr};

  std::vector<LobbyEntry> lobbies_;
  std::optional<uint32_t> currentLobbyId_;

  std::unordered_set<uint32_t> otherIds_;
  bool shouldStop_{false};

  id_t playerEntityId_;
  GameState state_;

  std::deque<Snapshot> snapshotHistory_;

  glm::vec2 playerDesiredSpeed_{0,0};
  // kostyl: we don't have a predicted pos for the first few frames
  std::optional<Entity> playerServerPredicted;
  std::deque<PlayerInputSnapshot> playerVelHistory_; 
};


int main(int argc, char** argv)
{
  if (argc != 3)
  {
    spdlog::error("Usage: {} <lobby addr> <lobby port>\n", argv[0]);
    return -1;
  }

  NG_VERIFY(enet_initialize() == 0);
  std::atexit(enet_deinitialize);

  Client client;

  client.joinLobby(argv[1], static_cast<enet_uint16>(std::atoi(argv[2])));

  client.run();

  return 0;
}
