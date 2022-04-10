#pragma once

#include <optional>
#include <utility>
#include <cstdlib>

extern "C"
{

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
}

#define VERIFY(pred)              \
  do                              \
  {                               \
    if (!(pred))                  \
    {                             \
      perror(#pred " violated!"); \
      std::abort();               \
    }                             \
  } while (false);

template <class F>
struct Defer
{
  F f;
  ~Defer() { f(); }
};

template<class F>
Defer(F) -> Defer<F>;

// Using FILE* is a bad idea, as that is buffered
class File
{
  constexpr static int FD_NONE = -1;

  int fd{FD_NONE};

public:
  File() = default;
  explicit File(int value) : fd{value} {};

  explicit operator int() const { return fd; }

  File(const File &) = delete;
  File &operator=(const File &) = delete;

  File(File &&other) : fd{std::exchange(other.fd, FD_NONE)} {}
  File &operator=(File &&other)
  {
    if (this != &other)
    {
      std::swap(fd, other.fd);
    }

    return *this;
  }

  bool valid() const { return fd != FD_NONE; }
  void release() { fd = FD_NONE; }

  ~File()
  {
    if (fd != FD_NONE)
    {
      close(fd);
    }
  }
};

inline File pick_addrinfo_and_create_socket(addrinfo* list, bool client)
{
  for (auto info = list; info != nullptr; info = info->ai_next)
  {
    File result{socket(info->ai_family, info->ai_socktype, info->ai_protocol)};
    if (!result.valid()) continue;
    
    if (client)
    {
      // we are a client

      // Send and receive packets "there" by default
      if (connect(int(result), info->ai_addr, info->ai_addrlen) == -1)
      {
        continue;
      }
      
      return result;
    }
    else
    {
      // we are a server

      // Receive packets from our chosen address
      if (bind(int(result), info->ai_addr, info->ai_addrlen) == -1)
      {
        continue;
      }

      // Send to "anywhere"
      return result;
    }
  }

  return File{};
}

inline File create_dgram_socket(
    const char *dst_addr,
    const char *dst_port)
{
  addrinfo hints{
      .ai_flags = dst_addr == nullptr ? AI_PASSIVE : 0,
      .ai_family = AF_INET,
      .ai_socktype = SOCK_DGRAM,
      .ai_protocol = IPPROTO_UDP,
      .ai_addrlen = 0,
      .ai_addr = nullptr,
      .ai_canonname = nullptr,
      .ai_next = nullptr,
  };

  addrinfo *list = nullptr;
  VERIFY(getaddrinfo(dst_addr, dst_port, &hints, &list) != -1);
  Defer freelist{[&](){ freeaddrinfo(list); }};
  
  File result = pick_addrinfo_and_create_socket(list, dst_addr != nullptr);
  VERIFY(result.valid());
  
  VERIFY(fcntl(int(result), F_SETFL, O_NONBLOCK) != -1);

  int trueVal = 1;
  setsockopt(int(result), SOL_SOCKET, SO_REUSEADDR, &trueVal, sizeof(int));

  return result;
}

template <int op>
[[nodiscard]] int epoll_ctl(const File &epoll, const File &file, uint32_t events)
{
  struct epoll_event ev{
    .events = events,
    .data = {
      .fd = int(file),
    },
  };
  return epoll_ctl(int(epoll), op, int(file), &ev);
}

File make_timer(time_t interval_sec)
{
  File timer{timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)};

  if (!timer.valid()) return timer;

  struct itimerspec spec{
    .it_interval = {
      .tv_sec = interval_sec,
      .tv_nsec = 0,
    },
    .it_value = {
      .tv_sec = interval_sec,
      .tv_nsec = 0,
    },
  };

  if (timerfd_settime(int(timer), 0, &spec, nullptr) == -1)
  {
    return {};
  }
  
  return timer;
}
