#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace hitborderless {

struct MacroInputEvent {
    std::uint64_t frame = 0;
    std::int64_t timestampUs = 0;
    int button = 0;
    bool down = false;
    bool player1 = true;
};

class MacroEngine {
public:
    static MacroEngine& get();

    bool startRecording();
    std::string stopAndSave();
    void recordInput(bool down, int button, bool player1, std::uint64_t gameFrame);
    void onFrame(float dt);

    bool isRecording() const;
    std::uint64_t getUpdateSteps() const;
    std::size_t getInputCount() const;

private:
    MacroEngine() = default;

    bool m_recording = false;
    std::uint64_t m_updateSteps = 0;
    std::chrono::steady_clock::time_point m_startedAt{};
    std::vector<MacroInputEvent> m_events;
};

} // namespace hitborderless
