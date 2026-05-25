# Contributing to BadBlood-Revival

Pull requests are welcome — this project survives on community contributions.

## Before you start

1. **Read the issue tracker** to make sure someone isn't already working
   on the same thing.
2. **Open an issue** describing what you intend to change *before* writing
   a lot of code. Especially for new features. This avoids the awkward
   situation where a big PR has to be rejected for scope/direction reasons.
3. **One topic per PR.** A PR that fixes a bug AND adds a feature AND
   reformats half the codebase is hard to review. Split it.

## What we want

Areas with a clear "help wanted" sign on them:

- **Leaderboard format reverse engineering** — figure out the missing
  field(s) in the index payload so the in-game leaderboard stops showing
  "Waiting for data" forever.
- **Player stats format** — `playerdata.stats` array shape is unknown.
- **Mesh-level Player_09 glove overlay** — needs Chrome Engine 6 .msh
  editing, removes the leather glove overlay that hides custom colors.
- **32-bit MinGW build for the x86 EAC stub** — current build script
  produces an x64 DLL with an x86 name.
- **PE section expansion** to lift the 12-character hostname constraint
  in the patcher.
- **Runtime config** for the launcher and texture-hook (read
  `launcher.cfg` / `texture_hook.cfg` instead of compile-time `#define`s).
- **Min-players hardcode** in `gamedll_x64_rwdi.dll` — reverse engineer
  and patch so you can start matches with fewer than 6 players.
- **Real Discord rich presence** — `discord-rpc.dll` is proxied, we just
  don't drive it.
- **Tests** for the server endpoints (currently zero unit tests).

## What we don't want

- Bundled Techland binaries, assets, or data files. **Ever.** If a PR
  adds `*.pak`, `*.rpack`, patched `engine_x64_rwdi.dll`, or anything
  derived from Techland's IP, it will be rejected and the PR will be
  closed. This is a hard rule for legal reasons — see [LICENSE](LICENSE)
  and [NOTICE](NOTICE).
- Code that requires non-free dependencies (commercial libraries,
  closed-source tools).
- Cheats, aimbots, ESP, wallhacks, or any feature whose purpose is to
  give a player unfair multiplayer advantage. The project is about
  game preservation, not about ruining matches.
- Anti-cheat detection bypasses targeting other games or services.
- Cryptocurrency, NFT, ad, or telemetry-monetization integrations.

## How to submit a PR

1. **Fork the repo** to your own GitHub account.
2. **Create a branch** off `main` with a descriptive name:
   `fix/leaderboard-index-field`, `feat/discord-rich-presence`, etc.
3. **Make your changes.** Keep them focused on the one thing you're
   doing. Don't reformat unrelated files.
4. **Test locally** — at minimum, the patcher should still apply
   cleanly to a vanilla engine, the server should start without errors,
   and the launcher should build with `build.bat`.
5. **Preserve attribution.** Per Apache License 2.0 Section 4(c), any
   modified file must keep the existing copyright notices and attribution
   comments. Add your own copyright line if you want — don't remove existing ones.
6. **Update docs** if you change behaviour. README, the relevant
   component README, and `docs/known-issues.md` are usually the targets.
7. **Open the PR** against `main`. Describe what changed and why.
   Reference the issue you opened in step 2.

## Code style

No formal style guide. Match what's already in the file you're touching.
General preferences:

- **Python:** PEP 8-ish, 4 spaces, double-quoted strings unless single
  is genuinely clearer. Type hints welcome but not required.
- **C++:** 4 spaces, opening brace on same line, no exceptions. The
  existing code is plain C-with-classes, not "modern C++."
- **Markdown:** 80-column soft wrap. Tables welcome. No emoji-heavy
  decoration in headers.
- **Commit messages:** imperative mood ("fix X" not "fixed X" or "fixes X").
  First line under 70 chars. Body wrapped at 72. Reference issues with
  `Fixes #N` or `Refs #N` in the body.

## License of contributions

By submitting a contribution, you agree it will be released under the
**Apache License 2.0** (the project's license — see [LICENSE](LICENSE)).
Your copyright stays yours; the license grant is what's needed for the
project to redistribute your contribution.

You also agree your contribution may be acknowledged in [NOTICE](NOTICE)
and/or `docs/credits.md`. If you'd prefer not to be credited by your
real name, mention your preferred handle/pseudonym in the PR.

## Maintainership

MrPie is the project lead and merges PRs. There is no formal CLA
(Contributor License Agreement) — the license grant is implicit in the
Apache 2.0 terms.

If MrPie becomes unresponsive (no activity for several months and
critical PRs piling up), community maintainers may be added. The "canonical"
repo at <https://github.com/ThePie88/BadBlood-Revival> remains the
reference; community forks are normal and encouraged, but their changes
should flow back here via PR when possible.

## Bug reports

Open an issue with:

- What you tried to do
- What actually happened
- Steps to reproduce
- Game version, engine SHA-256, server version
- Logs: `server/pls-emu.log`, `launcher` output, browser console for the website

Don't open issues for "the game won't start" without telling us what
specifically went wrong — there are a dozen reasons that can happen.

## Code of conduct

Be respectful. No personal attacks, no harassment, no slurs. Disagree
about technical decisions all you want; don't make it personal.

Maintainers reserve the right to close issues/PRs and block users who
violate this.

## Questions?

Open an issue with the `question` label, or ping MrPie on the GitHub repo.
