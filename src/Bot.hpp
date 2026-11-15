#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace acm {

    // One recorded button event. `frame` is the physics step it happened on.
    struct Input {
        uint32_t frame = 0;
        bool down = false;
        int button = 1;
        bool player1 = true;
    };

    // A thing the Auto Path bot has to get past (a spike group or a block).
    struct Obstacle {
        float minX = 0.f, maxX = 0.f, minY = 0.f, maxY = 0.f;
        bool hazard = true;
        float refX() const { return hazard ? (minX + maxX) * 0.5f : minX; }
    };

    // Learned timing correction for one obstacle.
    // value > 0 means "jump earlier", value < 0 means "jump later" (in level units).
    struct Adjust {
        float value = 0.f;
        float step = 6.f;
        int lastDir = 0;
    };

    class Bot {
    public:
        static Bot& get();

        // ---- Modes (toggled from the Auto Macro Bot popup / pause menu) ----
        bool autoPath = false;   // bot plays by itself and writes the path as it goes
        bool record = false;     // record the player's own clicks
        bool replay = false;     // play the stored macro back

        // ---- Hook entry points ----
        void onLevelLoaded(PlayLayer* pl);
        void onReset(PlayLayer* pl);
        void onLevelExit();
        void onLevelComplete(PlayLayer* pl);
        void beforeStep(PlayLayer* pl);
        void onButton(bool down, int button, bool player1);
        void onDeath(PlayLayer* pl, PlayerObject* player, GameObject* by);
        bool shouldBlockInput() const;

        // ---- Queries ----
        bool isActive() const { return autoPath || replay; }
        bool usedThisAttempt() const { return m_used; }
        bool hasMacro() const { return !m_macro.empty(); }
        size_t macroSize() const { return m_macro.size(); }
        std::string const& levelKey() const { return m_key; }
        std::string hudText() const;

        // ---- Macro management ----
        bool startReplay();          // arms replay mode (loads from disk if needed)
        void stopAll();
        bool saveMacro();
        bool loadForCurrentLevel();
        void clearMacro(bool deleteFile);

    private:
        static std::string keyFor(GJGameLevel* level);
        void buildObstacles(PlayLayer* pl);
        void replayStep(PlayLayer* pl);
        void autoStep(PlayLayer* pl);
        void releaseHeld(PlayLayer* pl);
        void inject(PlayLayer* pl, bool down, int button, bool player1);
        std::filesystem::path macroPath() const;

        // level
        std::string m_key;
        std::string m_macroKey;

        // timeline
        uint32_t m_frame = 0;
        uint32_t m_attempt = 0;

        // macro
        std::vector<Input> m_macro;
        size_t m_cursor = 0;

        // input state
        bool m_injecting = false;
        bool m_used = false;          // bot touched the controls this attempt
        bool m_botControls = false;   // bot is currently in charge of the player

        // auto path
        std::vector<Obstacle> m_obstacles;
        std::unordered_map<GameObject*, size_t> m_obstacleIndex;
        std::vector<Adjust> m_adjust;
        std::unordered_map<std::string, std::vector<Adjust>> m_adjustStore;
        bool m_autoHolding = false;
        uint32_t m_autoReleaseFrame = 0;
        uint32_t m_cooldownUntil = 0;
        bool m_hasLastPress = false;
        size_t m_lastPressObstacle = 0;
        float m_lastPressX = 0.f;
        uint32_t m_deathAttempt = 0;
    };

}
