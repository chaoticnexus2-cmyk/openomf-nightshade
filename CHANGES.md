# Changes vs. vanilla OpenOMF

This fork is based on OpenOMF `master` (version 0.8.6-303, commit `6d64e331`).
Everything below is additive or a small, isolated engine tweak — the core
gameplay engine is unchanged.

## Engine source changes

These are the only modifications to existing engine files:

### `src/video/renderers/opengl3/sdl_window.c` — borderless fullscreen
- `create_window()` and `resize_window()` now request
  `SDL_WINDOW_FULLSCREEN_DESKTOP` instead of `SDL_WINDOW_FULLSCREEN`.
- **Why:** the original forced a hardware display-mode switch to the configured
  resolution, which fails or letterboxes on resolutions the monitor doesn't
  natively offer. Desktop fullscreen fills the screen at the native resolution
  and lets the renderer scale the internal 320×200 framebuffer — so fullscreen
  works regardless of the configured resolution.

### `src/game/scenes/vs.c` — player name in tournament taunts
- In tournament mode the opponent's pre-fight quote now substitutes the token
  `~1` with the player's chosen pilot name (trailing spaces trimmed).
- **Why:** lets authored tournament dialog address the player by name.

### `src/game/scenes/cutscene.c` — player name in cutscene story
- New helper `cutscene_set_chr_text()` substitutes `~1` with the player's pilot
  name on every victory-cutscene page.
- **Why:** the authored ending story reads naturally with the player's name and
  avoids any hardcoded hero or gendered pronouns.

### `CMakeLists.txt` — expansion build option
- Added `BUILD_EXPANSION` (default **OFF**). When ON, builds two helper tools
  (`expansion_gen`, `mkpilot`) that link the engine's own format library. The
  normal game/tool build is unaffected when OFF.

## New content & files (all additive)

| Path | Purpose |
|------|---------|
| `tools/expansion/gen.c` | Generates the two tournament `.TRN` files using the engine's own `sd_tournament_save()`, with the original boss-first roster, mechs, father-revenge story, and title-banner logos baked into the tournament sprite/palette. |
| `tools/expansion/mkportrait.c` | Appends the original Nightshade Concord faces (none reused from the base game) to a copy of `WORLD.PIC`, producing `NIGHTSHD.PIC` via the engine's own `sd_pic_save()`. |
| `tools/expansion/vance.h` | Single source of truth for the antagonist cast → PIC face index → source-art filename mapping (shared by `gen.c` and `mkportrait.c`). |
| `tools/expansion/mkpilot.c` | Builds a ready-to-play test pilot `.CHR` (maxed Jaguar, 200,000 cr) from scratch, pulling a real portrait from `PLAYERS.PIC`. |
| `tools/expansion/hdmod_pack.py` | Converts the upstream HD Remaster RGBA assets to the indexed PNG format this engine accepts, and packages a loadable mod zip. |
| `expansion-art/` | Title-banner source art + `quantize_logo.py`, plus `portraits/` (the hand-generated antagonist faces) + `quantize_portrait.py` (RGB → engine-ready `.vph`). |
| `build-expansion.sh` | One-command: build tools → bake portraits into `NIGHTSHD.PIC` → generate tournaments → package HD mod → install test pilot. |
| `play.sh` | Convenience launcher. |
| `README.md` / `EXPANSION-KYRA.md` | Fork overview and detailed expansion design notes. |

## Generated artifacts (not committed)

These are produced by `build-expansion.sh` into the (git-ignored) `build/` tree
and the user's save directory:

- `build/resources/KYRA.TRN` — *Nightshade Concord* tournament.
- `build/resources/RECKON.TRN` — *Iron Reckoning* tournament.
- `build/resources/NIGHTSHD.PIC` — `WORLD.PIC` copy with the original antagonist faces appended.
- `build/mods/openomf-hdremaster.zip` — packaged HD textures (optional).
- `~/Library/Application Support/OpenOMF/save/CHAMPION.CHR` — test pilot.

## Design notes

- **No new binary formats authored by hand.** Tournaments reuse an original
  tournament as a binary template (inheriting valid palette and `.BK` binding);
  only the roster, text, palette range (128–167) and logo sprite are rewritten,
  then saved with the engine's own writer. Portraits are likewise appended to a
  real `WORLD.PIC` and written back with `sd_pic_save()`.
- **Original antagonist cast, no reused faces.** The Concord's masked enforcers
  each get a hand-generated portrait injected at PIC face index 50+, safely
  above `WORLD.PIC`'s ~44 real faces.
- **Correct tournament flow.** Rosters are authored **boss-first** so the engine's
  array-order ranking puts the boss at rank 1 (fought last), and bosses are
  ordinary (non-secret) pilots so the victory cutscene actually fires when they
  fall. See EXPANSION-KYRA.md → "Roster ordering".
- **Tournaments auto-load.** The engine globs `*.TRN` in the resources dir, so
  no recompile is needed to add or change tournaments.
- **Balancing.** Enemy stats are capped within the World-Championship envelope
  (arm power ≤ 6, bosses ≤ 7, difficulty ≤ 2) so the post-championship
  tournaments are challenging but beatable.
