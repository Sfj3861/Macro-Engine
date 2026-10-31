#include "MacroEngine.hpp"
#include "MacroTiming.hpp"

#include <matjson.hpp>

#include <Geode/Geode.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <limits>

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
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count() % 1000;
        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        std::ostringstream filename;
        filename << "macro_" << std::put_time(&localTime, "%Y%m%d_%H%M%S")
                 << "_" << std::setw(3) << std::setfill('0') << milliseconds
                 << ".macroengine";
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
               << "  \"frame_counter\": {\"reference_fps\": " << timing::kReferenceFps
               << ", \"rounding\": \"floor\", \"source\": \"timestamp_us\", \"unit\": \"elapsed\"},\n"
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
                   << "\"normalized_frame_240\":" << timing::normalizedFrame(event.timestampUs) << ","
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
                   << "\"normalized_frame_240\":" << timing::normalizedFrame(sample.timestampUs) << ","
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


bool MacroEngine::loadMacro(const std::string& path, std::string& error) {
    error.clear();
    try {
        const std::filesystem::path macroPath(path);
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(macroPath, ec);
        if (ec || fileSize == 0 || fileSize > 64ull * 1024ull * 1024ull) {
            error = "The macro file is missing, empty, or larger than 64 MB.";
            return false;
        }

        std::ifstream input(macroPath, std::ios::binary);
        if (!input.is_open()) {
            error = "Could not open the selected macro file.";
            return false;
        }

        auto parsed = matjson::parse(input);
        if (!parsed) {
            error = "Invalid macro JSON: " + std::string(parsed.unwrapErr());
            return false;
        }

        const auto& root = parsed.unwrap();
        if (!root.isObject()) {
            error = "The macro root must be a JSON object.";
            return false;
        }

        const auto format = root["format"].asString();
        const auto version = root["schema_version"].asInt();
        if (!format || format.unwrap() != "macro-engine" || !version || version.unwrap() != 2) {
            error = "Unsupported macro format or schema version. Expected Macro Engine schema 2.";
            return false;
        }

        const auto& inputs = root["inputs"];
        if (!inputs.isArray() || inputs.size() > 1'000'000) {
            error = "The macro input list is missing or exceeds the safety limit.";
            return false;
        }

        std::vector<MacroInputEvent> loaded;
        loaded.reserve(inputs.size());
        std::int64_t previousTimestamp = -1;

        for (const auto& item : inputs) {
            if (!item.isObject()) {
                error = "An input event is not a JSON object.";
                return false;
            }

            const auto frame = item["frame"].asUInt();
            const auto timestamp = item["timestamp_us"].asInt();
            const auto button = item["button"].asInt();
            const auto action = item["action"].asString();
            const auto player = item["player"].asString();
            const auto x = item["x"].asDouble();
            const auto y = item["y"].asDouble();

            if (!frame || !timestamp || !button || !action || !player || !x || !y) {
                error = "An input event is missing a required field or has an invalid type.";
                return false;
            }

            const auto timestampUs = timestamp.unwrap();
            const auto buttonValue = button.unwrap();
            const auto actionValue = action.unwrap();
            const auto playerValue = player.unwrap();
            const auto xValue = x.unwrap();
            const auto yValue = y.unwrap();

            if (timestampUs < 0 || timestampUs < previousTimestamp
                || buttonValue < std::numeric_limits<int>::min()
                || buttonValue > std::numeric_limits<int>::max()
                || (actionValue != "press" && actionValue != "release")
                || (playerValue != "P1" && playerValue != "P2")
                || !std::isfinite(xValue) || !std::isfinite(yValue)) {
                error = "An input event contains invalid values or is out of chronological order.";
                return false;
            }

            loaded.push_back(MacroInputEvent{
                .frame = frame.unwrap(),
                .timestampUs = timestampUs,
                .button = static_cast<int>(buttonValue),
                .down = actionValue == "press",
                .player1 = playerValue == "P1",
                .x = static_cast<float>(xValue),
                .y = static_cast<float>(yValue)
            });
            previousTimestamp = timestampUs;
        }

        m_loadedEvents = std::move(loaded);
        m_playbackIndex = 0;
        m_heldButtons.clear();
        log::info("[Macro Engine] Loaded macro with {} input events from {}", m_loadedEvents.size(), path);
        return true;
    }
    catch (const std::exception& exception) {
        error = std::string("Could not load macro: ") + exception.what();
        return false;
    }
}

bool MacroEngine::loadLatestMacro(std::string& path, std::string& error) {
    error.clear();
    try {
        const auto macroDir = std::filesystem::path(Mod::get()->getSaveDir().string()) / "macros";
        if (!std::filesystem::exists(macroDir)) {
            error = "No macros folder exists yet. Record a macro first.";
            return false;
        }

        std::filesystem::path latest;
        std::filesystem::file_time_type latestTime{};
        for (const auto& entry : std::filesystem::directory_iterator(macroDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".macroengine") {
                continue;
            }
            const auto writeTime = entry.last_write_time();
            if (latest.empty() || writeTime > latestTime) {
                latest = entry.path();
                latestTime = writeTime;
            }
        }

        if (latest.empty()) {
            error = "No saved .macroengine files were found.";
            return false;
        }

        path = latest.string();
        return loadMacro(path, error);
    }
    catch (const std::exception& exception) {
        error = std::string("Could not search the macro library: ") + exception.what();
        return false;
    }
}

bool MacroEngine::startPlayback() {
    if (m_recording || m_playing || m_loadedEvents.empty()) {
        return false;
    }
    m_playbackIndex = 0;
    m_heldButtons.clear();
    m_playbackStartedAt = std::chrono::steady_clock::now();
    m_playing = true;
    log::info("[Macro Engine] Playback started ({} events)", m_loadedEvents.size());
    return true;
}

void MacroEngine::updatePlayback(const std::function<void(bool, int, bool)>& dispatch) {
    if (!m_playing || !dispatch) {
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - m_playbackStartedAt
    ).count();

    while (m_playbackIndex < m_loadedEvents.size()
        && m_loadedEvents[m_playbackIndex].timestampUs <= elapsed) {
        const auto& event = m_loadedEvents[m_playbackIndex++];
        dispatch(event.down, event.button, event.player1);

        const auto key = std::make_pair(event.button, event.player1);
        const auto held = std::find(m_heldButtons.begin(), m_heldButtons.end(), key);
        if (event.down) {
            if (held == m_heldButtons.end()) {
                m_heldButtons.push_back(key);
            }
        }
        else if (held != m_heldButtons.end()) {
            m_heldButtons.erase(held);
        }
    }

    if (m_playbackIndex >= m_loadedEvents.size()) {
        stopPlayback(dispatch);
        log::info("[Macro Engine] Playback completed");
    }
}

void MacroEngine::stopPlayback(const std::function<void(bool, int, bool)>& dispatch) {
    const auto held = m_heldButtons;
    m_heldButtons.clear();
    m_playing = false;
    for (const auto& [button, player1] : held) {
        if (dispatch) {
            dispatch(false, button, player1);
        }
    }
}

bool MacroEngine::isRecording() const {
    return m_recording;
}

bool MacroEngine::isPlaying() const {
    return m_playing;
}

bool MacroEngine::hasLoadedMacro() const {
    return !m_loadedEvents.empty();
}

std::size_t MacroEngine::getLoadedInputCount() const {
    return m_loadedEvents.size();
}

std::uint64_t MacroEngine::getUpdateSteps() const {
    return m_updateSteps;
}

std::int64_t MacroEngine::getElapsedMicroseconds() const {
    if (m_recording) {
        return elapsedMicroseconds();
    }
    if (m_playing) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_playbackStartedAt
        ).count();
    }
    return 0;
}

std::uint64_t MacroEngine::getCurrentNormalizedFrame() const {
    return timing::normalizedFrame(getElapsedMicroseconds());
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
