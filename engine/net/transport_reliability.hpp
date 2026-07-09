#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/net/transport.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace heartstead::net {

inline constexpr std::string_view transport_reliability_ack_payload_type = "control.transport_ack";

struct TransportReliableMessageKey {
    core::NetId sender;
    core::NetId recipient;
    TransportMessageKind kind = TransportMessageKind::command;
    std::uint64_t sequence = 0;
    std::string payload_type;
};

struct TransportReliablePendingMessage {
    TransportEnvelope envelope;
    std::uint32_t attempt_count = 0;
    std::int64_t first_sent_ms = 0;
    std::int64_t last_sent_ms = 0;
};

struct TransportReliabilityPollResult {
    std::vector<TransportEnvelope> retransmissions;
    std::vector<TransportReliablePendingMessage> dropped;
};

struct TransportReliableReceiveResult {
    bool accepted = false;
    bool duplicate = false;
    TransportEnvelope acknowledgement;
};

struct TransportReliableAckResult {
    bool matched_pending = false;
    TransportReliableMessageKey message;
};

class TransportReliabilityAckTextCodec {
  public:
    [[nodiscard]] static std::string encode(const TransportReliableMessageKey& message);
    [[nodiscard]] static core::Result<TransportReliableMessageKey> decode(std::string_view text);
};

class TransportReliabilityTracker final {
  public:
    TransportReliabilityTracker(core::NetId local_id, core::NetId remote_id,
                                TransportReliabilityConfig config = {});

    [[nodiscard]] core::NetId local_id() const noexcept;
    [[nodiscard]] core::NetId remote_id() const noexcept;
    [[nodiscard]] const TransportReliabilityConfig& config() const noexcept;
    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] std::size_t tracked_received_count() const noexcept;

    [[nodiscard]] core::Status track_send(TransportEnvelope envelope, std::int64_t now_ms);
    [[nodiscard]] core::Result<TransportReliableReceiveResult>
    accept_reliable_message(const TransportEnvelope& envelope, std::int64_t now_ms);
    [[nodiscard]] core::Result<TransportReliableAckResult>
    accept_acknowledgement(const TransportEnvelope& envelope);
    [[nodiscard]] TransportReliabilityPollResult poll(std::int64_t now_ms);
    void clear();

  private:
    [[nodiscard]] core::Status validate_endpoint_ids() const;
    [[nodiscard]] std::string key_id(const TransportReliableMessageKey& key) const;
    void remember_received(std::string key);

    core::NetId local_id_;
    core::NetId remote_id_;
    TransportReliabilityConfig config_;
    std::unordered_map<std::string, TransportReliablePendingMessage> pending_;
    std::unordered_set<std::string> received_;
    std::vector<std::string> received_order_;
};

[[nodiscard]] core::Status
validate_transport_reliability_config(const TransportReliabilityConfig& config) noexcept;
[[nodiscard]] core::Status
validate_transport_reliable_message_key(const TransportReliableMessageKey& key) noexcept;
[[nodiscard]] core::Result<TransportReliableMessageKey>
transport_reliable_message_key_from_envelope(const TransportEnvelope& envelope);
[[nodiscard]] TransportMessage
make_transport_reliability_ack_message(const TransportReliableMessageKey& key,
                                       std::int64_t timestamp_ms);
[[nodiscard]] core::Result<TransportEnvelope>
make_transport_reliability_ack_envelope(const TransportEnvelope& reliable_envelope,
                                        std::int64_t timestamp_ms);
[[nodiscard]] std::string
transport_reliable_message_key_name(const TransportReliableMessageKey& key);

} // namespace heartstead::net
