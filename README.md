# Kaleido Launcher

<p align="center">
  <img src="logo/Kali_Logo.png" alt="KaleidoVR" width="300">
</p>

I built this so OBS can start and stop the apps you stream with. You set the list once. OBS can launch them when it opens, or you do it from **KaleidoVR → App Autostarter**.

Pick a profile for that setup. Add the programs. Start them minimized if you want. **Launch Apps** and **Close Apps** are on that same window. **Save to Scene Collection** keeps this with the collection you are on. **Export Settings Backup** writes a JSON file if you move machines.

<p align="center">
  <img width="462" height="792" alt="Kaleido Launcher" src="https://github.com/user-attachments/assets/3b54200e-439d-49ed-b2a8-c3aff30c633d" />
</p>

- Windows x64
- Designed for **OBS Studio 32.2.2**
- Window stays **460×760**

## Install

1. Download the latest installer or ZIP from [Releases](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest).
2. **Installer** — run [KaleidoLauncher_v1.0.10_Installer.exe](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest). It installs into your 64-bit OBS folder (`C:\Program Files\obs-studio`).
3. **ZIP** — extract [kaleido-launcher-1.0.10-win64.zip](https://github.com/KaleidoVR/OBS-App-Launcher/releases/latest) over that same OBS folder. The archive opens at `obs-plugins`.
4. Restart OBS, then open **KaleidoVR → App Autostarter** in the top menu bar.

A later installer upgrades the existing Kaleido Launcher Apps & Features entry instead of adding a second one.

## Requirements

Windows x64 and a 64-bit OBS Studio install. I built this against **OBS Studio 32.2.2**.

There is no macOS or Linux build. The DLL belongs in `obs-plugins\64bit` next to OBS itself, not under `%ProgramData%`.

## Usage

1. Open **KaleidoVR → App Autostarter**.
2. **Select Profile** for that stream setup. **New Profile** adds one. **Delete Profile** removes the one you are on. Default Profile cannot be deleted.
3. **+ Add App** picks an `.exe`. Per app, **Start Minimized** is on that row. **- Remove App** drops the selected row.
4. **Enable Automatic Startup** launches that profile when OBS starts. **Close apps automatically when OBS exits** closes them when OBS quits.
5. **Launch Apps** and **Close Apps** use the profile you have selected, including apps that were already running.
6. **Save to Scene Collection** writes this into the collection you are on.

Switching OBS scene collections reloads an open launcher window. Changing profiles saves the list you just left.

### Profiles

Each profile is its own app list. Switch profiles when you change shows instead of rebuilding the list every time.

### Scene collections

I store this under a Kaleido key in the scene collection, so it travels with that collection and does not spill into OBS's other collection data.

### Backup

**Export Settings Backup** writes a JSON file of your profiles and app paths. **Import Settings Backup** asks first, then replaces what you have. Only import a backup you trust. Paths are whatever was on that PC, so you may need to re-add apps if their folders differ.

## Credits

Created and maintained by **KaleidoVR**.

- [kalivr.com](https://kalivr.com)
- [Discord](https://discord.com/invite/cRsufJssTA)

## License

[GNU GPL v2 or later](LICENSE) — Copyright (C) 2026 Kaleido VR
