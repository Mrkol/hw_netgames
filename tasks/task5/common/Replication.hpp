#pragma once

#include <deque>
#include <span>

#include "Service.hpp"
#include "bytestream.hpp"
#include "tuple_hash.hpp"


template<class Derived>
class Replication
{
  struct ReplState
  {
    uint64_t sequence;
    std::vector<std::byte> data;
  };

  struct ReplicationData
  {
    std::vector<std::byte> remoteState;
    std::deque<ReplState> localStates;
  };

  Derived& self() { return *static_cast<Derived*>(this); }
  const Derived& self() const { return *static_cast<const Derived*>(this); }

  static std::vector<std::byte> delta(const ReplState& old, const ReplState& recent)
  {
    std::vector<bool> changed;
    std::vector<std::byte> del;
    for (size_t i = 0; i < recent.data.size(); ++i)
    {
      if (i < old.data.size() && old.data[i] == recent.data[i])
      {
        changed.push_back(false);
      }
      else
      {
        changed.push_back(true);
        del.push_back(recent.data[i]);
      }
    }

    ByteOstream s;
    s << changed << std::span{del.data(), del.size()};
    return std::move(s).finalize();
  }

  static std::vector<std::byte> apply(std::span<std::byte const> old, std::span<std::byte const> delta)
  {
    std::vector<bool> changed;
    std::vector<std::byte> del;
    {
      ByteIstream s(delta);
      s >> changed;
      s >> del;
    }

    std::vector<std::byte> result(old.begin(), old.end());
    result.resize(changed.size());

    size_t j = 0;
    for (size_t i = 0; i < changed.size(); ++i)
    {
      if (changed[i])
      {
        result[i] = del[j++];
      }
    }
    return result;
  }

public:
  void handlePacket(ENetPeer* peer, enet_uint8 chan, const PReplication& packet, std::span<std::byte> cont)
  {
    auto& replData = replication_.at(std::tuple{peer, chan});

    replData.remoteState = apply({replData.remoteState.data(), replData.remoteState.size()}, cont);
    self().send(peer, chan, {}, PReplicationAck{ .sequence = packet.sequence });

    self().handleReplication(peer, chan, std::span{replData.remoteState.data(), replData.remoteState.size()});
  }

  void handlePacket(ENetPeer* peer, enet_uint8 chan, const PReplicationAck& packet)
  {
    auto& replData = replication_.at(std::tuple{peer, chan});
    while (!replData.localStates.empty() && replData.localStates.back().sequence < packet.sequence)
    {
      replData.localStates.pop_back();
    }
  }

  void setupReplication(ENetPeer* peer, enet_uint8 channel)
  {
    replication_.emplace(std::make_tuple(peer, channel),
      ReplicationData{
        .localStates = {ReplState{ .sequence = 0 }}
      });
  }

  void stopReplication(ENetPeer* peer, enet_uint8 channel)
  {
    replication_.erase(std::make_tuple(peer, channel));
  }

  void replicate(ENetPeer* peer, enet_uint8 channel, std::span<std::byte> bytes)
  {
    auto& replData = replication_.at(std::tuple{peer, channel});

    auto& newState = replData.localStates.emplace_front(
      ReplState{
        .sequence = replData.localStates.front().sequence + 1,
        .data = std::vector(bytes.begin(), bytes.end()),
      });
    const auto& lastAckedState = replData.localStates.back();

    auto del = delta(lastAckedState, newState);
    self().send(peer, channel, {}, PReplication{ .sequence = newState.sequence }, {del.data(), del.size()});
  }

private:
  std::unordered_map<std::tuple<ENetPeer*, enet_uint8>, ReplicationData> replication_;
};

