#include "ReplayAnalyzer.hpp"

#include <Geode/Geode.hpp>
#include <gdr/gdr.hpp>
#include <gdr/physics_extension.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>

using namespace geode::prelude;
using json = nlohmann::json;

namespace macroengine {
namespace {

struct PhysicsInput : gdr::Input<"Phys"> {
    float xPosition = 0.f;
    float yPosition = 0.f;
    float rotation = 0.f;
    double xVelocity = 0.0;
    double yVelocity = 0.0;

    void parseExtension(binary_reader& reader) override {
        reader >> xPosition >> yPosition >> rotation >> xVelocity >> yVelocity;
    }
};

struct PhysicsReplay : gdr::Replay<PhysicsReplay, PhysicsInput> {
    PhysicsReplay() : Replay("Macro Engine", 1) {}
    bool shouldParseExtension() const override { return true; }
};

struct PlainReplay : gdr::Replay<PlainReplay, gdr::Input<>> {
    PlainReplay() : Replay("Macro Engine", 1) {}
};

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 128ull * 1024ull * 1024ull) {
        error = "The replay is missing, empty, or larger than 128 MB.";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open the selected replay.";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "Could not read the complete replay file.";
        return false;
    }
    return true;
}

bool readVarint(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& value) {
    value = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (offset >= bytes.size()) return false;
        const auto byte = bytes[offset++];
        if (i == 9 && (byte & 0xFE)) return false;
        value |= static_cast<std::uint64_t>(byte & 0x7F) << (i * 7);
        if ((byte & 0x80) == 0) return true;
    }
    return false;
}

bool hasPhysicsTag(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 5 || bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'R') return false;
    std::size_t offset = 3;
    std::uint64_t version = 0;
    if (!readVarint(bytes, offset, version)) return false;
    const auto end = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end(), 0);
    if (end == bytes.end()) return false;
    const std::string tag(bytes.begin() + static_cast<std::ptrdiff_t>(offset), end);
    return tag == "Phys";
}

template <class T>
T jsonValue(const json& object, const char* key, T fallback) {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    try {
        return object[key].get<T>();
    }
    catch (...) {
        return fallback;
    }
}

std::string firstString(const json& object, const char* first, const char* second = nullptr) {
    if (object.is_object() && object.contains(first) && object[first].is_string()) {
        return object[first].get<std::string>();
    }
    if (second && object.is_object() && object.contains(second) && object[second].is_string()) {
        return object[second].get<std::string>();
    }
    return {};
}

bool parseLegacy(const std::vector<std::uint8_t>& bytes, bool isJson, ReplayData& out, std::string& error) {
    json root;
    try {
        if (isJson) {
            root = json::parse(bytes.begin(), bytes.end());
        }
        else {
            root = json::from_msgpack(bytes);
        }
    }
    catch (const std::exception& e) {
        error = std::string("Could not decode legacy GDR data: ") + e.what();
        return false;
    }

    if (!root.is_object() || !root.contains("inputs") || !root["inputs"].is_array()) {
        error = "The legacy GDR file does not contain a valid inputs array.";
        return false;
    }
    if (root["inputs"].size() > 2'000'000) {
        error = "The replay contains too many input events (limit: 2,000,000).";
        return false;
    }

    out.sourceFormat = isJson ? "GDR JSON v1" : "GDR MessagePack v1";
    out.author = firstString(root, "author", "creator");
    out.framerate = jsonValue<double>(root, "framerate", 240.0);
    out.durationFrames = static_cast<std::uint64_t>(std::max(0.0,
        jsonValue<double>(root, "duration", 0.0) * (std::isfinite(out.framerate) && out.framerate > 0.0 ? out.framerate : 240.0)));
    out.bot = root.value("bot", json::object()).is_object()
        ? firstString(root.value("bot", json::object()), "name") : std::string{};
    const auto level = root.value("level", json::object());
    if (level.is_object()) {
        out.levelName = firstString(level, "name");
        out.levelID = jsonValue<std::uint32_t>(level, "id", 0);
    }

    if (!std::isfinite(out.framerate) || out.framerate <= 0.0 || out.framerate > 100000.0) {
        out.framerate = 240.0;
    }

    for (const auto& item : root["inputs"]) {
        if (!item.is_object()) continue;
        ReplayInput input;
        input.frame = jsonValue<std::uint64_t>(item, "frame", 0);
        input.button = static_cast<std::uint8_t>(std::clamp(jsonValue<int>(item, "btn", 1), 0, 255));
        input.player2 = jsonValue<bool>(item, "2p", false);
        input.down = jsonValue<bool>(item, "down", false);
        out.durationFrames = std::max(out.durationFrames, input.frame);
        out.inputs.push_back(input);

        const auto x = item.contains("xPosition") ? jsonValue<double>(item, "xPosition", 0.0)
            : item.contains("xpos") ? jsonValue<double>(item, "xpos", 0.0)
            : jsonValue<double>(item, "x", std::numeric_limits<double>::quiet_NaN());
        const auto y = item.contains("yPosition") ? jsonValue<double>(item, "yPosition", 0.0)
            : item.contains("ypos") ? jsonValue<double>(item, "ypos", 0.0)
            : jsonValue<double>(item, "y", std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(x) && std::isfinite(y)) {
            ReplayPosition position;
            position.frame = input.frame;
            position.x = static_cast<float>(x);
            position.y = static_cast<float>(y);
            position.player2 = input.player2;
            out.positions.push_back(position);
        }
    }

    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
    std::stable_sort(out.positions.begin(), out.positions.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
    return true;
}

bool parseGdr2(const std::vector<std::uint8_t>& bytes, ReplayData& out, std::string& error) {
    try {
        if (hasPhysicsTag(bytes)) {
            auto parsed = PhysicsReplay::importData(std::span<std::uint8_t>(
                const_cast<std::uint8_t*>(bytes.data()), bytes.size()));
            if (parsed.isErr()) {
                error = "GDR2 parser rejected the replay: " + parsed.unwrapErr();
                return false;
            }
            auto replay = std::move(parsed).unwrap();
            out.sourceFormat = "GDR2";
            out.author = replay.author;
            out.bot = replay.botInfo.name;
            out.levelName = replay.levelInfo.name;
            out.levelID = replay.levelInfo.id;
            out.framerate = replay.framerate;
            out.durationFrames = static_cast<std::uint64_t>(std::max(0.0, replay.duration * replay.framerate));
            out.platformer = replay.platformer;
            if (replay.inputs.size() > 2'000'000) {
                error = "The replay contains too many input events (limit: 2,000,000).";
                return false;
            }
            for (const auto& item : replay.inputs) {
                out.inputs.push_back({item.frame, item.button, item.player2, item.down});
                out.durationFrames = std::max(out.durationFrames, item.frame);
                if (std::isfinite(item.xPosition) && std::isfinite(item.yPosition)) {
                    out.positions.push_back({item.frame, item.xPosition, item.yPosition,
                        item.rotation, item.player2});
                }
            }
        }
        else {
            auto parsed = PlainReplay::importData(std::span<std::uint8_t>(
                const_cast<std::uint8_t*>(bytes.data()), bytes.size()));
            if (parsed.isErr()) {
                error = "GDR2 parser rejected the replay: " + parsed.unwrapErr();
                return false;
            }
            auto replay = std::move(parsed).unwrap();
            out.sourceFormat = "GDR2";
            out.author = replay.author;
            out.bot = replay.botInfo.name;
            out.levelName = replay.levelInfo.name;
            out.levelID = replay.levelInfo.id;
            out.framerate = replay.framerate;
            out.durationFrames = static_cast<std::uint64_t>(std::max(0.0, replay.duration * replay.framerate));
            out.platformer = replay.platformer;
            if (replay.inputs.size() > 2'000'000) {
                error = "The replay contains too many input events (limit: 2,000,000).";
                return false;
            }
            for (const auto& item : replay.inputs) {
                out.inputs.push_back({item.frame, item.button, item.player2, item.down});
                out.durationFrames = std::max(out.durationFrames, item.frame);
            }
        }
    }
    catch (const std::exception& e) {
        error = std::string("Could not decode GDR2 replay: ") + e.what();
        return false;
    }

    if (!std::isfinite(out.framerate) || out.framerate <= 0.0 || out.framerate > 100000.0) {
        out.framerate = 240.0;
    }
    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
    std::stable_sort(out.positions.begin(), out.positions.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
    return true;
}

std::string makeOutputName(AnalysisKind kind) {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::string(kind == AnalysisKind::FrameCounter ? "frame_counter_" : "hit_border_")
        + std::to_string(millis) + (kind == AnalysisKind::FrameCounter ? ".fcr" : ".hbr");
}

bool saveAnalysis(const ReplayData& replay, AnalysisKind kind, std::string& path, std::string& error) {
    try {
        const auto directory = std::filesystem::path(Mod::get()->getSaveDir().string()) / "analyses";
        std::filesystem::create_directories(directory);
        auto outputPath = directory / makeOutputName(kind);

        json root = {
            {"format", kind == AnalysisKind::FrameCounter ? "macro-engine-frame-counter" : "macro-engine-hit-border"},
            {"schema_version", 1},
            {"source_format", replay.sourceFormat},
            {"author", replay.author},
            {"bot", replay.bot},
            {"level_name", replay.levelName},
            {"level_id", replay.levelID},
            {"framerate", replay.framerate},
            {"duration_frames", replay.durationFrames},
            {"platformer", replay.platformer},
            {"input_count", replay.inputs.size()},
            {"trajectory_count", replay.positions.size()},
            {"trajectory_available", !replay.positions.empty()}
        };

        root["inputs"] = json::array();
        for (const auto& input : replay.inputs) {
            root["inputs"].push_back({
                {"frame", input.frame}, {"button", input.button},
                {"player", input.player2 ? "P2" : "P1"},
                {"action", input.down ? "press" : "release"}
            });
        }

        if (kind == AnalysisKind::HitBorder) {
            root["trajectory"] = json::array();
            for (const auto& point : replay.positions) {
                root["trajectory"].push_back({
                    {"frame", point.frame}, {"x", point.x}, {"y", point.y},
                    {"rotation", point.rotation}, {"player", point.player2 ? "P2" : "P1"}
                });
            }
        }

        const auto tempPath = std::filesystem::path(outputPath.string() + ".tmp");
        {
            std::ofstream file(tempPath, std::ios::binary);
            if (!file) {
                error = "Could not create the analysis output file.";
                return false;
            }
            file << root.dump(2);
            if (!file) {
                error = "Failed while writing the analysis file.";
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tempPath, outputPath, ec);
        if (ec) {
            std::filesystem::remove(tempPath);
            error = "Could not finalize the analysis file: " + ec.message();
            return false;
        }
        path = outputPath.string();
        return true;
    }
    catch (const std::exception& e) {
        error = std::string("Could not save analysis: ") + e.what();
        return false;
    }
}

} // namespace

ReplayAnalyzer& ReplayAnalyzer::get() {
    static ReplayAnalyzer instance;
    return instance;
}

bool ReplayAnalyzer::importReplay(const std::string& path, AnalysisKind kind,
                                  std::string& outputPath, std::string& message) {
    outputPath.clear();
    message.clear();

    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes, message)) return false;

    ReplayData replay;
    const auto extension = std::filesystem::path(path).extension().string();
    const auto filename = std::filesystem::path(path).filename().string();
    const bool legacyJson = filename.size() >= 9
        && filename.compare(filename.size() - 9, 9, ".gdr.json") == 0;

    bool ok = false;
    if (bytes.size() >= 3 && bytes[0] == 'G' && bytes[1] == 'D' && bytes[2] == 'R') {
        ok = parseGdr2(bytes, replay, message);
    }
    else if (legacyJson || extension == ".json") {
        ok = parseLegacy(bytes, true, replay, message);
    }
    else if (extension == ".gdr") {
        ok = parseLegacy(bytes, false, replay, message);
    }
    else {
        message = "Unsupported replay extension. Select .gdr, .gdr.json or .gdr2.";
        return false;
    }

    if (!ok) return false;
    if (replay.inputs.empty()) {
        message = "The replay was imported, but it contains no input events.";
        return false;
    }
    if (!saveAnalysis(replay, kind, outputPath, message)) return false;

    m_current = std::move(replay);
    m_currentKind = kind;
    m_hasCurrent = true;
    ++m_revision;

    std::ostringstream summary;
    summary << "Imported " << m_current.inputs.size() << " inputs from " << m_current.sourceFormat
            << ".\nDuration: " << m_current.durationFrames << " frames at "
            << m_current.framerate << " FPS.";
    if (kind == AnalysisKind::HitBorder) {
        summary << "\nTrajectory samples: " << m_current.positions.size() << ".";
        if (m_current.positions.empty()) {
            summary << "\nThis replay has no recognized position extension, so only its input timeline was saved.";
        }
    }
    summary << "\nSaved: " << outputPath;
    message = summary.str();
    return true;
}

const ReplayData& ReplayAnalyzer::getCurrentReplay() const {
    return m_current;
}

AnalysisKind ReplayAnalyzer::getCurrentKind() const {
    return m_currentKind;
}

bool ReplayAnalyzer::hasCurrentReplay() const {
    return m_hasCurrent;
}

std::uint64_t ReplayAnalyzer::getRevision() const {
    return m_revision;
}

void ReplayAnalyzer::clear() {
    m_current = {};
    m_hasCurrent = false;
}

} // namespace macroengine
