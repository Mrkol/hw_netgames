#pragma once

#include "Entity.hpp"
#include "../common/proto.hpp"


enum class EntityField : uint32_t
{
  // no reflection :(
  // each is 64 bits
  Pos, SizeColor
};

struct EntityDelta {
  uint32_t id;
  EntityField field;
  uint64_t value;
};

PROTO_IMPL_PACKET(SnapshotDelta)
{
  using Continuation = EntityDelta;
  uint64_t sequence;
  uint64_t __padding;
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
