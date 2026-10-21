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

// Cấu hình 5 Stage (Stage 5 là OFF ở index 4)
static constexpr std::array<StageConfig, 5> kStages = {{
    { 2, 2 },
    { 4, 4 },
    { 8, 8 },
    { 16, 8 },
    { 0, 0 }
}};

namespace InputBufferState {
    int stage = 0; // 0 đến 4
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
        // Dùng CCLog chuẩn C++ thuần, tuyệt đối không đụng vào fmtlib
        CCLog("[PredictiveBuffer] Stage changed");
    }
}

// Hiển thị thông báo ngắn trên màn hình chơi khi đổi Stage
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

        if (!pushing
