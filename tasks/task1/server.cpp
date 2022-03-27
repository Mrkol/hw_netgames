#include <iostream>
#include <cstdio>
#include <array>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <string>
#include <chrono>
#include <deque>

extern "C"
{
#include <sys/epoll.h>
#include <sys/timerfd.h>
}

#include "common.hpp"
#include "proto.hpp"

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Client
{
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  std::deque<std::array<char, MAX_PACKET_SIZE>> outbound;
  Clock::time_point last_heartbeat;
};

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cout << "Usage: " << argv[0] << " <listen port>" << std::endl;
    return -1;
  }

  auto listener = create_dgram_socket(nullptr, argv[1]);
  VERIFY(listener.valid());


  // Timeout people every 3 seconds
  auto timer = make_timer(3);
  VERIFY(timer.valid());

  auto epoll = File{epoll_create1(0)};
  VERIFY(epoll.valid());

  VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, listener, EPOLLIN) != -1);
  VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, timer, EPOLLIN) != -1);

  std::unordered_map<std::string, Client> alive_clients;
  bool any_outbound_messages = false;

  std::array<struct epoll_event, 16> events;
  for (;;)
  {
    auto ev_count = epoll_wait(int(epoll), events.data(), events.size(), -1);

    VERIFY(ev_count != -1);

    for (int i = 0; i < ev_count; ++i)
    {
      auto &event = events[i];

      if (event.data.fd == int(timer))
      {
        uint64_t count;
        VERIFY(read(int(timer), &count, sizeof(count)) != -1);

        auto time = Clock::now();
        for (auto it = alive_clients.begin(); it != alive_clients.end();)
        {
          if (time - it->second.last_heartbeat > 3s)
          {
            std::cout << it->first << " timed out!" << std::endl;
            it = alive_clients.erase(it);
          }
          else
          {
            ++it;
          }
        }
      }
      else if (event.events & EPOLLIN)
      {
        // packet from clinet
        struct sockaddr_storage addr;
        socklen_t len = sizeof(addr);
        std::array<char, MAX_PACKET_SIZE> buf;
        ssize_t recvd = recvfrom(int(listener), buf.data(), buf.size(), 0,
          reinterpret_cast<struct sockaddr*>(&addr), &len);
        VERIFY(recvd > 0);

        auto* packet = reinterpret_cast<Packet*>(buf.data());

        std::string identifier;
        {
          std::array<char, NI_MAXHOST> host;
          std::array<char, NI_MAXSERV> serv;
          auto s = getnameinfo(reinterpret_cast<struct sockaddr*>(&addr), len,
            host.data(), host.size(), serv.data(), serv.size(), 0);
          VERIFY(s != -1);

          identifier += host.data();
          identifier += ":";
          identifier += serv.data();
        }

        if (packet->header.type == PacketType::REGISTER)
        {
          std::cout << identifier << " register." << std::endl;

          if (!alive_clients.contains(identifier))
          {
            alive_clients.emplace(identifier,
              Client{
                .addr = addr,
                .len = len,
                .outbound = {},
                .last_heartbeat = Clock::now(),
              });
          }
          continue;
        }
      
        if (!alive_clients.contains(identifier))
        {
          std::cout << identifier << " -- unknown identifier!";
          continue;
        }
        
        if (packet->header.type == PacketType::HEARTBEAT)
        {
          alive_clients.at(identifier).last_heartbeat = Clock::now();
        }
        else if (packet->header.type == PacketType::CHAT)
        {
          std::string msg(reinterpret_cast<char*>(packet->data),
            packet->header.size - sizeof(Header));

          std::cout << identifier << " says: " << msg << std::endl;

          for (auto&[id, client] : alive_clients)
          {
            if (identifier == id) continue;
            
            client.outbound.emplace_back(buf);
          }
        }

        if (!std::exchange(any_outbound_messages, true))
        {
          VERIFY(epoll_ctl<EPOLL_CTL_MOD>(
            epoll, listener, EPOLLIN | EPOLLOUT) != -1);
        }
      }
      else if (event.events & EPOLLOUT)
      {
        any_outbound_messages = false;
        for (auto&[_, client] : alive_clients)
        {
          while (!client.outbound.empty())
          {
            auto& buf = client.outbound.front();
            Packet* packet = reinterpret_cast<Packet*>(buf.data());
            if (sendto(int(listener), buf.data(), packet->header.size, 0,
              reinterpret_cast<struct sockaddr*>(&client.addr), client.len)
                != packet->header.size)
            {
              any_outbound_messages = true;
              break;
            }
            client.outbound.pop_front();
          }

          if (any_outbound_messages) break;
        }

        if (!any_outbound_messages)
        {
          VERIFY(epoll_ctl<EPOLL_CTL_MOD>(
            epoll, listener, EPOLLIN) != -1);
        }
      }
    }
  }

  return 0;
}
