#pragma once

#include "ophion/platform/memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ophion::platform {

struct TransportResult {
    Error error{Error::Ok};
    SnapshotEpoch epoch{};
    std::uint64_t value{};
    std::uint32_t completed_bytes{};
};

class InProcessRingTransport {
public:
    explicit InProcessRingTransport(const PageTableWalker& walker) noexcept : walker_(walker) {}

    [[nodiscard]] SessionState attach(Nonce client_nonce, Capability requested) noexcept;
    void close() noexcept;
    [[nodiscard]] TransportResult status(const RecordHeader& header) noexcept;
    [[nodiscard]] TransportResult translate(const TranslateRequest& request) noexcept;
    [[nodiscard]] TransportResult scatter(const ScatterReadRequest& request, std::span<const ScatterDescriptor> descriptors,
                                          std::span<std::byte> output) noexcept;
    [[nodiscard]] TransportResult snapshot(const RecordHeader& header) noexcept;
    [[nodiscard]] std::size_t ring_depth() const noexcept { return count_; }

private:
    [[nodiscard]] Error accept(const RecordHeader& header, Command expected) noexcept;
    void record(Command command) noexcept;

    const PageTableWalker& walker_;
    SessionState session_{};
    std::array<Command, 64> commands_{};
    std::size_t head_{};
    std::size_t count_{};
};

using MockTransport = InProcessRingTransport;

} // namespace ophion::platform
