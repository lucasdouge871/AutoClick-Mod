#include "Bot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

namespace acm {

    namespace {
        // Physics numbers for a normal-size cube at 1x speed (level units, 30 = one block).
        constexpr float kSpikeLead = 64.f;      // start the jump this far before the centre of a spike group
        constexpr float kBlockLead = 77.f;      // ...or this far before the face of a block
        constexpr float kLookahead = 260.f;     // how far ahead the bot looks
        constexpr float kNormalSpeed = 0.9f;    // PlayerObject::m_playerSpeed at 1x
        constexpr uint32_t kHoldFrames = 8;     // how long a bot click is held
        constexpr float kMaxEarly = 80.f;
        constexpr float kMaxLate = -40.f;
        constexpr int kJumpButton = 1;

        std::string sanitize(std::string s) {
            for (auto& c : s) {
                if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
            }
            return s;
        }
    }

    Bot& Bot::get() {
        static Bot instance;
        return instance;
    }

    std::string Bot::keyFor(GJGameLevel* level) {
        if (!level) return "unknown";
        int id = level->m_levelID.value();
        if (id > 0) return "level_" + std::to_string(id);
        std::string name = level->m_levelName;
        return "local_" + sanitize(name);
    }

    std::filesystem::path Bot::macroPath() const {
        return Mod::get()->getSaveDir() / "macros" / (m_macroKey + ".acm");
    }

    // ------------------------------------------------------------------
    // Level lifecycle
    // ------------------------------------------------------------------

    void Bot::buildObstacles(PlayLayer* pl) {
        m_obstacles.clear();
        m_obstacleIndex.clear();
        if (!pl->m_objects) return;

        struct Raw {
            Obstacle ob;
            GameObject* object;
        };
        std::vector<Raw> raw;

        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (!obj) continue;
            bool hazard = obj->m_objectType == GameObjectType::Hazard;
            bool solid = obj->m_objectType == GameObjectType::Solid;
            if (!hazard && !solid) continue;

            auto const& r = obj->getObjectRect();
            Raw item;
            item.ob.minX = r.getMinX();
            item.ob.maxX = r.getMaxX();
            item.ob.minY = r.getMinY();
            item.ob.maxY = r.getMaxY();
            item.ob.hazard = hazard;
            item.object = obj;
            raw.push_back(item);
        }

        std::sort(raw.begin(), raw.end(), [](Raw const& a, Raw const& b) {
            if (a.ob.minX != b.ob.minX) return a.ob.minX < b.ob.minX;
            return a.ob.minY < b.ob.minY;
        });

        // Merge neighbouring spikes into one group so the bot clears the whole
        // group with a single jump instead of aiming at only the first spike.
        for (auto const& item : raw) {
            if (item.ob.hazard && !m_obstacles.empty()) {
                auto& last = m_obstacles.back();
                bool sameRow = std::abs(last.minY - item.ob.minY) < 10.f;
                if (last.hazard && sameRow && item.ob.minX - last.maxX < 40.f) {
                    last.maxX = std::max(last.maxX, item.ob.maxX);
                    last.maxY = std::max(last.maxY, item.ob.maxY);
                    m_obstacleIndex[item.object] = m_obstacles.size() - 1;
                    continue;
                }
            }
            m_obstacles.push_back(item.ob);
            m_obstacleIndex[item.object] = m_obstacles.size() - 1;
        }
    }

    void Bot::onLevelLoaded(PlayLayer* pl) {
        m_key = keyFor(pl->m_level);
        buildObstacles(pl);

        auto stored = m_adjustStore.find(m_key);
        if (stored != m_adjustStore.end() && stored->second.size() == m_obstacles.size()) {
            m_adjust = stored->second;
        } else {
            m_adjust.assign(m_obstacles.size(), Adjust{});
        }

        // A macro belongs to one level. Drop the old one and try the saved file.
        if (m_macroKey != m_key) {
            m_macro.clear();
            m_macroKey = m_key;
            this->loadForCurrentLevel();
        }

        m_cursor = 0;
        m_frame = 0;
        m_attempt = 0;
        m_deathAttempt = 0;
        m_used = false;
        m_botControls = false;
        m_autoHolding = false;
    }

    void Bot::onReset(PlayLayer* pl) {
        ++m_attempt;
        m_autoHolding = false;
        m_autoReleaseFrame = 0;
        m_cooldownUntil = 0;
        m_hasLastPress = false;
        m_used = false;
        m_botControls = false;

        uint32_t start = pl->m_gameState.m_currentProgress;
        m_frame = start;

        if (replay) {
            auto it = std::lower_bound(
                m_macro.begin(), m_macro.end(), start,
                [](Input const& in, uint32_t f) { return in.frame < f; }
            );
            m_cursor = static_cast<size_t>(it - m_macro.begin());
        }
        else if (record || autoPath) {
            // Fresh attempt: throw away the old take. Mid-level (practice checkpoint):
            // keep everything up to the checkpoint.
            if (start == 0) {
                m_macro.clear();
            } else {
                m_macro.erase(
                    std::remove_if(m_macro.begin(), m_macro.end(), [start](Input const& in) {
                        return in.frame > start;
                    }),
                    m_macro.end()
                );
            }
        }
    }

    void Bot::onLevelExit() {
        // Never carry the bot into the next level by accident.
        autoPath = false;
        record = false;
        replay = false;
        m_used = false;
        m_botControls = false;
        m_autoHolding = false;
        m_obstacles.clear();
        m_obstacleIndex.clear();
    }

    void Bot::onLevelComplete(PlayLayer*) {
        if ((record || autoPath) && !m_macro.empty()) {
            size_t count = m_macro.size();
            if (this->saveMacro()) {
                Notification::create(
                    "Auto Macro Bot: path saved (" + std::to_string(count) + " inputs)",
                    NotificationIcon::Success
                )->show();
            }
        }
        replay = false;
    }

    // ------------------------------------------------------------------
    // Per-step logic
    // ------------------------------------------------------------------

    void Bot::inject(PlayLayer* pl, bool down, int button, bool player1) {
        m_injecting = true;
        pl->handleButton(down, button, player1);
        m_injecting = false;
        m_used = true;
    }

    void Bot::beforeStep(PlayLayer* pl) {
        m_frame = pl->m_gameState.m_currentProgress;

        if (replay) {
            m_botControls = true;
            this->replayStep(pl);
        }
        else if (autoPath) {
            this->autoStep(pl);
        }
        else {
            m_botControls = false;
        }
    }

    void Bot::replayStep(PlayLayer* pl) {
        while (m_cursor < m_macro.size() && m_macro[m_cursor].frame <= m_frame) {
            Input in = m_macro[m_cursor++];
            this->inject(pl, in.down, in.button, in.player1);
        }
    }

    void Bot::releaseHeld(PlayLayer* pl) {
        if (m_autoHolding) {
            this->inject(pl, false, kJumpButton, true);
            m_autoHolding = false;
        }
    }

    void Bot::autoStep(PlayLayer* pl) {
        m_botControls = false;

        PlayerObject* p = pl->m_player1;
        if (!p || p->m_isDead || pl->m_isPlatformer || pl->m_gameState.m_isDualMode) {
            this->releaseHeld(pl);
            return;
        }

        // v1.0: the bot drives click-to-jump gameplay (cube / robot style).
        // In ship, ufo, wave, ball, swing and spider sections you stay in control.
        bool jumpStyle = !(p->m_isShip || p->m_isBird || p->m_isBall ||
                           p->m_isDart || p->m_isSwing || p->m_isSpider);
        if (!jumpStyle || p->m_isUpsideDown || p->m_isSideways) {
            this->releaseHeld(pl);
            return;
        }

        m_botControls = true;

        if (m_autoHolding) {
            if (m_frame >= m_autoReleaseFrame) {
                this->inject(pl, false, kJumpButton, true);
                m_autoHolding = false;
            }
            return;
        }

        if (!p->m_isOnGround || m_frame < m_cooldownUntil || m_obstacles.empty()) return;

        float px = p->getPositionX();
        float py = p->getPositionY();
        float feet = py - 15.f;
        float speed = std::max(0.3f, p->m_playerSpeed) / kNormalSpeed;

        auto it = std::lower_bound(
            m_obstacles.begin(), m_obstacles.end(), px - 60.f,
            [](Obstacle const& o, float x) { return o.minX < x; }
        );

        for (; it != m_obstacles.end(); ++it) {
            if (it->minX > px + kLookahead * speed) break;

            float dx = it->refX() - px;
            if (dx < -8.f) continue;

            // Is it actually in the player's lane?
            if (it->maxY < feet + 4.f) continue;   // below our feet
            if (it->minY > py + 20.f) continue;    // overhead

            if (!it->hazard) {
                float rise = it->maxY - feet;
                if (rise < 8.f) continue;    // a step we simply roll over
                if (rise > 62.f) continue;   // too tall for a cube jump
            }

            size_t idx = static_cast<size_t>(it - m_obstacles.begin());
            float base = it->hazard ? kSpikeLead : kBlockLead;
            float lead = base * speed + (idx < m_adjust.size() ? m_adjust[idx].value : 0.f);

            if (dx <= lead) {
                this->inject(pl, true, kJumpButton, true);
                m_autoHolding = true;
                m_autoReleaseFrame = m_frame + kHoldFrames;
                m_cooldownUntil = m_frame + kHoldFrames + 4;
                m_hasLastPress = true;
                m_lastPressObstacle = idx;
                m_lastPressX = px;
                return;
            }
        }
    }

    void Bot::onButton(bool down, int button, bool player1) {
        if (replay) return;
        if (!(record || autoPath)) return;
        Input in;
        in.frame = m_frame;
        in.down = down;
        in.button = button;
        in.player1 = player1;
        m_macro.push_back(in);
    }

    bool Bot::shouldBlockInput() const {
        if (m_injecting) return false;
        return replay || (autoPath && m_botControls);
    }

    // The bot learns from every death: if it died on an obstacle it nudges the
    // jump timing for that obstacle and tries again on the next attempt.
    void Bot::onDeath(PlayLayer* pl, PlayerObject* player, GameObject* by) {
        if (!autoPath || !m_botControls) return;
        if (!player || player != pl->m_player1) return;
        if (m_deathAttempt == m_attempt) return;   // already handled this death
        m_deathAttempt = m_attempt;

        float px = player->getPositionX();

        size_t idx = static_cast<size_t>(-1);
        auto found = m_obstacleIndex.find(by);
        if (by && found != m_obstacleIndex.end()) {
            idx = found->second;
        }
        else if (m_hasLastPress && px - m_lastPressX < 240.f) {
            idx = m_lastPressObstacle;   // died shortly after a jump
        }
        else {
            for (size_t i = 0; i < m_obstacles.size(); ++i) {   // died before reacting
                if (m_obstacles[i].refX() >= px - 12.f) { idx = i; break; }
            }
        }
        if (idx >= m_adjust.size()) return;

        int dir;
        if (player->m_isOnGround) dir = +1;         // hit it on the ground: too late
        else if (player->m_yVelocity > 0.0) dir = +1; // still rising: too late
        else dir = -1;                              // already falling: too early

        Adjust& a = m_adjust[idx];
        if (a.lastDir != 0 && a.lastDir != dir) a.step = std::max(1.f, a.step * 0.5f);
        a.value = std::clamp(a.value + static_cast<float>(dir) * a.step, kMaxLate, kMaxEarly);
        a.lastDir = dir;

        m_adjustStore[m_key] = m_adjust;
    }

    // ------------------------------------------------------------------
    // HUD text
    // ------------------------------------------------------------------

    std::string Bot::hudText() const {
        if (replay) {
            return fmt::format("Auto Macro Bot | Replay {}/{}", m_cursor, m_macro.size());
        }
        if (autoPath) {
            if (!m_botControls) return "Auto Macro Bot | Auto Path (you drive this section)";
            return fmt::format("Auto Macro Bot | Auto Path | {} inputs", m_macro.size());
        }
        if (record) {
            return fmt::format("Auto Macro Bot | Recording | {} inputs", m_macro.size());
        }
        return "";
    }

    // ------------------------------------------------------------------
    // Macro management + files
    // ------------------------------------------------------------------

    bool Bot::startReplay() {
        if (m_macro.empty()) this->loadForCurrentLevel();
        if (m_macro.empty()) return false;
        replay = true;
        autoPath = false;
        record = false;
        return true;
    }

    void Bot::stopAll() {
        autoPath = false;
        record = false;
        replay = false;
    }

    bool Bot::saveMacro() {
        if (m_macro.empty() || m_macroKey.empty()) return false;

        std::error_code ec;
        std::filesystem::create_directories(Mod::get()->getSaveDir() / "macros", ec);

        std::ofstream out(this->macroPath(), std::ios::trunc);
        if (!out) return false;

        out << "ACM1\n";
        for (auto const& in : m_macro) {
            out << in.frame << ' ' << (in.down ? 1 : 0) << ' '
                << in.button << ' ' << (in.player1 ? 1 : 0) << '\n';
        }
        return out.good();
    }

    bool Bot::loadForCurrentLevel() {
        m_macroKey = m_key;
        std::ifstream in(this->macroPath());
        if (!in) return false;

        std::string magic;
        std::getline(in, magic);
        if (magic != "ACM1") return false;

        std::vector<Input> loaded;
        uint32_t frame = 0;
        int down = 0, button = 0, p1 = 0;
        while (in >> frame >> down >> button >> p1) {
            Input item;
            item.frame = frame;
            item.down = down != 0;
            item.button = button;
            item.player1 = p1 != 0;
            loaded.push_back(item);
        }
        if (loaded.empty()) return false;

        std::stable_sort(loaded.begin(), loaded.end(), [](Input const& a, Input const& b) {
            return a.frame < b.frame;
        });
        m_macro = std::move(loaded);
        m_cursor = 0;
        return true;
    }

    void Bot::clearMacro(bool deleteFile) {
        m_macro.clear();
        m_cursor = 0;
        if (deleteFile && !m_macroKey.empty()) {
            std::error_code ec;
            std::filesystem::remove(this->macroPath(), ec);
        }
    }

}
