# Desktop Pet

A desktop pet for Windows that walks around the desktop and on top of your windows. Pets stand on window edges; move or close a window and they fall and land somewhere else.

- 2D sprite and 3D (glTF) characters, several at once
- Optional features (all off by default): AI chat (Sign in with ChatGPT), screen watching (the pet occasionally looks at the screen and makes a remark), reading aloud (Windows voices or a local TTS engine)
- Korean and English, light and dark

[한국어](README.md)

## Install

1. Download `DesktopPet-<version>-win-x64.zip` from the release page.
2. (Recommended) Check that the file matches the release SHA-256. In PowerShell:
   ```powershell
   Get-FileHash .\DesktopPet-0.1.0-win-x64.zip -Algorithm SHA256
   ```
   Compare the result with the `.sha256` file of the same name and the value in the release notes.
3. Extract it anywhere and run `DesktopPet\desktop-pet.exe`. There is no installer (portable).
4. The executable is not code-signed, so **Windows SmartScreen** may show "Windows protected your PC" the first time. If you have checked the source and SHA-256, choose **More info → Run anyway**.

**Requirements:** Windows 11 x64 and a Direct3D 11 GPU. (It may work on Windows 10 2004 or later, but this has not been tested.) No separate runtime is needed.

## First run

- The release does not include characters. Download the sample character **Windowling** from the release (`DesktopPet-character-windowling-*.zip`) and drop it on the **Characters…** window (tray menu); it is checked, installed and shown right away. You can also choose which characters and how many pets to show there.
- The app lives in the notification area (tray). A welcome window explains the basics the first time (open it again with **Help** in the tray menu).

## Controls

| Action | Result |
|---|---|
| Drag | Pick a pet up and put it somewhere else |
| Click | The pet reacts |
| Right-click a pet | Pet menu (chat, change character, remove, hide) |
| Tray icon | Full menu (pets, AI chat, screen watching, reading aloud, characters, settings, help, exit) |

Change settings in **Settings…** (language, theme, pet size, AI, screen watching, voice, full-screen exceptions). Advanced values can be edited in `config.toml` with **Open settings file** in the settings window.

## AI chat (optional)

- Turn it on with **AI chat → Use AI chat (Sign in with ChatGPT)** in the tray menu, then sign in with **Continue with ChatGPT** in the chat window. Finish the OpenAI sign-in and consent in your browser and you return to the app.
- Chats use **your ChatGPT plan's usage** (the app itself has no charges). Check usage with **Manage usage** in the chat window.
- Sign-in data (tokens) is stored on this PC encrypted with Windows DPAPI. Chat text is kept in memory only, never written to disk or logs.

## Screen watching (optional)

- When you turn it on with **Screen watching → Turn on screen watching…**, a consent window shows what is sent; it turns on only after you tick the box.
- While on, now and then (by default every 15–30 minutes, at most twice an hour) one downscaled image of the monitor the pet is on is sent to OpenAI through your signed-in ChatGPT account to get a short remark. Images and raw responses are not stored.
- It does not look while the screen is locked, in full screen, while you are away, while chatting, or when an **excluded app** (password managers, messengers, …, matched by executable name) is visible on that monitor. Note that web messengers or password extensions inside a browser cannot be excluded by executable name.
- Turn it off or pause it at any time (a pause survives restarts). If you widen its scope in the settings after consenting (frequency, image size, excluded apps, …), it turns off and asks for consent again.

## Reading aloud (optional)

Turn on **Read pet speech aloud** in the tray menu to read what pets say with Windows voices. You can also connect a local AI TTS engine you installed — [Local TTS](docs/local_tts_ko.md) (Korean).

## Making characters

A character is a folder (package) with a `manifest.toml`. See [ADR-13 Character package](docs/adr/ADR-13-character-package.md) (Korean) and the examples `characters/windowling` (2D, the sample character) and `characters/test-fox` (3D) in the repository. `pack-check.exe` in the release zip checks a package with the same rules as the app:

```powershell
.\pack-check.exe C:\my-characters\my-cat
```

Installation checks zip paths, links, sizes and scripts (Lua sandbox). Avoid installing character packages from unknown sources.

## Data and privacy

- Data location: `%LOCALAPPDATA%\DesktopPet` (settings, characters, logs, sign-in data, state files). Logs older than 14 days are deleted.
- What the app sends over the network:
  - When you use AI chat: the conversation → OpenAI (Sign in with ChatGPT).
  - When screen watching is on: what is described above → OpenAI.
  - Update check (on by default, once a day): a request for `latest.json` on GitHub releases (GitHub sees your IP address and the app version). Turn off **Check for new versions** in the settings to stop it. The app only tells you about a new version; you download it yourself from the release page.
  - Nothing else — no usage statistics or telemetry.
- Local TTS connects only to an engine on this PC (127.0.0.1).

## Uninstall

1. Tray **Settings… → General & display → Delete all data…** signs out (revokes the token), removes the startup registration, deletes the data folder, and exits.
2. Delete the extracted `DesktopPet` folder.

If the app cannot run, delete `%LOCALAPPDATA%\DesktopPet` and, if you turned on startup, the `DesktopPet` value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (or turn it off in **Settings → Apps → Startup**).

## Reporting problems and security

- Bugs and suggestions: Issues in the repository.
- Security problems: please report them privately as described in [SECURITY.md](SECURITY.md), not in a public issue.

## License

Code: [MIT License](LICENSE). Licenses of the libraries used: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) (full texts in `licenses\` of the release zip). Character packages have their own licenses (`[license]` in `manifest.toml`).

Use of Sign in with ChatGPT is subject to OpenAI's [Sign in with ChatGPT Terms](https://openai.com/policies/sign-in-with-chatgpt-terms/). This project is not affiliated with OpenAI.
