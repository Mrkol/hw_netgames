#include <iostream>
#include <cstring>
#include <array>
#include <vector>
#include <deque>

extern "C"
{

#include <sys/epoll.h>
#include <sys/timerfd.h>

}

#include "common.hpp"
#include "proto.hpp"


int main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::cout << "Usage: " << argv[0] << " <server> <port>" << std::endl;
    return -1;
  }

  File cstdin{0};
  File cstdout{1};
  Defer release_stdinout{[&]() {
      cstdin.release();
      cstdout.release();
    }};

  File server = create_dgram_socket(argv[1], argv[2]);
  {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getsockname(int(server),
      reinterpret_cast<struct sockaddr*>(&addr), &len);
    // no truncation pls
    VERIFY(len <= sizeof(addr));
    std::cout << "Port: " << ntohs(addr.sin_port) << std::endl;
  }

  // Heartbeat every second
  auto timer = make_timer(1);
  VERIFY(timer.valid());

  auto epoll = File{epoll_create1(0)};
  VERIFY(epoll.valid());

  VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, timer, EPOLLIN) != -1);
  VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, server, EPOLLIN) != -1);
  VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, cstdin, EPOLLIN) != -1);
  

  std::deque<std::string> incoming_messages;
  std::deque<std::array<std::byte, MAX_PACKET_SIZE>> outgoing_packets;

  auto schedule_send = [&]()
    {
      if (!outgoing_packets.empty()) return;
      VERIFY(epoll_ctl<EPOLL_CTL_MOD>(epoll, server, EPOLLIN | EPOLLOUT) != -1);
    };

  {
    schedule_send();

    Packet* packet = reinterpret_cast<Packet*>(outgoing_packets.emplace_back().data());
    packet->header = {
      .type = PacketType::REGISTER,
      .size = sizeof(Header),
    };
  }

  std::array<struct epoll_event, 16> events;
  for (;;)
  {
    auto ev_count = epoll_wait(int(epoll), events.data(), events.size(), -1);

    for (int i = 0; i < ev_count; ++i)
    {
      auto& event = events[i];

      if (event.data.fd == int(timer))
      {
        uint64_t count;
        read(int(timer), &count, sizeof(count));
        
        schedule_send();

        Packet* packet = reinterpret_cast<Packet*>(outgoing_packets.emplace_back().data());
        packet->header = {
          .type = PacketType::HEARTBEAT,
          .size = sizeof(Header),
        };
      }
      else if (event.data.fd == 0)
      {
        // Data came from stdin
        std::array<std::byte, MAX_PACKET_SIZE> buffer;
        const size_t maxsize = MAX_PACKET_SIZE - sizeof(Header);
        ssize_t readed = read(int(cstdin), buffer.data() + sizeof(Header), maxsize);
        auto packet = reinterpret_cast<Packet*>(buffer.data());
        packet->header.size = readed + sizeof(Header);
        packet->header.type = PacketType::CHAT;

        schedule_send();
        outgoing_packets.push_back(std::move(buffer));
      }
      else if (event.data.fd == 1)
      {
        // We are ready to write to stdout

        while (!incoming_messages.empty())
        {
          auto& str = incoming_messages.front();
          auto written = write(int(cstdout), str.data(), str.size());
          VERIFY(written >= 0);
          if (static_cast<size_t>(written) < str.size())
          {
            str = str.substr(written);
            break;
          }
          incoming_messages.pop_front();
        }

        if (incoming_messages.empty())
        {
          VERIFY(epoll_ctl<EPOLL_CTL_DEL>(epoll, cstdout, {}) != -1);
        }
      }
      else if (event.events & EPOLLOUT)
      {
        // We are ready to send packets


        while (!outgoing_packets.empty())
        {
          auto& raw = outgoing_packets.front();
          auto* packet = reinterpret_cast<Packet*>(raw.data());
          auto res =
            send(int(server), raw.data(), packet->header.size, 0);

          if (res != packet->header.size) break;

          outgoing_packets.pop_front();
        }

        if (outgoing_packets.empty())
        {
          VERIFY(epoll_ctl<EPOLL_CTL_MOD>(epoll, server, EPOLLIN) != -1);
        }
      }
      else if (event.events & EPOLLIN)
      {
        // Packets were received
        std::array<std::byte, MAX_PACKET_SIZE> buffer;
        ssize_t packet_size =
          recv(int(server), buffer.data(), buffer.size(), 0);
        VERIFY(packet_size > 0);
        packet_size -= sizeof(Header);
        Packet* packet = reinterpret_cast<Packet*>(buffer.data());
        
        if (packet->header.type != PacketType::CHAT)
          continue;

        if (incoming_messages.empty())
        {
          VERIFY(epoll_ctl<EPOLL_CTL_ADD>(epoll, cstdout, EPOLLOUT) != -1);
        }

        incoming_messages.emplace_back(
          reinterpret_cast<char*>(packet->data), packet_size);
      }
    }
  }


  return 0;
}
