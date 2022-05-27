#pragma once

#include <vector>
#include <cstring>
#include <type_traits>


class ByteOstream
{
public:
  template<class T>
    requires std::is_trivially_copyable_v<T>
  ByteOstream& operator<<(const T& t)
  {
    const auto offset = data_.size();
    data_.resize(data_.size() + sizeof(T));
    std::memcpy(data_.data() + offset, &t, sizeof(T));
    return *this;
  }

  template<class T>
    requires std::is_trivially_copyable_v<T>
  ByteOstream& operator<<(std::span<T> vec)
  {
    operator<<(vec.size());
    const auto offset = data_.size();
    data_.resize(data_.size() + sizeof(T)*vec.size());
    std::memcpy(data_.data() + offset, vec.data(), sizeof(T)*vec.size());
    return *this;
  }

  ByteOstream& operator<<(const std::vector<bool>& vec)
  {
    operator<<(vec.size());

    const auto offset = data_.size();

    const size_t bytesForFlags = (vec.size() + 7) / 8;
    data_.resize(data_.size() + bytesForFlags);


    for (size_t i = 0; i < bytesForFlags; ++i)
    {
      uint8_t packed = 0;
      for (size_t j = 0; j < 8; ++j)
      {
        size_t idx = 8*i + j;
        if (idx >= vec.size()) break;
        packed |= vec[idx] << j;
      }
      
      data_[offset + i] = static_cast<std::byte>(packed);
    }

    return *this;
  }

  std::vector<std::byte> finalize() &&
  {
    return std::move(data_);
  }

private:
  std::vector<std::byte> data_;
};

class ByteIstream
{
public:
  ByteIstream(std::span<std::byte const> data)
    : data_{data}
  {
  }

  template<class T>
    requires std::is_trivially_copyable_v<T>
  ByteIstream& operator>>(T& t)
  {
    std::memcpy(&t, data_.data(), sizeof(T));
    eat(sizeof(T));
    return *this;
  }

  template<class T>
    requires std::is_trivially_copyable_v<T>
  ByteIstream& operator>>(std::vector<T>& vec)
  {
    vec.clear();
    size_t size;
    operator>>(size);
    vec.resize(size);
    std::memcpy(vec.data(), data_.data(), sizeof(T)*size);
    eat(sizeof(T)*size);
    return *this;
  }

  ByteIstream& operator>>(std::vector<bool>& vec)
  {
    vec.clear();
    size_t size;
    operator>>(size);
    vec.reserve(size);

    for (size_t i = 0; i < size; ++i)
    {
      const auto byte = static_cast<uint8_t>(data_[i/8]);
      const auto bit = (byte >> (i%8)) & 1;
      vec.emplace_back(bit != 0);
    }

    eat((size + 7) / 8);

    return *this;
  }

private:
  void eat(size_t bytes)
  {
    data_ = data_.subspan(bytes);
  }

private:
  std::span<std::byte const> data_;
};


