#include "MacroEngine.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace geode::prelude;

namespace hitborderless {

MacroEngine& MacroEngine::get() {
    static MacroEngine instance;
    return instance;
}

bool MacroEngine::startRecording() {
    if (m_recording) {
        return false;
    }

    m_events.clear();
    m_frame = 0;
    m_startedAt = std::chrono::steady_clock::now();
    m_recording = true;

    log::info("[Macro Engine] Recording started");
    return true;
}

void MacroEngine::recordInput(bool down, int button, bool player1) {
    if (!m_recording) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - m_startedAt
    ).count();

    m_events.push_back(MacroInputEvent{
        .frame = m_frame,
        .timestampUs = elapsed,
        .button = button,
        .down = down,
        .player1 = player1
    });
}

void MacroEngine::onFrame(float) {
    if (m_recording) {
        ++m_frame;
    }
}

std::string MacroEngine::stopAndSave() {
    if (!m_recording) {
        return {};
    }

    m_recording = false;

    const auto endedAt = std::chrono::steady_clock::now();
    const auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
        endedAt - m_startedAt
    ).count();

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
        filename << "macro_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".hbmacro";
        const auto outputPath = macroDir / filename.str();

        std::ofstream output(outputPath, std::ios::binary);
        if (!output.is_open()) {
            log::error("[Macro Engine] Could not open macro output file");
            return {};
        }

        output << "{\n"
               << "  \"format\": \"hit-borderless-macro\",\n"
               << "  \"schema_version\": 1,\n"
               << "  \"timebase\": \"steady_clock_microseconds\",\n"
               << "  \"frame_source\": \"PlayLayer::postUpdate\",\n"
               << "  \"frame_count\": " << m_frame << ",\n"
               << "  \"duration_us\": " << durationUs << ",\n"
               << "  \"input_count\": " << m_events.size() << ",\n"
               << "  \"inputs\": [";

        for (std::size_t i = 0; i < m_events.size(); ++i) {
            const auto& event = m_events[i];
            output << (i == 0 ? "\n" : ",\n")
                   << "    {"
                   << "\"frame\":" << event.frame << ","
                   << "\"timestamp_us\":" << event.timestampUs << ","
                   << "\"button\":" << event.button << ","
                   << "\"down\":" << (event.down ? "true" : "false") << ","
                   << "\"player1\":" << (event.player1 ? "true" : "false")
                   << "}";
        }

        if (!m_events.empty()) {
            output << "\n  ";
        }
        output << "]\n}\n";
        output.close();

        const auto result = outputPath.string();
        log::info("[Macro Engine] Saved {} inputs to {}", m_events.size(), result);
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

std::uint64_t MacroEngine::getCurrentFrame() const {
    return m_frame;
}

std::size_t MacroEngine::getInputCount() const {
    return m_events.size();
}

} // namespace hitborderless
