#pragma once

#include <cstdint>
#include <array>

#include <enet/enet.h>


enum class PacketType : uint8_t
{
  StartLobby,
  LobbyStarted,
  RegisterClientInLobby,
  RegisterServerInLobby,

  LobbyListUpdate,
  CreateLobby,
  JoinLobby,
  JoinedLobby,
  LeaveLobby,
  StartServerGame,

  SendKey,

  PlayerJoined,
  PlayerLeft,
  Chat,

  SnapshotDelta,
  Snapshot,
  SnapshotDeltaAck,
  
  SpawnEntity,
  DestroyEntity,
  PossesEntity,
  PlayerInput,
  EntityPropsChanged,
  EntityTeleport,
  COUNT,
};

template<PacketType t>
struct PacketBase
{
  PacketType type{t};
};

template<PacketType type>
struct Packet;

#define PROTO_IMPL_PACKET(Name)\
using P##Name = Packet<PacketType::Name>;\
template<>\
struct Packet<PacketType::Name> : PacketBase<PacketType::Name>


PROTO_IMPL_PACKET(StartLobby) { uint32_t id; };
PROTO_IMPL_PACKET(StartServerGame) { uint32_t botCount; };


PROTO_IMPL_PACKET(CreateLobby)
{
  std::array<char, 128> name;
  uint32_t botCount;
};
PROTO_IMPL_PACKET(JoinLobby) { uint32_t id; };
PROTO_IMPL_PACKET(JoinedLobby) { uint32_t id; };
PROTO_IMPL_PACKET(LeaveLobby) { uint32_t id; };

struct LobbyEntry
{
  std::array<char, 128> name;
  uint32_t id;
  uint32_t playerCount;
  uint32_t botCount;
};

PROTO_IMPL_PACKET(LobbyListUpdate)
{
  using Continuation = LobbyEntry;
};

PROTO_IMPL_PACKET(LobbyStarted)
{
  ENetAddress serverAddress;
};

PROTO_IMPL_PACKET(RegisterClientInLobby) {};
PROTO_IMPL_PACKET(RegisterServerInLobby) {};

PROTO_IMPL_PACKET(PlayerJoined)
{
  uint32_t id;
};

PROTO_IMPL_PACKET(PlayerLeft)
{
  uint32_t id;
};

PROTO_IMPL_PACKET(Chat)
{
  uint32_t player;
  std::array<char, 1000> message;
};

using XorKey = std::array<char, 4>;

PROTO_IMPL_PACKET(SendKey) { XorKey key; };
