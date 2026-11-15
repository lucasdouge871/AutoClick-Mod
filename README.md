# Auto Click Mod (by Lucasdouge)

Geode mod for Geometry Dash 2.2081. Auto Macro Bot with Auto Path, Record and Replay.

## Get the .geode file

A `.geode` file is a zip that contains the **compiled** mod for each platform, so it has to be built with the Geode toolchain.

### Easiest: build it on GitHub (no setup)
1. Create a new GitHub repo and upload everything in this folder (keep the `.github` folder).
2. Open the **Actions** tab and let **Build Auto Click Mod** run (or press *Run workflow*).
3. When it finishes, download the **Build Output** artifact. Inside is `lucasdouge.auto-click-mod.geode`.
4. Put that file in your Geometry Dash `geode/mods` folder (or drop it onto the game window) and restart.

### Build locally
1. Install the Geode CLI and SDK: https://docs.geode-sdk.org/getting-started/
2. In this folder run:

```
geode sdk install
geode build
```

The `.geode` file appears in `build/`.
