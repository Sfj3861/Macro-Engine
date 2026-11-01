#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ReplayAnalyzer.hpp"

using namespace geode::prelude;
using macroengine::AnalysisKind;
using macroengine::ReplayAnalyzer;

class $modify(MEPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* hitBorderDraw = nullptr;
        CCLabelBMFont* frameCounterLabel = nullptr;
        bool hitBorderDrawn = false;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& analyzer = ReplayAnalyzer::get();
        if (!analyzer.hasCurrentReplay()) {
            if (m_fields->hitBorderDraw) {
                m_fields->hitBorderDraw->clear();
                m_fields->hitBorderDraw->setVisible(false);
            }
            if (m_fields->frameCounterLabel) {
                m_fields->frameCounterLabel->setVisible(false);
            }
            return;
        }

        const auto& replay = analyzer.getCurrentReplay();
        if (!m_fields->frameCounterLabel) {
            auto* label = CCLabelBMFont::create("MACRO ENGINE", "bigFont.fnt");
            if (label) {
                label->setID("macro-engine-analysis-hud");
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(0.28f);
                const auto size = CCDirector::sharedDirector()->getWinSize();
                label->setPosition({10.f, size.height - 18.f});
                label->setZOrder(10001);
                this->addChild(label);
                m_fields->frameCounterLabel = label;
            }
        }

        if (analyzer.getCurrentKind() == AnalysisKind::FrameCounter) {
            if (m_fields->hitBorderDraw) {
                m_fields->hitBorderDraw->clear();
                m_fields->hitBorderDraw->setVisible(false);
            }

            if (m_fields->frameCounterLabel) {
                const auto currentFrame = static_cast<std::uint64_t>(this->m_gameState.m_commandIndex);
                auto next = std::lower_bound(
                    replay.inputs.begin(), replay.inputs.end(), currentFrame,
                    [](const macroengine::ReplayInput& input, std::uint64_t frame) {
                        return input.frame < frame;
                    }
                );

                std::ostringstream text;
                text << "FRAME COUNTER | GF " << currentFrame
                     << " | " << std::fixed << std::setprecision(1) << replay.framerate << " TPS"
                     << " | INPUTS " << replay.inputs.size();
                if (next != replay.inputs.end()) {
                    text << " | NEXT +" << (next->frame > currentFrame ? next->frame - currentFrame : 0);
                }
                else {
                    text << " | END";
                }
                m_fields->frameCounterLabel->setString(text.str().c_str());
                m_fields->frameCounterLabel->setVisible(true);
            }
            return;
        }

        if (m_fields->frameCounterLabel) {
            std::ostringstream text;
            text << "HIT BORDER | " << replay.positions.size() << " POSITION SAMPLES";
            if (replay.positions.empty()) {
                text << " | NO TRAJECTORY DATA";
            }
            m_fields->frameCounterLabel->setString(text.str().c_str());
            m_fields->frameCounterLabel->setVisible(true);
        }

        if (!m_fields->hitBorderDraw) {
            auto* draw = CCDrawNode::create();
            if (draw) {
                draw->setID("macro-engine-hit-border-overlay");
                draw->setZOrder(10000);
                this->addChild(draw);
                m_fields->hitBorderDraw = draw;
            }
        }

        if (m_fields->hitBorderDraw) {
            m_fields->hitBorderDraw->setVisible(true);
            if (!m_fields->hitBorderDrawn) {
                m_fields->hitBorderDraw->clear();

                const macroengine::ReplayPosition* previousP1 = nullptr;
                const macroengine::ReplayPosition* previousP2 = nullptr;
                for (const auto& point : replay.positions) {
                    auto*& previous = point.player2 ? previousP2 : previousP1;
                    if (previous) {
                        const ccColor4F color = point.player2
                            ? ccColor4F{0.15f, 0.78f, 1.f, 0.92f}
                            : ccColor4F{1.f, 0.82f, 0.12f, 0.92f};
                        m_fields->hitBorderDraw->drawSegment(
                            {previous->x, previous->y},
                            {point.x, point.y},
                            1.1f,
                            color
                        );
                    }
                    m_fields->hitBorderDraw->drawDot(
                        {point.x, point.y},
                        1.6f,
                        point.player2 ? ccColor4F{0.15f, 0.78f, 1.f, 1.f}
                                      : ccColor4F{1.f, 0.82f, 0.12f, 1.f}
                    );
                    previous = &point;
                }
                m_fields->hitBorderDrawn = true;
            }
        }
    }

    void onExit() {
        if (m_fields->hitBorderDraw) {
            m_fields->hitBorderDraw->clear();
        }
        PlayLayer::onExit();
    }
};

class $modify(MEPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        const auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition(CCPointZero);

        auto addImportButton = [&](const char* text, float y, SEL_MenuHandler callback) {
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            if (!label) return;
            auto* button = CCMenuItemSpriteExtra::create(label, this, callback);
            button->setScale(0.34f);
            button->setPosition({winSize.width - 112.f, y});
            menu->addChild(button);
        };

        addImportButton("IMPORT REPLAY > FCR", winSize.height - 43.f,
            menu_selector(MEPauseLayer::onImportFrameCounter));
        addImportButton("IMPORT REPLAY > HBR", winSize.height - 76.f,
            menu_selector(MEPauseLayer::onImportHitBorder));

        this->addChild(menu, 1000);
    }

    void pickReplay(AnalysisKind kind) {
        file::FilePickOptions options;
        options.filters.push_back({
            "Geometry Dash replay files",
            {"*.gdr", "*.gdr.json", "*.gdr2"}
        });

        Ref<MEPauseLayer> self(this);
        async::spawn(
            file::pick(file::PickMode::OpenFile, std::move(options)),
            [self, kind](auto result) {
                if (!self) return;
                if (!result) {
                    FLAlertLayer::create("Macro Engine", result.unwrapErr().c_str(), "OK")->show();
                    return;
                }

                auto selected = std::move(result).unwrap();
                if (!selected) return;

                std::string outputPath;
                std::string message;
                const auto inputPath = selected->string();
                if (!ReplayAnalyzer::get().importReplay(inputPath, kind, outputPath, message)) {
                    FLAlertLayer::create("Import failed", message.c_str(), "OK")->show();
                    return;
                }

                FLAlertLayer::create(
                    kind == AnalysisKind::FrameCounter ? "Frame Counter ready" : "Hit Border ready",
                    message.c_str(),
                    "Great"
                )->show();
                log::info("[Macro Engine] {}", message);
            }
        );
    }

    void onImportFrameCounter(CCObject*) {
        this->pickReplay(AnalysisKind::FrameCounter);
    }

    void onImportHitBorder(CCObject*) {
        this->pickReplay(AnalysisKind::HitBorder);
    }
};
