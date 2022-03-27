#pragma once

#include <cinttypes>
#include <utility>
#include <cstddef>


enum class PacketType : uint8_t
{
    NONE,
    REGISTER,
    HEARTBEAT,
    CHAT,
};

constexpr size_t MAX_PACKET_SIZE = 2048;

struct Header
{
    PacketType type;
    char padding[3] {};
    uint32_t size;
};

struct Packet
{
    Header header;
    std::byte data[];
};
