#include "ophion/platform/adapters.hpp"
#include "ophion/platform/internal.hpp"
#include "ophion/platform/memory.hpp"
#include "ophion/platform/overlay.hpp"
#include "ophion/platform/protocol.hpp"
#include "ophion/platform/transport.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

using namespace ophion::platform;

namespace {
std::vector<std::byte> page(std::initializer_list<std::pair<std::size_t, std::uint64_t>> entries) {
    std::vector<std::byte> bytes(0x1000);
    for (const auto& [index, value] : entries) std::memcpy(bytes.data() + index * sizeof(value), &value, sizeof(value));
    return bytes;
}

void test_protocol_layout_and_bounds() {
    static_assert(sizeof(RecordHeader) == 28);
    const ScatterDescriptor ranges[]{{0x1000, 4, 0}, {0x1004, 4, 4}};
    assert(validate_scatter(ranges).error == Error::Ok);
    const ScatterDescriptor bad[]{{0x1000, 0, 0}};
    assert(validate_scatter(bad).error == Error::LimitExceeded);
}

void test_four_level_large_page() {
    MockMemoryReader memory;
    constexpr std::uint64_t va = 0x12345000ULL;
    memory.map(0x1000, page({{0, 0x2001}}));
    memory.map(0x2000, page({{0, 0x3001}}));
    memory.map(0x3000, page({{(va >> 21) & 0x1ff, 0x40000000ULL | 0x81}}));
    PageTableWalker walker(memory, {4, {{0x1000, 0x3000}, {0x40000000ULL, 0x200000}}});
    const Translation translated = walker.translate(0x1000, va);
    assert(translated.error == Error::Ok);
    assert(translated.physical_address == 0x40000000ULL + (va & 0x1fffffULL));
    assert(translated.page_bytes == 0x200000);
}

void test_five_level_walk() {
    MockMemoryReader memory;
    constexpr std::uint64_t va = 0x0001000000001000ULL;
    memory.map(0x1000, page({{1, 0x2001}}));
    memory.map(0x2000, page({{0, 0x3001}}));
    memory.map(0x3000, page({{0, 0x4001}}));
    memory.map(0x4000, page({{0, 0x5001}}));
    memory.map(0x5000, page({{1, 0x7001}}));
    PageTableWalker walker(memory, {5, {{0x1000, 0x7000}}});
    const Translation translated = walker.translate(0x1000, va);
    assert(translated.error == Error::Ok && translated.physical_address == 0x7000);
}

void test_scatter_coalescing() {
    const ScatterPlanEntry entries[]{{0x1000, 4, 0}, {0x1004, 4, 4}, {0x2000, 4, 8}};
    const ScatterPlan plan = validate_and_coalesce(entries);
    assert(plan.error == Error::Ok && plan.total_bytes == 12 && plan.reads.size() == 2);
    const ScatterPlanEntry overlap[]{{0x1000, 4, 0}, {0x1002, 4, 4}};
    assert(validate_and_coalesce(overlap).error == Error::MalformedRecord);
}

void test_process_lifecycle() {
    ProcessTracker tracker;
    [[maybe_unused]] const auto first = tracker.reconcile({{42, 0x1000, 0, 0, "mock.exe", false}});
    assert(tracker.find(42)->generation == 1);
    [[maybe_unused]] const auto changed = tracker.reconcile({{42, 0x2000, 0, 0, "mock.exe", false}});
    assert(tracker.find(42)->generation == 2);
    [[maybe_unused]] const auto exited = tracker.reconcile({});
    assert(tracker.find(42)->exited);
}

void test_fingerprint_gate() {
    ImageFingerprint expected{};
    expected.time_date_stamp = 1;
    expected.image_size = 4;
    expected.digest[0] = std::byte{1};
    OffsetProfile profile(expected, {{"world", 16}});
    ImageFingerprint actual = expected;
    actual.digest[0] = std::byte{2};
    assert(!profile.validate(actual).has_value());
    assert(profile.validate(expected).has_value());
}

void test_nonce_handshake() {
    MockMemoryReader memory;
    PageTableWalker walker(memory, {4, {}});
    MockTransport transport(walker);
    const SessionState session = transport.attach(7, Capability::Status | Capability::Snapshot);
    RecordHeader status{};
    status.command = Command::Status;
    status.record_bytes = sizeof(status);
    status.nonce = session.nonce + 1;
    assert(transport.status(status).error == Error::NonceMismatch);
    status.nonce = session.nonce;
    assert(transport.status(status).error == Error::Ok);
}

class StubProjector final : public WorldProjector {
public:
    [[nodiscard]] bool project(const Vec3& world, ScreenPoint& screen) const override {
        if (world.z < 0.0F) return false;
        screen = {world.x, world.y};
        return true;
    }
};

void test_overlay_model_and_image_rejection() {
    GameSnapshot snapshot{};
    snapshot.entities.push_back({42, {{50.0F, 50.0F, 1.0F}, {}, {}}, {}});
    snapshot.entities.push_back({99, {{50.0F, 50.0F, -1.0F}, {}, {}}, {}});
    const StubProjector projector;
    const ExternalOverlayModel overlay;
    const auto commands = overlay.build(snapshot, projector, {100.0F, 100.0F});
    assert(commands.size() == 2 && commands[0].kind == DrawKind::Box && commands[1].text == "42");
    ImageExecutionPlan plan{};
    const std::array<std::byte, 64> invalid{};
    assert(PeImagePlanner::build(invalid, plan) == Error::MalformedRecord);
}
} // namespace

int main() {
    test_protocol_layout_and_bounds();
    test_four_level_large_page();
    test_five_level_walk();
    test_scatter_coalescing();
    test_process_lifecycle();
    test_fingerprint_gate();
    test_nonce_handshake();
    test_overlay_model_and_image_rejection();
}
