# Security / 보안

## Reporting a vulnerability / 취약점 신고

Please do **not** open a public issue for security problems. Use GitHub's private vulnerability reporting: the repository's **Security** tab → **Report a vulnerability**. Include the version (tray menu shows it), steps to reproduce, and the impact you expect.

보안 문제는 공개 이슈로 올리지 말고, 저장소의 **Security** 탭 → **Report a vulnerability** (비공개 신고)로 알려 주세요. 버전(트레이 메뉴에 표시), 재현 방법, 예상되는 영향을 적어 주세요.

## Scope / 범위

In scope / 대상:
- The app (`desktop-pet.exe`) and `pack-check.exe` from the release zip
- Character package handling (installation checks, Lua sandbox, glTF/image parsing)
- Sign in with ChatGPT handling (tokens, callback), screen watching privacy guards, local TTS connection, update check

Out of scope / 대상 아님:
- Problems that need an attacker who can already run code as your Windows user, or edit `config.toml` (the settings file can point the TTS command engine at any program by design)
- Character packages you install yourself from untrusted sources doing what their persona or behavior script says within the sandbox

## Design notes / 설계 메모

See [docs/security_review_ko.md](docs/security_review_ko.md) (Korean) for the threat model, past findings and their fixes.
