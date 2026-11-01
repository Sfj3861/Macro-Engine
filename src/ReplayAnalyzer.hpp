#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace macroengine {

struct ReplayInput {
    std::uint64_t frame = 0;
    std::uint8_t button = 1;
    bool player2 = false;
    bool down = false;
};

struct ReplayPosition {
    std::uint64_t frame = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    bool player2 = false;
};

struct ReplayData {
    std::string sourceFormat;
    std::string author;
    std::string bot;
    std::string levelName;
    std::uint32_t levelID = 0;
    double framerate = 240.0;
    std::uint64_t durationFrames = 0;
    bool platformer = false;
    std::vector<ReplayInput> inputs;
    std::vector<ReplayPosition> positions;
};

enum class AnalysisKind {
    FrameCounter,
    HitBorder
};

class ReplayAnalyzer {
public:
    static ReplayAnalyzer& get();

    bool importReplay(const std::string& path, AnalysisKind kind,
                      std::string& outputPath, std::string& message);
    const ReplayData& getCurrentReplay() const;
    AnalysisKind getCurrentKind() const;
    bool hasCurrentReplay() const;
    std::uint64_t getRevision() const;
    void clear();

private:
    ReplayAnalyzer() = default;
    ReplayData m_current;
    AnalysisKind m_currentKind = AnalysisKind::FrameCounter;
    bool m_hasCurrent = false;
    std::uint64_t m_revision = 0;
};

} // namespace macroengine
