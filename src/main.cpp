#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

#include <iomanip>
#include <sstream>

#include "MacroEngine.hpp"

using namespace geode::prelude;
using macroengine::MacroEngine;

class $modify(MEBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* playLayer = PlayLayer::get();
        if (playLayer && static_cast<GJBaseGameLayer*>(playLayer) == static_cast<GJBaseGameLayer*>(this) && !playLayer->m_useReplay && !MacroEngine::get().isPlaying()) {
            auto* player = isPlayer1 ? playLayer->m_player1 : playLayer->m_player2;
            const auto position = player ? player->getPosition() : CCPointZero;
            MacroEngine::get().recordInput(
                down,
                button,
                isPlayer1,
                playLayer->m_gameState.m_commandIndex,
                position.x,
                position.y
            );
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(MEPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* hitBorderlessDraw = nullptr;
        CCLabelBMFont* frameCounterLabel = nullptr;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& engine = MacroEngine::get();
        if (this->m_useReplay && engine.isPlaying()) {
            engine.stopPlayback();
        }
        if (engine.isPlaying() && !this->m_useReplay) {
            engine.updatePlayback([this](bool down, int button, bool player1) {
                this->handleButton(down, button, player1);
            });
        }

        if ((!engine.isRecording() && !engine.isPlaying()) || this->m_useReplay) {
            // Do not leave a stale trajectory on screen after recording stops.
            if (m_fields->hitBorderlessDraw) {
                m_fields->hitBorderlessDraw->clear();
                m_fields->hitBorderlessDraw->setVisible(false);
            }
            if (m_fields->frameCounterLabel) {
                m_fields->frameCounterLabel->setVisible(false);
            }
            return;
        }

        if (engine.isRecording()) {
            if (!m_fields->hitBorderlessDraw) {
                auto* draw = CCDrawNode::create();
                if (draw) {
                    draw->setID("macro-engine-hit-borderless-overlay");
                    draw->setZOrder(10000);
                    this->addChild(draw);
                    m_fields->hitBorderlessDraw = draw;
                }
            }
            if (m_fields->hitBorderlessDraw) {
                m_fields->hitBorderlessDraw->setVisible(true);
            }
        }
        else if (m_fields->hitBorderlessDraw) {
            m_fields->hitBorderlessDraw->clear();
            m_fields->hitBorderlessDraw->setVisible(false);
        }

        if (engine.isRecording()) {
            engine.onFrame(dt);
        }

        if (!m_fields->frameCounterLabel) {
            auto* label = CCLabelBMFont::create("FE 0 | 0.00 ms", "bigFont.fnt");
            if (label) {
                label->setID("macro-engine-frame-counter");
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(0.24f);
                const auto size = CCDirector::sharedDirector()->getWinSize();
                label->setPosition({10.f, size.height - 18.f});
                label->setZOrder(10001);
                this->addChild(label);
                m_fields->frameCounterLabel = label;
            }
        }
        if (m_fields->frameCounterLabel) {
            const auto elapsedUs = engine.getElapsedMicroseconds();
            std::ostringstream counter;
            counter << (engine.isPlaying() ? "PLAY " : "REC ")
                    << "FE " << engine.getCurrentNormalizedFrame()
                    << " | " << std::fixed << std::setprecision(2)
                    << (static_cast<double>(elapsedUs) / 1000.0) << " ms";
            const auto inputIntervalUs = engine.getLastInputIntervalMicroseconds();
            if (inputIntervalUs >= 0) {
                counter << " | d " << (static_cast<double>(inputIntervalUs) / 1000.0) << " ms";
            }
            else {
                counter << " | d --";
            }
            m_fields->frameCounterLabel->setString(counter.str().c_str());
            m_fields->frameCounterLabel->setVisible(true);
        }

        if (engine.isRecording()) {
            const auto frame = this->m_gameState.m_commandIndex;
    
            if (this->m_player1) {
                const auto position = this->m_player1->getPosition();
                engine.recordPlayerPosition(position.x, position.y, true, frame);
            }
            if (this->m_player2) {
                const auto position = this->m_player2->getPosition();
                engine.recordPlayerPosition(position.x, position.y, false, frame);
            }
    
            // Draw a short, live trail to keep the overlay inexpensive on mobile.
            if (auto* draw = m_fields->hitBorderlessDraw) {
                draw->clear();
                const auto& samples = engine.getTrajectory();
                constexpr std::size_t maxTrailSamples = 1800;
                const auto first = samples.size() > maxTrailSamples
                    ? samples.size() - maxTrailSamples
                    : 0;
    
                const macroengine::MacroPositionSample* previousP1 = nullptr;
                const macroengine::MacroPositionSample* previousP2 = nullptr;
                for (std::size_t i = first; i < samples.size(); ++i) {
                    const auto& sample = samples[i];
                    auto*& previous = sample.player1 ? previousP1 : previousP2;
                    if (previous) {
                        const ccColor4F trailColor = sample.player1
                            ? ccColor4F{1.f, 0.82f, 0.12f, 0.92f}
                            : ccColor4F{1.f, 0.94f, 0.45f, 0.92f};
                        draw->drawSegment(
                            {previous->x, previous->y},
                            {sample.x, sample.y},
                            0.8f,
                            trailColor
                        );
                    }
                    previous = &sample;
                }
    
                // Input markers are placed at the player's sampled position.
                const auto& inputs = engine.getInputEvents();
                const auto markerFirst = inputs.size() > 160
                    ? inputs.size() - 160
                    : 0;
                for (std::size_t i = markerFirst; i < inputs.size(); ++i) {
                    const auto& event = inputs[i];
                    const ccColor4F markerColor = event.down
                        ? ccColor4F{0.55f, 0.36f, 0.02f, 1.f}
                        : ccColor4F{1.f, 1.f, 1.f, 1.f};
                    draw->drawDot(
                        {event.x, event.y},
                        event.down ? 2.6f : 2.1f,
                        markerColor
                    );
                }
            }
        }

    }

    void onQuit() {
        if (MacroEngine::get().isPlaying()) {
            MacroEngine::get().stopPlayback();
        }
        if (MacroEngine::get().isRecording()) {
            auto path = MacroEngine::get().stopAndSave();
            if (!path.empty()) {
                log::info("[Macro Engine] Recording saved: {}", path);
            }
        }
        PlayLayer::onQuit();
    }

    void onExit() {
        if (MacroEngine::get().isPlaying()) {
            MacroEngine::get().stopPlayback();
        }
        if (MacroEngine::get().isRecording()) {
            auto path = MacroEngine::get().stopAndSave();
            if (!path.empty()) {
                log::info("[Macro Engine] Recording saved: {}", path);
            }
        }
        PlayLayer::onExit();
    }
};

class $modify(MEPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        MacroEngine::get().pausePlayback();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition(CCPointZero);

        const char* labelText = MacroEngine::get().isRecording()
            ? "STOP & SAVE MACRO"
            : "START MACRO RECORDING";

        auto label = CCLabelBMFont::create(labelText, "bigFont.fnt");
        if (!label) {
            return;
        }

        auto button = CCMenuItemSpriteExtra::create(
            label,
            this,
            menu_selector(MEPauseLayer::onMacroEngineToggle)
        );
        button->setScale(0.38f);
        button->setPosition({
            winSize.width - 105.f,
            winSize.height - 45.f
        });

        menu->addChild(button);

        const char* playLabelText = MacroEngine::get().isPlaying()
            ? "STOP MACRO PLAYBACK"
            : "PLAY LATEST MACRO";
        auto playLabel = CCLabelBMFont::create(playLabelText, "bigFont.fnt");
        if (playLabel) {
            auto playButton = CCMenuItemSpriteExtra::create(
                playLabel,
                this,
                menu_selector(MEPauseLayer::onMacroEnginePlayLatest)
            );
            playButton->setScale(0.34f);
            playButton->setPosition({
                winSize.width - 105.f,
                winSize.height - 76.f
            });
            menu->addChild(playButton);
        }

        this->addChild(menu, 1000);
    }

    void onMacroEnginePlayLatest(CCObject*) {
        auto& engine = MacroEngine::get();
        if (engine.isPlaying()) {
            auto* playLayer = PlayLayer::get();
            if (playLayer) {
                engine.stopPlayback([playLayer](bool down, int button, bool player1) {
                    playLayer->handleButton(down, button, player1);
                });
            }
            else {
                engine.stopPlayback();
            }
            this->onResume(nullptr);
            return;
        }

        std::string path;
        std::string error;
        if (!engine.loadLatestMacro(path, error)) {
            FLAlertLayer::create("Macro Engine", error.c_str(), "OK")->show();
            return;
        }
        if (!engine.startPlayback()) {
            FLAlertLayer::create(
                "Macro Engine",
                "The macro could not be started. Make sure no recording or playback is active and the macro contains inputs.",
                "OK"
            )->show();
            return;
        }
        log::info("[Macro Engine] Playing macro: {}", path);
        this->onResume(nullptr);
    }

    void onResume(CCObject* sender) {
        MacroEngine::get().resumePlayback();
        PauseLayer::onResume(sender);
    }

    void onMacroEngineToggle(CCObject*) {
        auto& engine = MacroEngine::get();

        if (engine.isRecording()) {
            const auto path = engine.stopAndSave();
            if (path.empty()) {
                FLAlertLayer::create(
                    "Macro Engine",
                    "The recording could not be saved. Check the Geode log for details.",
                    "OK"
                )->show();
            }
            else {
                const auto message = std::string("Macro saved successfully! ")
                    + std::to_string(engine.getInputCount())
                    + " inputs and "
                    + std::to_string(engine.getTrajectorySampleCount())
                    + " trajectory samples recorded.";
                FLAlertLayer::create("Macro Engine", message.c_str(), "Great")->show();
                log::info("[Macro Engine] Saved file: {}", path);
            }
            return;
        }

        if (engine.startRecording()) {
            log::info("[Macro Engine] Recording started from pause menu");
            this->onResume(nullptr);
        }
    }
};
