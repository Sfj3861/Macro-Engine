#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "MacroEngine.hpp"

using namespace geode::prelude;
using hitborderless::MacroEngine;

// Capture gameplay button transitions. The base layer is also used by the
// editor, so only events belonging to the active, non-replay PlayLayer are recorded.
class $modify(HBBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* playLayer = PlayLayer::get();
        if (playLayer == this && !playLayer->m_useReplay) {
            MacroEngine::get().recordInput(
                down,
                button,
                isPlayer1,
                playLayer->m_gameState.m_commandIndex
            );
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// Count PlayLayer update steps separately from the game's simulation command
// index and from wall-clock timestamps.
class $modify(HBPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        MacroEngine::get().onFrame(dt);
        PlayLayer::postUpdate(dt);
    }

    void onQuit() {
        if (MacroEngine::get().isRecording()) {
            auto path = MacroEngine::get().stopAndSave();
            if (!path.empty()) {
                log::info("[Hit Borderless] Recording saved: {}", path);
            }
        }
        PlayLayer::onQuit();
    }

    void onExit() {
        if (MacroEngine::get().isRecording()) {
            auto path = MacroEngine::get().stopAndSave();
            if (!path.empty()) {
                log::info("[Hit Borderless] Recording saved: {}", path);
            }
        }
        PlayLayer::onExit();
    }
};

// Pause-menu control: start recording and resume, or stop and save the take.
class $modify(HBPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

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
            menu_selector(HBPauseLayer::onMacroEngineToggle)
        );
        button->setScale(0.38f);
        button->setPosition({
            winSize.width - 105.f,
            winSize.height - 45.f
        });

        menu->addChild(button);
        this->addChild(menu, 1000);
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
                FLAlertLayer::create(
                    "Macro Engine",
                    "Macro saved successfully! " + std::to_string(engine.getInputCount()) + " inputs recorded.",
                    "Great"
                )->show();
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
