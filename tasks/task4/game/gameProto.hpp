#pragma once

#include "Entity.hpp"
#include "../common/proto.hpp"


PROTO_IMPL_PACKET(EntitySnapshot)
{
  uint32_t id;
  glm::vec2 pos;
};

PROTO_IMPL_PACKET(PlayerInput)
{
  glm::vec2 desiredSpeed;
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
