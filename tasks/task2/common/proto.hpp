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
  PlayerJoined,
  PlayerLeft,
  Chat,
  Time,
  COUNT,
};

template<PacketType t>
struct PacketBase
{
  PacketType type{t};
};

template<PacketType type>
struct Packet;

#define IMPL_PACKET(Name)\
using P##Name = Packet<PacketType::Name>;\
template<>\
struct Packet<PacketType::Name> : PacketBase<PacketType::Name>


IMPL_PACKET(StartLobby) {};

IMPL_PACKET(LobbyStarted)
{
  ENetAddress serverAddress;
};

IMPL_PACKET(RegisterClientInLobby) {};
IMPL_PACKET(RegisterServerInLobby) {};

IMPL_PACKET(PlayerJoined)
{
  uint32_t id;
};

IMPL_PACKET(PlayerLeft)
{
  uint32_t id;
};

IMPL_PACKET(Chat)
{
  uint32_t player;
  std::array<char, 1000> message;
};

IMPL_PACKET(Time)
{
  uint64_t time;
};


#undef IMPL_PACKET
