#pragma once

#include "engine/net/transport.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::net {

struct TransportPacketCodecConfig {
    std::uint32_t max_payload_bytes = 64u * 1024u;
};

struct TransportPacketFragmentCodecConfig {
    std::uint32_t max_fragment_payload_bytes = 1200;
    std::uint32_t max_packet_bytes = 1024u * 1024u;
    std::uint32_t max_fragment_count = 1024;
    std::uint32_t max_pending_packets = 64;
    std::uint64_t max_pending_packet_bytes = 16u * 1024u * 1024u;
};

struct TransportPacketFragment {
    std::uint64_t packet_id = 0;
    std::uint32_t fragment_index = 0;
    std::uint32_t fragment_count = 0;
    std::uint64_t total_packet_bytes = 0;
    std::string payload;
};

struct TransportPacketReassemblyResult {
    bool complete = false;
    std::uint64_t packet_id = 0;
    std::uint32_t received_fragment_count = 0;
    std::uint32_t expected_fragment_count = 0;
    std::string packet;
};

class TransportPacketCodec {
  public:
    [[nodiscard]] static std::string encode(const TransportEnvelope& envelope);
    [[nodiscard]] static core::Result<TransportEnvelope>
    decode(std::string_view packet, TransportPacketCodecConfig config = {});
};

class TransportPacketFragmentCodec {
  public:
    [[nodiscard]] static core::Result<std::vector<TransportPacketFragment>>
    fragment_packet(std::string_view packet, std::uint64_t packet_id,
                    TransportPacketFragmentCodecConfig config = {});
    [[nodiscard]] static std::string encode(const TransportPacketFragment& fragment);
    [[nodiscard]] static core::Result<TransportPacketFragment>
    decode(std::string_view fragment_packet, TransportPacketFragmentCodecConfig config = {});
    [[nodiscard]] static core::Status
    validate_config(const TransportPacketFragmentCodecConfig& config);
    [[nodiscard]] static core::Status
    validate_fragment(const TransportPacketFragment& fragment,
                      TransportPacketFragmentCodecConfig config = {});
};

class TransportPacketReassembler final {
  public:
    explicit TransportPacketReassembler(TransportPacketFragmentCodecConfig config = {});

    [[nodiscard]] core::Result<TransportPacketReassemblyResult>
    accept_fragment(TransportPacketFragment fragment);
    void discard(std::uint64_t packet_id);
    void clear();

  private:
    struct PendingPacket {
        std::uint64_t total_packet_bytes = 0;
        std::uint32_t fragment_count = 0;
        std::uint32_t received_fragment_count = 0;
        std::vector<std::string> fragments;
        std::vector<bool> received;
    };

    TransportPacketFragmentCodecConfig config_;
    std::unordered_map<std::uint64_t, PendingPacket> pending_;
    std::uint64_t pending_packet_bytes_ = 0;
};

} // namespace heartstead::net
