#include "Entity.hpp"
#include <random>


id_t Entity::firstFreeId = 0;
static std::default_random_engine engine;
static std::uniform_int_distribution<uint32_t> colorDistr(0xffffff);
static std::uniform_real_distribution<float> coordDistr(0, 1);
static std::uniform_real_distribution<float> sizeDistr(0.001, 0.05);


Entity Entity::create()
{
  Entity result{
    .x = coordDistr(engine),
    .y = coordDistr(engine),
    .size = sizeDistr(engine),
    .color = colorDistr(engine) | (128 << 24),
    .id = firstFreeId++,
  };

  if (firstFreeId == kInvalidId)
  {
    ++firstFreeId;
  }

  return result;
}

std::array<float, 2> Entity::randomPos()
{
  return {
    coordDistr(engine),
    coordDistr(engine),
  };
}

