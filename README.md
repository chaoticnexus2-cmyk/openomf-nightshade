OpenOMF — Nightshade Edition
============================

A **story expansion fork** of [OpenOMF](https://github.com/omf2097/openomf), the
open-source remake of *One Must Fall: 2097*. Nightshade Edition adds two new
narrative tournaments, optional HD textures, quality-of-life fixes, and a
ready-to-play test pilot — all on top of the stock OpenOMF engine.

> This is an unofficial, **non-profit fan project**. It is not affiliated with
> or endorsed by the OpenOMF team, Diversions Entertainment, or Epic MegaGames.
> See [Credits & License](#credits--license).

![Flail vs Gargoyle](/docs/flail.png)

---

## What's new vs. vanilla OpenOMF

| Area | Addition |
|------|----------|
| **Story** | Two brand-new single-player tournaments with an original revenge plot (see below) |
| **Cast** | The classic OMF pilots recast as a sinister fight-cult, plus a new villain, **Vance** |
| **Player-driven narrative** | Story text uses *your* pilot's name and is fully gender-neutral |
| **Custom art** | Hand-generated title banners for each tournament |
| **HD textures** | Optional integration of the community [HD Remaster mod](https://github.com/omf2097/openomf-hdremaster-mod) |
| **QoL** | True borderless fullscreen at any resolution |
| **Tooling** | A reproducible generator + a "drop-in" test pilot so you can play the new content immediately |

A full technical changelog lives in **[CHANGES.md](CHANGES.md)**.

---

## The Story

You play a pilot hunting the people who murdered your mentor. The narrative is
told the OMF way — through each opponent's pre-fight taunt and a multi-page
victory cutscene — and every line uses **your chosen pilot name** (no hardcoded
hero, no assumed gender).

### Tournament 1 — *Nightshade Concord*
Beneath the legitimate circuit festers the **Nightshade Concord**, a fight-cult
that rigs arenas and answers to a masked king named **Vance**. Climb through his
enforcers — the familiar OMF fighters, recast as coerced or corrupt members —
and uncover who really ordered the killing. Entry fee: **15,000 cr**.

Roster (escalating): Cossette · Milano · Jean-Paul · Christian · Angel ·
Ibrahim · Shirro · Raven · **Vance** (gated final boss).

### Tournament 2 — *Iron Reckoning*
The Concord is broken, but a patron survives it — a financier "in a green
machine" who bankrolled every shadow you cut down. The survivors return in
deadlier prototype mechs for the circuit's hardest gauntlet. Entry fee:
**25,000 cr**.

Roster: the same cast in new HARs, plus a hidden patron tied to the original
game's legacy as the secret final boss.

Both tournaments are tuned to the post-World-Championship power band — hard, but
beatable with an upgraded HAR.

---

## How to access the new content

### 1. Build (macOS / Linux)
Install dependencies (see [BUILD.md](BUILD.md)), then:
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
cd ..
```
You also need the freeware OMF:2097 data files (see [Game data](#game-data)).

### 2. Install the expansion
One command builds the expansion tools, generates the tournaments, packages the
optional HD mod, and drops in a ready-to-play test pilot:
```bash
./build-expansion.sh
```

### 3. Play
```bash
./play.sh
```
- **Load Game → CHAMPION** — a pre-made pilot with a fully-upgraded Jaguar and
  200,000 cr, ready to enter either tournament. (Or start a new pilot and name
  them anything — the story adapts to the name.)
- **Mechlab → Select Tournament** — choose **Nightshade Concord** or
  **Iron Reckoning**.
- Win every ranked fight to trigger the gated boss, then advance the victory
  cutscene with **Punch / Kick**.

### Fullscreen
Main Menu → Configuration → Video → **Fullscreen On** (enable *aspect ratio* to
avoid stretching). This fork uses borderless-desktop fullscreen, so it fills the
screen at any resolution.

---

## Game data

OpenOMF loads the original *One Must Fall: 2097* data files. OMF:2097 is
freeware; download the assets from
[omf2097.com](https://www.omf2097.com/pub/files/omf/openomf-assets.zip) and
place the `OMF2097/` contents in your resources directory. See
[PATHS.md](PATHS.md). These files are **not** redistributed in this repository.

---

## Credits & License

This fork stands entirely on the work of others:

- **OpenOMF** — the engine this builds on, by Tuomas Virtanen, Andrew Thompson,
  Hunter, and contributors. <https://github.com/omf2097/openomf>
- **One Must Fall: 2097** — the original game by **Diversions Entertainment**,
  published by Epic MegaGames (1994), now freeware.
- **HD Remaster assets** — the community
  [openomf-hdremaster-mod](https://github.com/omf2097/openomf-hdremaster-mod)
  (optional; fetched at build time, not redistributed here).

The original upstream README is preserved as
[README.upstream.md](README.upstream.md).

**License:** MIT, the same as upstream OpenOMF — see [LICENSE](LICENSE). This is
a **non-profit** fan project; it is distributed free of charge and must not be
sold. Bundled third-party components keep their own licenses (see the upstream
README). The new expansion content, tools, and documentation added by this fork
are released under the same MIT terms.
