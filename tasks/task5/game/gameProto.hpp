#pragma once

#include "Entity.hpp"
#include "../common/proto.hpp"


struct EntityDelta {
  uint32_t id;
  // packed
  glm::uint deltaPos;
};

PROTO_IMPL_PACKET(SnapshotDelta)
{
  using Continuation = EntityDelta;
  uint64_t sequence;
};

PROTO_IMPL_PACKET(Snapshot)
{
  using Continuation = Entity;
};

PROTO_IMPL_PACKET(SnapshotDeltaAck)
{
  uint64_t sequence;
};

PROTO_IMPL_PACKET(PlayerInput)
{
  // packed
  glm::uint desiredSpeed; // already pos delta
};

PROTO_IMPL_PACKET(SpawnEntity)
{
  Entity entity;
};

PROTO_IMPL_PACKET(DestroyEntity) { uint32_t id; };

PROTO_IMPL_PACKET(PossesEntity) { uint32_t id; };

PROTO_IMPL_PACKET(EntityPropsChanged)
{
    uint32_t id;
    float size;
    uint32_t color;
};

PROTO_IMPL_PACKET(EntityTeleport)
{
    uint32_t id;
    glm::vec2 pos;
};
