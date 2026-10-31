#include "MacroEngine.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace geode::prelude;

namespace macroengine {

MacroEngine& MacroEngine::get() {
    static MacroEngine instance;
    return instance;
}

std::int64_t MacroEngine::elapsedMicroseconds() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - m_startedAt
    ).count();
}

bool MacroEngine::startRecording() {
    if (m_recording) {
        return false;
    }

    m_events.clear();
    m_positions.clear();
    m_updateSteps = 0;
    m_startedAt = std::chrono::steady_clock::now();
    m_recording = true;

    log::info("[Macro Engine] Recording started");
    return true;
}

void MacroEngine::recordInput(bool down, int button, bool player1, std::uint64_t gameFrame, float x, float y) {
    if (!m_recording) {
        return;
    }

    m_events.push_back(MacroInputEvent{
        .frame = gameFrame,
        .timestampUs = elapsedMicroseconds(),
        .button = button,
        .down = down,
        .player1 = player1,
        .x = x,
        .y = y
    });
}

void MacroEngine::recordPlayerPosition(float x, float y, bool player1, std::uint64_t gameFrame) {
    if (!m_recording) {
        return;
    }

    m_positions.push_back(MacroPositionSample{
        .frame = gameFrame,
        .timestampUs = elapsedMicroseconds(),
        .x = x,
        .y = y,
        .player1 = player1
    });
}

void MacroEngine::onFrame(float) {
    if (m_recording) {
        ++m_updateSteps;
    }
}

std::string MacroEngine::stopAndSave() {
    if (!m_recording) {
        return {};
    }

    m_recording = false;
    const auto durationUs = elapsedMicroseconds();

    try {
        const auto saveDir = std::filesystem::path(Mod::get()->getSaveDir().string());
        const auto macroDir = saveDir / "macros";
        std::filesystem::create_directories(macroDir);

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        std::ostringstream filename;
        filename << "macro_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".macroengine";
        const auto outputPath = macroDir / filename.str();

        std::ofstream output(outputPath, std::ios::binary);
        if (!output.is_open()) {
            log::error("[Macro Engine] Could not open macro output file");
            return {};
        }

        output << "{\n"
               << "  \"format\": \"macro-engine\",\n"
               << "  \"schema_version\": 2,\n"
               << "  \"timebase\": \"steady_clock_microseconds\",\n"
               << "  \"frame_source\": \"GJGameState::m_commandIndex\",\n"
               << "  \"update_steps\": " << m_updateSteps << ",\n"
               << "  \"duration_us\": " << durationUs << ",\n"
               << "  \"input_count\": " << m_events.size() << ",\n"
               << "  \"trajectory_sample_count\": " << m_positions.size() << ",\n"
               << "  \"inputs\": [";

        for (std::size_t i = 0; i < m_events.size(); ++i) {
            const auto& event = m_events[i];
            output << (i == 0 ? "\n" : ",\n")
                   << "    {"
                   << "\"frame\":" << event.frame << ","
                   << "\"timestamp_us\":" << event.timestampUs << ","
                   << "\"button\":" << event.button << ","
                   << "\"action\":\"" << (event.down ? "press" : "release") << "\","
                   << "\"player\":\"" << (event.player1 ? "P1" : "P2") << "\","
                   << "\"x\":" << std::setprecision(9) << event.x << ","
                   << "\"y\":" << std::setprecision(9) << event.y
                   << "}";
        }

        if (!m_events.empty()) {
            output << "\n  ";
        }
        output << "],\n  \"trajectory\": [";

        for (std::size_t i = 0; i < m_positions.size(); ++i) {
            const auto& sample = m_positions[i];
            output << (i == 0 ? "\n" : ",\n")
                   << "    {"
                   << "\"frame\":" << sample.frame << ","
                   << "\"timestamp_us\":" << sample.timestampUs << ","
                   << "\"x\":" << std::setprecision(9) << sample.x << ","
                   << "\"y\":" << std::setprecision(9) << sample.y << ","
                   << "\"player\":\"" << (sample.player1 ? "P1" : "P2") << "\""
                   << "}";
        }

        if (!m_positions.empty()) {
            output << "\n  ";
        }
        output << "]\n}\n";
        output.close();

        if (!output) {
            log::error("[Macro Engine] Failed while writing macro file");
            return {};
        }

        const auto result = outputPath.string();
        log::info(
            "[Macro Engine] Saved {} inputs and {} trajectory samples to {}",
            m_events.size(), m_positions.size(), result
        );
        return result;
    }
    catch (const std::exception& exception) {
        log::error("[Macro Engine] Save failed: {}", exception.what());
        return {};
    }
}

bool MacroEngine::isRecording() const {
    return m_recording;
}

std::uint64_t MacroEngine::getUpdateSteps() const {
    return m_updateSteps;
}

std::size_t MacroEngine::getInputCount() const {
    return m_events.size();
}

std::size_t MacroEngine::getTrajectorySampleCount() const {
    return m_positions.size();
}

const std::vector<MacroInputEvent>& MacroEngine::getInputEvents() const {
    return m_events;
}

const std::vector<MacroPositionSample>& MacroEngine::getTrajectory() const {
    return m_positions;
}

} // namespace macroengine
