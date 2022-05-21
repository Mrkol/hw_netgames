#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <vector>
#include <concepts>
#include <span>
#include <limits>
#include <glm/glm.hpp>

using id_t = uint32_t;

constexpr id_t kInvalidId = std::numeric_limits<id_t>::max();
struct Entity
{
  glm::vec2 pos;
  glm::vec2 vel;
  float size = 0.1f;
  uint32_t color = 0xffffffff;
  id_t id = kInvalidId;

  void simulate(float dt);

  static id_t firstFreeId;
  static Entity create();
  static glm::vec2 randomPos();
};

struct GameState
{
  uint64_t sequence;
  std::vector<Entity> entities;
};

template<class T>
concept HasId = requires(T t) { { t.id } -> std::convertible_to<id_t>; };

template<HasId T, HasId U, std::invocable<T&, U&> F>
void zipById(std::span<T> first, std::span<U> second, F f)
{
  auto comp = [](const auto& fst, const auto& snd) { return fst.id < snd.id; };

  std::sort(first.begin(), first.end(), comp);
  std::sort(second.begin(), second.end(), comp);

  size_t i = 0;
  size_t j = 0;
  while (i < first.size() && j < second.size())
  {
    if (first[i].id < second[j].id)
    {
      ++i;
      continue;
    }
    if (first[i].id > second[j].id)
    {
      ++j;
      continue;
    }

    f(first[i], second[j]);
    ++i;
    ++j;
  }
}
