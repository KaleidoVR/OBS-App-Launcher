# Kaleido Launcher

<p align="center">
  <img src="logo/Kali_Logo.png" alt="KaleidoVR" width="300">
</p>

An OBS Studio plugin that starts and stops the apps you use for streaming. Configure the list once, then launch everything with OBS — or from **KaleidoVR → App Autostarter** in the top menu bar.

Add the programs you need for a stream, pick a profile, and let OBS bring them up when it starts. You can launch or close them by hand from the same window, save the setup with a scene collection, and export a JSON backup if you move machines.
<p align="center">
  <img width="462" height="792" alt="image" src="https://github.com/user-attachments/assets/3b54200e-439d-49ed-b2a8-c3aff30c633d" />
</p>

- Windows x64
- Designed for **OBS Studio 32.2.2**

## Install

1. Download the latest installer or ZIP from [Releases](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest).
2. **Installer** — run [KaleidoLauncher_Installer.exe](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest). It installs into your 64-bit OBS folder (`C:\Program Files\obs-studio`).
3. **ZIP** — extract [kaleido-launcher-win64.zip](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest) over that same OBS folder. The archive opens at `obs-plugins`.
4. Restart OBS, then open **KaleidoVR → App Autostarter** in the top menu bar.

A later installer upgrades the existing Apps & Features entry instead of adding a second one.

## Requirements

Windows x64 and a 64-bit OBS Studio install. The plugin was designed against **OBS Studio 32.2.2**.

There is no macOS or Linux build. The DLL belongs in `obs-plugins\64bit` next to OBS itself, not under `%ProgramData%`.

## Usage

1. Open **KaleidoVR → App Autostarter**.
2. Create or select a **profile** for that stream setup.
3. Use **+ Add App** to pick executables. Per app, you can start them minimized.
4. Optional: turn on automatic startup when OBS starts, and automatic close when OBS quits.
5. Press **Launch Apps** or **Close Apps** as needed.
6. **Save to Scene Collection** stores the launcher config with the current collection.

### Profiles

Each profile is its own app list. Switch profiles when you change shows instead of rebuilding the list every time.

### Scene collections

Launcher settings are saved under a Kaleido key in the scene collection, so they travel with that collection and do not spill into OBS's other collection data.

### Backup

**Export Settings Backup** writes a JSON file. **Import Settings Backup** replaces the current launcher config after a confirmation. Use this to copy a setup to another PC.

## Credits

Created and maintained by **KaleidoVR**.

- [kalivr.com](https://kalivr.com)
- [Discord](https://discord.com/invite/cRsufJssTA)

## License

[GNU GPL v2 or later](LICENSE) — Copyright (C) 2026 Kaleido VR
