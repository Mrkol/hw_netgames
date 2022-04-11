#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <limits>

using id_t = uint32_t;

constexpr id_t kInvalidId = std::numeric_limits<id_t>::max();
struct Entity
{
  float x = 0.f;
  float y = 0.f;
  float size = 0.1f;
  uint32_t color = 0xffffffff;
  id_t id = kInvalidId;

  static id_t firstFreeId;
  static Entity create();
  static std::array<float, 2> randomPos();
};

float entityDistance(const auto& first, const auto& second)
{
  float dx = first.x - second.x;
  float dy = first.y - second.y;
  return std::sqrtf(dx*dx + dy*dy);
}
