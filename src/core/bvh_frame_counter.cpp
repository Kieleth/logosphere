#include "core/bvh_frame_counter.h"

#include <atomic>

namespace logosphere {
namespace bvh_frame {

namespace {
std::atomic<std::uint64_t> g_counter{0};
}

void increment() {
    g_counter.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t read() {
    return g_counter.load(std::memory_order_relaxed);
}

}  // namespace bvh_frame
}  // namespace logosphere
