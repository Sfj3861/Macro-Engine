#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "MacroEngine.hpp"

using namespace geode::prelude;
using macroengine::MacroEngine;

class $modify(MEBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* playLayer = PlayLayer::get();
        if (playLayer && static_cast<GJBaseGameLayer*>(playLayer) == static_cast<GJBaseGameLayer*>(this) && !playLayer->m_useReplay) {
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

class $modify(MEPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& engine = MacroEngine::get();
        if (!engine.isRecording() || this->m_useReplay) {
            return;
        }

        engine.onFrame(dt);
        const auto frame = this->m_gameState.m_commandIndex;

        if (this->m_player1) {
            const auto position = this->m_player1->getPosition();
            engine.recordPlayerPosition(position.x, position.y, true, frame);
        }
        if (this->m_player2) {
            const auto position = this->m_player2->getPosition();
            engine.recordPlayerPosition(position.x, position.y, false, frame);
        }
    }

    void onQuit() {
        if (MacroEngine::get().isRecording()) {
            auto path = MacroEngine::get().stopAndSave();
            if (!path.empty()) {
                log::info("[Macro Engine] Recording saved: {}", path);
            }
        }
        PlayLayer::onQuit();
    }

    void onExit() {
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
                const auto message = std::string("Macro saved successfully! ")
                    + std::to_string(engine.getInputCount())
                    + " inputs and trajectory samples recorded.";
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
