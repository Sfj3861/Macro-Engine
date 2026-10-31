#pragma once

#include <cstdint>

namespace macroengine::timing {

// Macro Engine's display clock is fixed at 240 reference frames per second.
// This is a time conversion, not a claim about the game's simulation FPS.
inline constexpr std::uint32_t kReferenceFps = 240;
inline constexpr std::uint64_t kMicrosecondsPerSecond = 1'000'000;

constexpr std::uint64_t normalizedFrame(std::int64_t timestampUs) {
    if (timestampUs <= 0) {
        return 0;
    }

    return (static_cast<std::uint64_t>(timestampUs) * kReferenceFps)
        / kMicrosecondsPerSecond;
}

constexpr double milliseconds(std::int64_t timestampUs) {
    return timestampUs <= 0 ? 0.0 : static_cast<double>(timestampUs) / 1000.0;
}

static_assert(normalizedFrame(0) == 0);
static_assert(normalizedFrame(500'000) == 120);
static_assert(normalizedFrame(1'000'000) == 240);

} // namespace macroengine::timing
