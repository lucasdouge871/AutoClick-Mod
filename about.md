# Auto Click Mod

**By Lucasdouge**

The **Auto Macro Bot** for Geometry Dash. It plays a path through your level, writes that path down, and lets you watch it run again with one button.

## How to use it

1. Start a level and open the **pause menu**.
2. Tap **Auto Bot** (bottom-left) to open the Auto Macro Bot panel.
3. Turn on **Auto Path**, then restart the attempt. The bot clicks for you and builds a path.
4. When the level is finished, the path is saved automatically.
5. Any time later, tap **Replay** in the pause menu to watch the bot run the saved path from the start.

## Features

- **Auto Path** - the bot looks ahead for spikes and blocks, jumps for you, and learns from each death by nudging its timing. Works in cube/robot style sections (ship, ufo, wave, ball, swing and spider sections are yours to drive).
- **Record** - records your own clicks frame by frame so any gamemode can be replayed.
- **Replay** - plays the stored path back. Your input is locked while it runs.
- **Save / Load / Clear** - paths are stored per level in the mod's save folder.
- **HUD** - small status label while the bot is active.

## Safe Mode (on by default)

Runs that use the bot (Auto Path or Replay) are treated like test runs and **never save progress or completions**. You can switch this off in the mod settings, but please keep bot runs off the leaderboards.

## Notes

- Auto Path is a best-effort path finder, not magic. Orbs, pads, portals and tricky timings can still beat it. Record your own run for those parts.
- Turning the bot on mid-attempt gives a partial path. Restart the attempt after changing modes.
