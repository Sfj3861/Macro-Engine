#include "ReplayAnalyzer.hpp"

#include <Geode/Geode.hpp>
#include <gdr/gdr.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

using namespace geode::prelude;

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

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                }
                else out << static_cast<char>(c);
        }
    }
    return out.str();
}

class MsgPackJsonReader {
public:
    explicit MsgPackJsonReader(const std::vector<std::uint8_t>& data) : m_data(data) {}

    bool convert(std::string& output, std::string& error) {
        if (!readValue(output, 0)) {
            error = "The .gdr file contains invalid or unsupported MessagePack data.";
            return false;
        }
        if (m_offset != m_data.size()) {
            error = "The .gdr file contains unexpected trailing bytes.";
            return false;
        }
        return true;
    }

private:
    bool readBytes(std::size_t count, const std::uint8_t*& ptr) {
        if (count > m_data.size() - std::min(m_offset, m_data.size())) return false;
        ptr = m_data.data() + m_offset;
        m_offset += count;
        return true;
    }

    bool readUnsigned(std::size_t count, std::uint64_t& value) {
        const std::uint8_t* ptr = nullptr;
        if (!readBytes(count, ptr)) return false;
        value = 0;
        for (std::size_t i = 0; i < count; ++i) value = (value << 8) | ptr[i];
        return true;
    }

    bool readString(std::size_t count, std::string& output) {
        const std::uint8_t* ptr = nullptr;
        if (!readBytes(count, ptr)) return false;
        output.assign(reinterpret_cast<const char*>(ptr), count);
        return true;
    }

    bool readLength(std::uint8_t code, std::uint64_t& length) {
        if (code == 0xD9 || code == 0xC4 || code == 0xC7) return readUnsigned(1, length);
        if (code == 0xDA || code == 0xC5 || code == 0xC8
            || code == 0xDC || code == 0xDE) return readUnsigned(2, length);
        if (code == 0xDB || code == 0xC6 || code == 0xC9
            || code == 0xDD || code == 0xDF) return readUnsigned(4, length);
        return false;
    }

    bool readValue(std::string& output, unsigned depth) {
        if (depth > 64 || ++m_nodes > 5'000'000 || m_offset >= m_data.size()) return false;
        const auto code = m_data[m_offset++];

        if (code <= 0x7F) { output = std::to_string(code); return true; }
        if (code >= 0xE0) { output = std::to_string(static_cast<int>(static_cast<std::int8_t>(code))); return true; }

        if ((code & 0xF0) == 0x80) {
            const auto count = code & 0x0F;
            return readMap(count, output, depth);
        }
        if ((code & 0xF0) == 0x90) {
            const auto count = code & 0x0F;
            return readArray(count, output, depth);
        }
        if ((code & 0xE0) == 0xA0) {
            std::string value;
            if (!readString(code & 0x1F, value)) return false;
            output = "\"" + escapeJson(value) + "\"";
            return true;
        }

        switch (code) {
            case 0xC0: output = "null"; return true;
            case 0xC2: output = "false"; return true;
            case 0xC3: output = "true"; return true;
            case 0xCA: {
                std::uint64_t bits;
                if (!readUnsigned(4, bits)) return false;
                const float value = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
                if (!std::isfinite(value)) return false;
                std::ostringstream s; s << std::setprecision(9) << value; output = s.str(); return true;
            }
            case 0xCB: {
                std::uint64_t bits;
                if (!readUnsigned(8, bits)) return false;
                const double value = std::bit_cast<double>(bits);
                if (!std::isfinite(value)) return false;
                std::ostringstream s; s << std::setprecision(17) << value; output = s.str(); return true;
            }
            case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
                std::uint64_t value;
                const std::size_t size = code == 0xCC ? 1 : code == 0xCD ? 2 : code == 0xCE ? 4 : 8;
                if (!readUnsigned(size, value)) return false;
                output = std::to_string(value); return true;
            }
            case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
                std::uint64_t bits;
                const std::size_t size = code == 0xD0 ? 1 : code == 0xD1 ? 2 : code == 0xD2 ? 4 : 8;
                if (!readUnsigned(size, bits)) return false;
                std::int64_t value = 0;
                if (size == 1) value = static_cast<std::int8_t>(bits);
                else if (size == 2) value = static_cast<std::int16_t>(bits);
                else if (size == 4) value = static_cast<std::int32_t>(bits);
                else value = std::bit_cast<std::int64_t>(bits);
                output = std::to_string(value); return true;
            }
            case 0xD9: case 0xDA: case 0xDB: {
                std::uint64_t length;
                if (!readLength(code, length) || length > 16ull * 1024ull * 1024ull) return false;
                std::string value;
                if (!readString(static_cast<std::size_t>(length), value)) return false;
                output = "\"" + escapeJson(value) + "\""; return true;
            }
            case 0xDC: case 0xDD: {
                std::uint64_t count;
                if (!readLength(code, count) || count > 2'000'000) return false;
                return readArray(count, output, depth);
            }
            case 0xDE: case 0xDF: {
                std::uint64_t count;
                if (!readLength(code, count) || count > 2'000'000) return false;
                return readMap(count, output, depth);
            }
            case 0xC4: case 0xC5: case 0xC6: {
                std::uint64_t length;
                if (!readLength(code, length) || length > 16ull * 1024ull * 1024ull) return false;
                std::string value;
                if (!readString(static_cast<std::size_t>(length), value)) return false;
                output = "\"" + escapeJson(value) + "\""; return true;
            }
            case 0xC7: case 0xC8: case 0xC9: {
                std::uint64_t length;
                if (!readLength(code, length) || length > 16ull * 1024ull * 1024ull) return false;
                const std::uint8_t* ignored = nullptr;
                if (!readBytes(1, ignored) || !readBytes(static_cast<std::size_t>(length), ignored)) return false;
                output = "null"; return true;
            }
            case 0xD4: case 0xD5: case 0xD6: case 0xD7: case 0xD8: {
                const std::size_t length = code == 0xD4 ? 1 : code == 0xD5 ? 2 : code == 0xD6 ? 4 : code == 0xD7 ? 8 : 16;
                const std::uint8_t* ignored = nullptr;
                if (!readBytes(1, ignored) || !readBytes(length, ignored)) return false;
                output = "null"; return true;
            }
            default: return false;
        }
    }

    bool readArray(std::uint64_t count, std::string& output, unsigned depth) {
        if (count > 2'000'000) return false;
        output = "[";
        for (std::uint64_t i = 0; i < count; ++i) {
            std::string item;
            if (!readValue(item, depth + 1)) return false;
            if (i) output += ",";
            output += item;
        }
        output += "]";
        return true;
    }

    bool readMap(std::uint64_t count, std::string& output, unsigned depth) {
        if (count > 2'000'000) return false;
        output = "{";
        for (std::uint64_t i = 0; i < count; ++i) {
            std::string key, value;
            if (!readValue(key, depth + 1) || !readValue(value, depth + 1)) return false;
            if (key.empty() || key.front() != '"') return false;
            if (i) output += ",";
            output += key + ":" + value;
        }
        output += "}";
        return true;
    }

    const std::vector<std::uint8_t>& m_data;
    std::size_t m_offset = 0;
    std::uint64_t m_nodes = 0;
};

template <typename T>
T getValue(const matjson::Value& object, const char* key, T fallback) {
    if (!object.isObject()) return fallback;
    const auto& value = object[key];
    if constexpr (std::is_same_v<T, std::string>) {
        auto result = value.asString();
        return result ? result.unwrap() : fallback;
    }
    else if constexpr (std::is_same_v<T, bool>) {
        auto result = value.asBool();
        return result ? result.unwrap() : fallback;
    }
    else if constexpr (std::is_floating_point_v<T>) {
        auto result = value.asDouble();
        return result ? static_cast<T>(result.unwrap()) : fallback;
    }
    else if constexpr (std::is_unsigned_v<T>) {
        auto result = value.asUInt();
        return result ? static_cast<T>(result.unwrap()) : fallback;
    }
    else {
        auto result = value.asInt();
        return result ? static_cast<T>(result.unwrap()) : fallback;
    }
}

std::string getEitherString(const matjson::Value& object, const char* first, const char* second) {
    auto value = getValue<std::string>(object, first, {});
    return value.empty() ? getValue<std::string>(object, second, {}) : value;
}

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

bool parseLegacy(const std::vector<std::uint8_t>& bytes, bool isJson, ReplayData& out, std::string& error) {
    std::string jsonText;
    if (isJson) {
        jsonText.assign(bytes.begin(), bytes.end());
    }
    else {
        MsgPackJsonReader reader(bytes);
        if (!reader.convert(jsonText, error)) return false;
    }

    auto parsed = matjson::parse(jsonText);
    if (!parsed) {
        error = "Could not decode legacy GDR data: " + parsed.unwrapErr().message;
        return false;
    }
    const auto& root = parsed.unwrap();
    if (!root.isObject()) {
        error = "The legacy GDR root must be a JSON object.";
        return false;
    }
    const auto& inputs = root["inputs"];
    if (!inputs.isArray()) {
        error = "The legacy GDR file does not contain a valid inputs array.";
        return false;
    }
    if (inputs.size() > 2'000'000) {
        error = "The replay contains too many input events (limit: 2,000,000).";
        return false;
    }

    out.sourceFormat = isJson ? "GDR JSON v1" : "GDR MessagePack v1";
    out.author = getEitherString(root, "author", "creator");
    out.framerate = getValue<double>(root, "framerate", 240.0);
    const double safeFps = std::isfinite(out.framerate) && out.framerate > 0.0 ? out.framerate : 240.0;
    const double duration = getValue<double>(root, "duration", 0.0);
    out.durationFrames = static_cast<std::uint64_t>(std::max(0.0, duration * safeFps));
    const auto& bot = root["bot"];
    out.bot = getValue<std::string>(bot, "name", {});
    const auto& level = root["level"];
    out.levelName = getValue<std::string>(level, "name", {});
    out.levelID = getValue<std::uint32_t>(level, "id", 0);

    if (!std::isfinite(out.framerate) || out.framerate <= 0.0 || out.framerate > 100000.0) {
        out.framerate = 240.0;
    }

    for (const auto& item : inputs) {
        if (!item.isObject()) continue;
        ReplayInput input;
        input.frame = getValue<std::uint64_t>(item, "frame", 0);
        input.button = static_cast<std::uint8_t>(std::clamp(getValue<int>(item, "btn", 1), 0, 255));
        input.player2 = getValue<bool>(item, "2p", false);
        input.down = getValue<bool>(item, "down", false);
        out.durationFrames = std::max(out.durationFrames, input.frame);
        out.inputs.push_back(input);
        // Legacy GDR marks platformer replays through directional button IDs.
        if (input.button != 1) out.platformer = true;

        const double x = getValue<double>(item, "xPosition",
            getValue<double>(item, "xpos", getValue<double>(item, "x", std::numeric_limits<double>::quiet_NaN())));
        const double y = getValue<double>(item, "yPosition",
            getValue<double>(item, "ypos", getValue<double>(item, "y", std::numeric_limits<double>::quiet_NaN())));
        const double rotation = getValue<double>(item, "rotation", 0.0);
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(rotation)) {
            out.positions.push_back({
                input.frame, static_cast<float>(x), static_cast<float>(y),
                static_cast<float>(rotation), input.player2
            });
        }
    }

    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
    std::stable_sort(out.positions.begin(), out.positions.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
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
                    out.positions.push_back({item.frame, item.xPosition, item.yPosition, item.rotation, item.player2});
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

    if (!std::isfinite(out.framerate) || out.framerate <= 0.0 || out.framerate > 100000.0) out.framerate = 240.0;
    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
    std::stable_sort(out.positions.begin(), out.positions.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
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
        const auto outputPath = directory / makeOutputName(kind);
        const auto tempPath = std::filesystem::path(outputPath.string() + ".tmp");

        std::ofstream file(tempPath, std::ios::binary);
        if (!file) {
            error = "Could not create the analysis output file.";
            return false;
        }

        file << "{\n"
             << "  \"format\": \"" << (kind == AnalysisKind::FrameCounter ? "macro-engine-frame-counter" : "macro-engine-hit-border") << "\",\n"
             << "  \"schema_version\": 1,\n"
             << "  \"source_format\": \"" << escapeJson(replay.sourceFormat) << "\",\n"
             << "  \"author\": \"" << escapeJson(replay.author) << "\",\n"
             << "  \"bot\": \"" << escapeJson(replay.bot) << "\",\n"
             << "  \"level_name\": \"" << escapeJson(replay.levelName) << "\",\n"
             << "  \"level_id\": " << replay.levelID << ",\n"
             << "  \"framerate\": " << std::setprecision(12) << replay.framerate << ",\n"
             << "  \"duration_frames\": " << replay.durationFrames << ",\n"
             << "  \"platformer\": " << (replay.platformer ? "true" : "false") << ",\n"
             << "  \"input_count\": " << replay.inputs.size() << ",\n"
             << "  \"trajectory_count\": " << replay.positions.size() << ",\n"
             << "  \"trajectory_available\": " << (replay.positions.empty() ? "false" : "true") << ",\n"
             << "  \"inputs\": [";
        for (std::size_t i = 0; i < replay.inputs.size(); ++i) {
            const auto& input = replay.inputs[i];
            file << (i ? ",\n" : "\n")
                 << "    {\"frame\": " << input.frame
                 << ", \"button\": " << static_cast<unsigned>(input.button)
                 << ", \"player\": \"" << (input.player2 ? "P2" : "P1")
                 << "\", \"action\": \"" << (input.down ? "press" : "release") << "\"}";
        }
        if (!replay.inputs.empty()) file << "\n  ";
        file << "]";
        if (kind == AnalysisKind::HitBorder) {
            file << ",\n  \"trajectory\": [";
            for (std::size_t i = 0; i < replay.positions.size(); ++i) {
                const auto& point = replay.positions[i];
                file << (i ? ",\n" : "\n")
                     << "    {\"frame\": " << point.frame
                     << ", \"x\": " << point.x << ", \"y\": " << point.y
                     << ", \"rotation\": " << point.rotation
                     << ", \"player\": \"" << (point.player2 ? "P2" : "P1") << "\"}";
            }
            if (!replay.positions.empty()) file << "\n  ";
            file << "]";
        }
        file << "\n}\n";
        file.close();

        if (!file) {
            std::filesystem::remove(tempPath);
            error = "Failed while writing the analysis file.";
            return false;
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

const ReplayData& ReplayAnalyzer::getCurrentReplay() const { return m_current; }
AnalysisKind ReplayAnalyzer::getCurrentKind() const { return m_currentKind; }
bool ReplayAnalyzer::hasCurrentReplay() const { return m_hasCurrent; }
std::uint64_t ReplayAnalyzer::getRevision() const { return m_revision; }

void ReplayAnalyzer::clear() {
    m_current = {};
    m_hasCurrent = false;
    ++m_revision;
}

} // namespace macroengine
