#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

static int s_currentStage = 1;

inline int getStageTicks(int stage, bool isWave) {
    switch (stage) {
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
        case 4: return isWave ? 8 : 16;
        case 5: default: return 0;
    }
}

inline const char* getStageName(int stage) {
    switch (stage) {
        case 1: return "Stage 1 (2 ticks)";
        case 2: return "Stage 2 (4 ticks)";
        case 3: return "Stage 3 (8 ticks)";
        case 4: return "Stage 4 (16/8 ticks)";
        case 5: return "Stage 5 (OFF)";
        default: return "Stage Unknown";
    }
}

void showStageNotification(PlayLayer* playLayer, int stage) {
    if (!playLayer) return;
    std::string msg = "Predictive Buffer: ";
    msg += getStageName(stage);
    Notification::create(msg, NotificationIcon::Info)->show();
}

struct BufferedInput {
    bool down;
    int button;
    bool isPlayer1;
    int ticksRemaining;
};

class $modify(PredictiveGameLayer, GJBaseGameLayer) {
    struct Fields {
        std::vector<BufferedInput> bufferedInputs;
        bool isInternalCall = false;
    };

    void handleButton(bool down, int button, bool isPlayer1) {
        if (s_currentStage == 5 || m_fields->isInternalCall) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        bool isWave = false;
        if (isPlayer1 && m_player1) {
            isWave = m_player1->m_isWave;
        } else if (!isPlayer1 && m_player2) {
            isWave = m_player2->m_isWave;
        }

        int maxTicks = getStageTicks(s_currentStage, isWave);
        if (maxTicks <= 0) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        m_fields->bufferedInputs.push_back({down, button, isPlayer1, maxTicks});
    }

    void update(float dt) {
        GJBaseGameLayer::update(dt);

        if (s_currentStage == 5 || m_fields->bufferedInputs.empty()) {
            return;
        }

        auto& inputs = m_fields->bufferedInputs;
        for (auto it = inputs.begin(); it != inputs.end();) {
            it->ticksRemaining--;

            if (it->ticksRemaining <= 0) {
                m_fields->isInternalCall = true;
                this->handleButton(it->down, it->button, it->isPlayer1);
                m_fields->isInternalCall = false;

                it = inputs.erase(it);
            } else {
                ++it;
            }
        }
    }
};

class $modify(PredictivePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();

        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2.f, 35.f});
        menu->setContentSize({300.f, 50.f});
        menu->setLayout(RowLayout::create()->setGap(10.f));

        auto btnSprite = ButtonSprite::create("Stage 1", 110, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.7f);
        
        auto btn = CCMenuItemSpriteExtra::create(
            btnSprite,
            this,
            menu_selector(PredictivePauseLayer::onChangeStageClicked)
        );
        btn->setID("predictive-stage-btn"_spr);
        menu->addChild(btn);
        menu->updateLayout();

        this->addChild(menu, 10);
        updateButtonText();
    }

    void updateButtonText() {
        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByID("predictive-stage-btn"));
        if (!btn) return;

        std::string labelText = "Stage ";
        labelText += std::to_string(s_currentStage);
        if (s_currentStage == 5) {
            labelText = "Stage 5 (OFF)";
        }

        auto newSprite = ButtonSprite::create(labelText.c_str(), 120, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.7f);
        btn->setNormalImage(newSprite);
    }

    void onChangeStageClicked(CCObject*) {
        s_currentStage++;
        if (s_currentStage > 5) {
            s_currentStage = 1;
        }

        Mod::get()->setSavedValue("current-stage", s_currentStage);
        updateButtonText();

        std::string msg = "Switched to: ";
        msg += getStageName(s_currentStage);
        Notification::create(msg, NotificationIcon::Info)->show();
    }
};

class $modify(PredictivePlayLayer, PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;

        s_currentStage = Mod::get()->getSavedValue<int>("current-stage", 1);
        showStageNotification(this, s_currentStage);

        return true;
    }
};
