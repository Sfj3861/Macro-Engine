#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <utility>

namespace macroengine {

struct MacroInputEvent {
    std::uint64_t frame = 0;
    std::int64_t timestampUs = 0;
    int button = 0;
    bool down = false;
    bool player1 = true;
    float x = 0.f;
    float y = 0.f;
};

struct MacroPositionSample {
    std::uint64_t frame = 0;
    std::int64_t timestampUs = 0;
    float x = 0.f;
    float y = 0.f;
    bool player1 = true;
};

class MacroEngine {
public:
    static MacroEngine& get();

    bool startRecording();
    std::string stopAndSave();
    bool loadMacro(const std::string& path, std::string& error);
    bool loadLatestMacro(std::string& path, std::string& error);
    bool startPlayback();
    void updatePlayback(const std::function<void(bool, int, bool)>& dispatch);
    void pausePlayback();
    void resumePlayback();
    void stopPlayback(const std::function<void(bool, int, bool)>& dispatch = {});
    void recordInput(bool down, int button, bool player1, std::uint64_t gameFrame, float x, float y);
    void recordPlayerPosition(float x, float y, bool player1, std::uint64_t gameFrame);
    void onFrame(float dt);

    bool isRecording() const;
    bool isPlaying() const;
    bool hasLoadedMacro() const;
    std::size_t getLoadedInputCount() const;
    std::uint64_t getUpdateSteps() const;
    std::int64_t getElapsedMicroseconds() const;
    std::int64_t getLastInputIntervalMicroseconds() const;
    std::uint64_t getCurrentNormalizedFrame() const;
    std::size_t getInputCount() const;
    std::size_t getTrajectorySampleCount() const;
    const std::vector<MacroInputEvent>& getInputEvents() const;
    const std::vector<MacroPositionSample>& getTrajectory() const;

private:
    MacroEngine() = default;

    std::int64_t elapsedMicroseconds() const;

    bool m_recording = false;
    std::uint64_t m_updateSteps = 0;
    std::chrono::steady_clock::time_point m_startedAt{};
    bool m_playing = false;
    bool m_playbackClockStarted = false;
    bool m_playbackPaused = false;
    std::int64_t m_playbackPausedElapsedUs = 0;
    std::chrono::steady_clock::time_point m_playbackStartedAt{};
    std::vector<MacroInputEvent> m_loadedEvents;
    std::size_t m_playbackIndex = 0;
    std::vector<std::pair<int, bool>> m_heldButtons;
    std::int64_t m_previousInputTimestampUs = -1;
    std::int64_t m_lastInputIntervalUs = -1;
    std::vector<MacroInputEvent> m_events;
    std::vector<MacroPositionSample> m_positions;
};

} // namespace macroengine
