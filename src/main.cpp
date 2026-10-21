#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <array>
#include <string>

using namespace geode::prelude;

struct StageConfig {
    int all;
    int wave;
};

static constexpr std::array<StageConfig, 5> kStages = {{
    { 2, 2 },
    { 4, 4 },
    { 8, 8 },
    { 16, 8 },
    { 0, 0 }
}};

namespace InputBufferState {
    int stage = 0;
    bool inLevel = false;
    bool isEditor = false;
    int tick = 0;
    bool replaying = false;

    struct PendingInput {
        bool active = false;
        int button = 0;
        bool pushing = true;
        int frame0 = 0;
    };
    PendingInput p1, p2;

    int getLimit(bool wave) {
        if (stage >= 4) return 0;
        return wave ? kStages[stage].wave : kStages[stage].all;
    }

    void reset() {
        tick = 0;
        p1 = {};
        p2 = {};
    }

    void fire(GJBaseGameLayer* gl, PendingInput& pend, bool isP1) {
        if (!pend.active) return;
        int btn = pend.button;
        bool push = pend.pushing;
        pend = {};

        replaying = true;
        gl->handleButton(push, btn, isP1);
        replaying = false;
    }

    void cycleStage() {
        stage = (stage + 1) % 5;
    }
}

static void showHUD() {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    if (auto* old = pl->getChildByTag(8310)) {
        old->removeFromParent();
    }

    std::string text = "Buffer Stage: ";
    text += std::to_string(InputBufferState::stage + 1);
    if (InputBufferState::stage == 4) {
        text = "Buffer: OFF";
    }

    auto* lbl = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    if (!lbl) return;
    
    lbl->setScale(0.7f);
    lbl->setTag(8310);

    auto ws = CCDirector::get()->getWinSize();
    lbl->setPosition({ws.width * 0.5f, ws.height - 40.f});
    pl->addChild(lbl, 999);

    lbl->runAction(CCSequence::create(
        CCDelayTime::create(1.5f),
        CCFadeOut::create(0.3f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

struct ModPlayLayer : Modify<ModPlayLayer, PlayLayer> {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;
        InputBufferState::reset();
        InputBufferState::inLevel = true;
        InputBufferState::isEditor = false;
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        InputBufferState::p1 = {};
        InputBufferState::p2 = {};
        PlayLayer::destroyPlayer(player, obj);
    }

    void onQuit() {
        InputBufferState::inLevel = false;
        InputBufferState::reset();
        PlayLayer::onQuit();
    }
};

struct ModEditorLayer : Modify<ModEditorLayer, LevelEditorLayer> {
    bool init(GJGameLevel* lvl, bool unk) {
        InputBufferState::isEditor = true;
        return LevelEditorLayer::init(lvl, unk);
    }
};

struct ModBaseGameLayer : Modify<ModBaseGameLayer, GJBaseGameLayer> {
    void handleButton(bool pushing, int button, bool isPlayer1) {
        if (InputBufferState::replaying || !InputBufferState::inLevel || InputBufferState::isEditor) {
            GJBaseGameLayer::handleButton(pushing, button, isPlayer1);
            return;
        }

        auto* pl = PlayLayer::get();
        PlayerObject* player = pl ? (isPlayer1 ? pl->m_player1 : pl->m_player2) : nullptr;
        bool wave = player && (player->m_gamemode == IconType::Wave);
        int limit = InputBufferState::getLimit(wave);

        if (limit == 0) {
            GJBaseGameLayer::handleButton(pushing, button, isPlayer1);
            return;
        }

        auto& pend = isPlayer1 ? InputBufferState::p1 : InputBufferState::p2;
        if (pend.active) {
            GJBaseGameLayer::handleButton(pushing, button, isPlayer1);
            return;
        }

        if (!pushing && !wave) {
            GJBaseGameLayer::handleButton(pushing, button, isPlayer1);
            return;
        }

        pend = { true, button, pushing, InputBufferState::tick };
    }

    void collisionCheckObjects(PlayerObject* player, CCArray* arr, float p2, float p3) {
        if (InputBufferState::inLevel && !InputBufferState::isEditor && arr && arr->count() > 0) {
            auto* pl = PlayLayer::get();
            bool isP1 = pl && (player == pl->m_player1);
            auto& pend = isP1 ? InputBufferState::p1 : InputBufferState::p2;

            if (pend.active) {
                for (uint32_t i = 0; i < arr->count(); ++i) {
                    auto* obj = typeinfo_cast<EffectGameObject*>(arr->objectAtIndex(i));
                    if (!obj) continue;
                    if (!GJBaseGameLayer::canBeActivatedByPlayer(player, obj)) continue;

                    InputBufferState::fire(this, pend, isP1);
                    break;
                }
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, arr, p2, p3);
    }
};

struct ModPlayerObject : Modify<ModPlayerObject, PlayerObject> {
    void update(float dt) {
        PlayerObject::update(dt);
        if (!InputBufferState::inLevel || InputBufferState::isEditor) return;

        auto* pl = PlayLayer::get();
        if (!pl) return;

        bool isP1 = (this == pl->m_player1);
        if (isP1) InputBufferState::tick++;

        auto& pend = isP1 ? InputBufferState::p1 : InputBufferState::p2;
        if (!pend.active) return;

        bool wave = (m_gamemode == IconType::Wave);
        int limit = InputBufferState::getLimit(wave);
        int age = InputBufferState::tick - pend.frame0;

        if (limit > 0 && age >= limit) {
            InputBufferState::fire(static_cast<GJBaseGameLayer*>(pl), pend, isP1);
        }
    }
};

struct ModPauseLayer : Modify<ModPauseLayer, PauseLayer> {
    void customSetup() {
        PauseLayer::customSetup();

        auto* lbl = CCLabelBMFont::create("Buffer Stage", "bigFont.fnt");
        if (lbl) {
            lbl->setScale(0.4f);
            auto* btn = CCMenuItemExt::createSpriteExtra(lbl, [this](auto*) {
                InputBufferState::cycleStage();
                showHUD();
            });

            auto* menu = CCMenu::create();
            menu->addChild(btn);

            auto ws = CCDirector::get()->getWinSize();
            menu->setPosition({ws.width * 0.5f, 25.f});
            this->addChild(menu, 10);
        }
    }
};
