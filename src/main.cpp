/*
 * ═══════════════════════════════════════════════════════════════════════════
 *  Predictive Input Buffer  ·  Geode SDK v4  ·  GD Android 2.2081
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  BUILD RULES (avoid NDK Clang errors):
 *   – Zero fmtlib usage.  No <fmt/...>, no FMT_STRING, no fmt::format.
 *   – All strings: const char* or std::string.
 *   – Logging: CCLog() with printf-style specifiers only.
 *
 *  ARCHITECTURE
 *  ─────────────────────────────────────────────────────────────────────────
 *  Three $modify hooks:
 *
 *  PIBBase  (GJBaseGameLayer)
 *    handleButton          → intercept every tap/release; queue in g_buf.
 *    collisionCheckObjects → flush the buffer when an interactive object
 *                            (orb / pad / portal) overlaps the player,
 *                            injecting the press at the exact collision tick.
 *
 *  PIBPlay  (PlayLayer)
 *    init        → reset all state on level entry.
 *    update      → age queued entries each frame; timeout-flush stale ones
 *                  (handles open-air jumps that never hit an orb).
 *    resetLevel  → clear buffer on death / restart.
 *    onQuit      → tear down on level exit.
 *
 *  PIBPause (PauseLayer)
 *    customSetup  → add a tap-to-cycle button at the bottom of the pause UI.
 *    onCycleStage → advance g_stage, refresh button label.
 *
 *  STAGE TABLE
 *  ─────────────────────────────────────────────────────────────────────────
 *  Index │ Label           │ All-modes ticks │ Wave ticks
 *  ──────┼─────────────────┼─────────────────┼───────────
 *    0   │ Stage 1 [2t]    │        2        │     2
 *    1   │ Stage 2 [4t]    │        4        │     4
 *    2   │ Stage 3 [8t]    │        8        │     8    ← default
 *    3   │ Stage 4         │       16        │     8
 *    4   │ Stage 5 (OFF)   │   raw input, no buffering
 *
 *  HOW THE BUFFER WORKS
 *  ─────────────────────────────────────────────────────────────────────────
 *  1. Player taps N frames before an orb.
 *  2. handleButton is suppressed; the tap is stored with age = 0.
 *  3. Each physics step collisionCheckObjects scans the objects near the
 *     player.  When it finds an orb/pad/portal intersecting the player rect,
 *     it re-fires the oldest matching entry via GJBaseGameLayer::handleButton
 *     BEFORE the original collision logic runs.  The orb sees a live press.
 *  4. If no orb collision occurs within maxT frames the entry is timeout-
 *     flushed in PlayLayer::update (plain jump, input is never eaten).
 *
 *  RELEASE BUFFERING (Wave)
 *  Each tap *and* each release is stored.  Releases carry release=true and
 *  are flushed by the same logic, giving wave players predictive control
 *  over both the up-press and the up-release.
 *
 *  SIGNATURE NOTE
 *  ─────────────────────────────────────────────────────────────────────────
 *  collisionCheckObjects:  bool (PlayerObject*, CCArray*, bool, float)
 *  PlayLayer::init      :  bool (GJGameLevel*, bool)
 *  If your Geode codegen for GD 2.2081 uses different types/arity, adjust
 *  the hook signatures accordingly (check generated bindings headers).
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// ───────────────────────────────────────────────────────────────────────────
//  Stage configuration
// ───────────────────────────────────────────────────────────────────────────

static constexpr int kStages = 5;

//                          S1  S2  S3   S4  S5
static const int kAllTicks [kStages] = {  2,  4,  8,  16,  0 };
static const int kWaveTicks[kStages] = {  2,  4,  8,   8,  0 };

static const char* const kStageName[kStages] = {
    "PIB  Stage 1  [2t]",
    "PIB  Stage 2  [4t]",
    "PIB  Stage 3  [8t]",
    "PIB  Stage 4  [16t | Wave 8t]",
    "PIB  OFF  [Raw Input]"
};

// ───────────────────────────────────────────────────────────────────────────
//  Shared runtime state
// ───────────────────────────────────────────────────────────────────────────

// Persisted across sessions via Geode's save system.
static int  g_stage    = 2;     // 0–4  (Stage 3 / 8t default)
static bool g_inLevel  = false; // true only while inside a PlayLayer
static bool g_flushing = false; // re-entry guard for handleButton

// ── Input queue ────────────────────────────────────────────────────────────
struct InputEntry {
    bool release; // false = press  |  true = release
    bool p1;      // player index
    int  btn;     // button value forwarded to handleButton
    int  age;     // frame count since buffered
};

static std::vector<InputEntry> g_buf;

// ── HUD label ──────────────────────────────────────────────────────────────
// Weak ref — owned by the PlayLayer scene graph; NULLed on removal.
static CCLabelBMFont* g_hud     = nullptr;
static float          g_hudLife = 0.f;   // seconds remaining before fade-out

// ───────────────────────────────────────────────────────────────────────────
//  Helpers
// ───────────────────────────────────────────────────────────────────────────

// Max buffer ticks given the current stage and player game-mode.
static int stageTicks(bool wave) {
    if (g_stage >= 4) return 0;
    return wave ? kWaveTicks[g_stage] : kAllTicks[g_stage];
}

// Returns true for object types whose activation requires a simultaneous
// button press.  Verify enum identifiers against your Geode GD-2.2 codegen
// (typically in $GEODE_SDK/bindings/  or the generated .hpp headers).
static bool isInteractive(GameObjectType t) {
    using T = GameObjectType;
    switch (t) {
        // ── Orbs ───────────────────────────────────────────────────────────
        case T::YellowOrb:
        case T::PurpleOrb:
        case T::RedOrb:
        case T::BlueOrb:
        case T::GreenOrb:
        case T::BlackOrb:
        case T::SpiderOrb:
        case T::DashOrb:
        // ── Jump pads ──────────────────────────────────────────────────────
        case T::YellowPad:
        case T::PinkPad:
        case T::RedPad:
        case T::BluePad:
        // ── Mode & gravity portals ─────────────────────────────────────────
        case T::InverseGravityPortal:
        case T::NormalGravityPortal:
        case T::ShipPortal:
        case T::CubePortal:
        case T::BallPortal:
        case T::UfoPortal:
        case T::RobotPortal:
        case T::SpiderPortal:
        case T::SwingPortal:
        case T::WavePortal:
            return true;
        default:
            return false;
    }
}

// ── HUD ────────────────────────────────────────────────────────────────────

static void destroyHUD() {
    if (!g_hud) return;
    g_hud->removeFromParent();
    g_hud     = nullptr;
    g_hudLife = 0.f;
}

static void showHUD(const char* text) {
    destroyHUD();
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return;

    CCLabelBMFont* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
    if (!lbl) return;

    CCSize s = CCDirector::sharedDirector()->getWinSize();
    lbl->setAnchorPoint({ 0.5f, 1.f });
    lbl->setScale(0.5f);
    lbl->setOpacity(255);
    lbl->setPosition({ s.width * 0.5f, s.height - 20.f });
    lbl->setZOrder(999);
    pl->addChild(lbl);

    g_hud     = lbl;
    g_hudLife = 1.8f;
}

static void tickHUD(float dt) {
    if (!g_hud) return;
    g_hudLife -= dt;
    if (g_hudLife <= 0.f) {
        destroyHUD();
    } else if (g_hudLife < 0.4f) {
        // Smooth fade-out in the final 0.4 s.
        g_hud->setOpacity(static_cast<GLubyte>(255.f * g_hudLife / 0.4f));
    }
}

// ── Stage cycling ──────────────────────────────────────────────────────────

static void cycleStage() {
    g_stage = (g_stage + 1) % kStages;
    g_buf.clear();

    // Persist selection.
    Mod::get()->setSavedValue<int>("stage", g_stage);

    CCLog("PIB: stage -> %d (%s)", g_stage + 1, kStageName[g_stage]);
    showHUD(kStageName[g_stage]);
}

// ── Module init (load saved stage) ────────────────────────────────────────
// Runs once when the mod is loaded, before any gameplay.
$on_mod(Loaded) {
    g_stage = Mod::get()->getSavedValue<int>("stage", 2);
    // Clamp in case the save file contains an out-of-range value.
    if (g_stage < 0 || g_stage >= kStages) g_stage = 2;
    CCLog("PIB: loaded, stage = %d", g_stage + 1);
}

// ───────────────────────────────────────────────────────────────────────────
//  PIBBase  ·  GJBaseGameLayer
//  – handleButton          : intercept & queue
//  – collisionCheckObjects : collision-frame flush
// ───────────────────────────────────────────────────────────────────────────
class $modify(PIBBase, GJBaseGameLayer) {

    // ──────────────────────────────────────────────────────────────────────
    //  Intercept every button event while inside a level.
    //  g_flushing == true   →  we are calling back from our own flush code;
    //                          pass through directly to avoid infinite loop.
    // ──────────────────────────────────────────────────────────────────────
    void handleButton(bool down, int button, bool isP1) {
        if (!g_inLevel || g_stage >= 4 || g_flushing) {
            GJBaseGameLayer::handleButton(down, button, isP1);
            return;
        }
        // Queue and suppress the original call.
        g_buf.push_back({ !down, isP1, button, 0 });
    }

    // ──────────────────────────────────────────────────────────────────────
    //  Called by the engine each physics sub-step with the objects around p.
    //  We scan arr for interactive objects overlapping the player; if found,
    //  flush the oldest valid buffer entry BEFORE the original runs, so the
    //  game's collision code observes a live button-press.
    //
    //  Signature: bool (PlayerObject*, CCArray*, bool, float)
    //  Adjust if your Geode codegen differs (void return, different params).
    // ──────────────────────────────────────────────────────────────────────
    bool collisionCheckObjects(PlayerObject* p, CCArray* arr, bool b, float f) {
        if (g_inLevel && !g_flushing && g_stage < 4 &&
            p && arr && !g_buf.empty())
        {
            bool isP1 = (p == m_player1);
            bool wave = p->m_isWave;
            int  maxT = stageTicks(wave);

            // Walk arr: look for an interactive object touching the player.
            bool hit  = false;
            CCRect pr = p->getObjectRect();

            for (unsigned i = 0; i < arr->count() && !hit; ++i) {
                auto* obj = static_cast<GameObject*>(arr->objectAtIndex(i));
                if (obj && isInteractive(obj->m_objectType)) {
                    if (pr.intersectsRect(obj->getObjectRect())) {
                        hit = true;
                    }
                }
            }

            if (hit) {
                // Flush the oldest valid entry for this player.
                for (auto it = g_buf.begin(); it != g_buf.end(); ++it) {
                    if (it->p1 == isP1 && it->age <= maxT) {
                        g_flushing = true;
                        // Call original (trampoline); bypasses our queue check.
                        GJBaseGameLayer::handleButton(
                            !it->release, it->btn, it->p1
                        );
                        g_flushing = false;
                        g_buf.erase(it);
                        break; // one flush per collision-check pass
                    }
                }
            }
        }
        return GJBaseGameLayer::collisionCheckObjects(p, arr, b, f);
    }
};

// ───────────────────────────────────────────────────────────────────────────
//  PIBPlay  ·  PlayLayer
//  – init        : reset state on level entry
//  – update      : age buffer entries; timeout-flush expired ones
//  – resetLevel  : clear buffer on death / restart
//  – onQuit      : clean up on level exit
// ───────────────────────────────────────────────────────────────────────────
class $modify(PIBPlay, PlayLayer) {

    // Signature: bool (GJGameLevel*, bool)
    // Some GD 2.2 builds add a third bool parameter; add it here if needed.
    bool init(GJGameLevel* lvl, bool useReplay) {
        if (!PlayLayer::init(lvl, useReplay)) return false;
        g_inLevel  = true;
        g_flushing = false;
        g_buf.clear();
        destroyHUD();
        return true;
    }

    void update(float dt) {
        // Run original first — physics + all collision hooks fire inside.
        PlayLayer::update(dt);

        tickHUD(dt);

        if (!g_inLevel || g_stage >= 4 || g_flushing || g_buf.empty()) return;

        bool p1Wave = m_player1 && m_player1->m_isWave;
        bool p2Wave = m_player2 && m_player2->m_isWave;

        // Age every entry.  Flush ones that have outlived their stage window.
        for (auto it = g_buf.begin(); it != g_buf.end(); ) {
            ++(it->age);

            bool wave = it->p1 ? p1Wave : p2Wave;
            int  maxT = stageTicks(wave);

            if (maxT > 0 && it->age > maxT) {
                // Timeout: fire the input so it is never silently discarded.
                // this->handleButton goes through PIBBase::handleButton;
                // g_flushing == true makes it pass straight to the original.
                g_flushing = true;
                this->handleButton(!it->release, it->btn, it->p1);
                g_flushing = false;
                it = g_buf.erase(it);
            } else {
                ++it;
            }
        }
    }

    void resetLevel() {
        // Discard stale inputs on death / restart to prevent ghost presses.
        g_buf.clear();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        g_inLevel = false;
        g_buf.clear();
        destroyHUD();
        PlayLayer::onQuit();
    }
};

// ───────────────────────────────────────────────────────────────────────────
//  PIBPause  ·  PauseLayer
//  Adds a tap-to-cycle stage button at the bottom-centre of the pause UI.
// ───────────────────────────────────────────────────────────────────────────
class $modify(PIBPause, PauseLayer) {

    // m_fields lets us store extra data on modified Cocos2d nodes safely.
    struct Fields {
        CCLabelBMFont* stageLabel = nullptr;
    };

    void customSetup() {
        PauseLayer::customSetup();

        // Own CCMenu so our button never interferes with existing layout.
        CCMenu* menu = CCMenu::create();
        if (!menu) return;
        menu->setPosition(CCPointZero);
        addChild(menu, 100);

        // Build the label that doubles as the button's visual.
        CCLabelBMFont* lbl =
            CCLabelBMFont::create(kStageName[g_stage], "bigFont.fnt");
        if (!lbl) return;

        lbl->setScale(0.45f);
        lbl->setColor({ 255, 215, 0 }); // gold accent — readable on any bg
        m_fields->stageLabel = lbl;

        CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(
            lbl, this,
            menu_selector(PIBPause::onCycleStage)
        );
        if (!btn) return;

        // Bottom-centre of the screen, below the standard pause buttons.
        CCSize s = CCDirector::sharedDirector()->getWinSize();
        btn->setPosition({ s.width * 0.5f, 26.f });
        menu->addChild(btn);
    }

    void onCycleStage(CCObject*) {
        cycleStage();
        // Refresh the button label in-place; no need to rebuild the menu.
        if (m_fields->stageLabel) {
            m_fields->stageLabel->setString(kStageName[g_stage]);
        }
    }
};
