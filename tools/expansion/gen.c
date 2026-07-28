/** @file gen.c
 * @brief OMF 2097 "Kyra" Expansion Pack tournament generator.
 *
 * Builds two new tournament (.TRN) files for OpenOMF by loading an original
 * tournament as a binary template (so the logo sprite, palette and the BK
 * cutscene / PIC photo bindings remain valid), then overwriting the roster,
 * mechs (HARs), pre-fight taunts, descriptions and the victory cutscene story.
 *
 * Output is byte-correct because it uses OpenOMF's own sd_tournament_save().
 *
 * Usage: expansion_gen <resource_dir> <output_dir>
 * @license MIT
 */

#include "formats/error.h"
#include "formats/pilot.h"
#include "formats/sprite.h"
#include "formats/tournament.h"
#include "formats/vga_image.h"
#include "game/common_defines.h"
#include "utils/allocator.h"
#include "utils/c_string_util.h"
#include "utils/path.h"
#include "vance.h"
#include "video/vga_palette.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- small helpers --------------------------------------------------------

static void set_str(char **dst, const char *s) {
    if(*dst) {
        omf_free(*dst);
    }
    *dst = s ? omf_strdup(s) : NULL;
}

// Describes one enemy fighter in a tournament.
//
// IMPORTANT ordering note: the engine (sd_chr_from_trn in formats/chr.c) assigns
// each non-secret enemy a rank sequentially by ARRAY ORDER -- roster[0] becomes
// rank 1, roster[1] rank 2, and so on. The player then climbs DOWN toward rank 1
// (mechlab_next_opponent picks rank == player_rank - 1; arena.c decrements the
// winner's rank), and the victory cutscene only fires when the player beats the
// rank-1 pilot while at rank 2 (newsroom.c "champion" check). Therefore the
// FINAL BOSS must sit at roster[0] as a NORMAL (non-secret) rank-1 pilot, and the
// weakest, first-fought enemy must be the LAST array element. Secret/gated bosses
// (rank 0) can never trigger the ending cutscene, so they are not used here.
typedef struct {
    const char *name; // display name (<= 17 chars)
    int pilot_id;     // engine logic key (kept off PILOT_KREISSACK to avoid the gate)
    int photo_id;     // face index into the tournament PIC, so the portrait matches the name
    int har_id;       // which mech (HAR_*)
    int difficulty;   // 0..3
    int power, agility, endurance; // 1..25
    int arm_power, leg_power, arm_speed, leg_speed, armor, stun; // 0..9
    int rank;         // informational only; the engine re-ranks by array order (see note above)
    int secret;       // gated boss (only appears after requirements met) -- unused for our bosses
    int req_rank;     // appears when player reaches this rank (for secret bosses)
    const char *quote; // pre-fight taunt (advances the plot)
} enemy_spec;

// Overwrite a pilot slot with our data, preserving the template's photo_id,
// palette and photo sprite so the in-game pilot photo stays valid.
static void apply_enemy(sd_pilot *p, const enemy_spec *e) {
    snprintf(p->name, sizeof(p->name), "%s", e->name);
    // Never use PILOT_KREISSACK (10): the engine special-cases that id with a
    // Veteran-difficulty gate and a canned insult, which would override our
    // custom quote. Remap it to a normal pilot id.
    p->pilot_id = (uint8_t)((e->pilot_id >= PILOT_KREISSACK) ? PILOT_RAVEN : e->pilot_id);
    p->har_id = (uint8_t)e->har_id;
    // Portrait face from WORLD.PIC (set as the tournament pic_file), so the
    // shown face matches the character's name.
    p->photo_id = (uint16_t)e->photo_id;
    p->difficulty = (uint8_t)(e->difficulty & 0x3);
    p->power = (uint8_t)e->power;
    p->agility = (uint8_t)e->agility;
    p->endurance = (uint8_t)e->endurance;
    p->arm_power = (uint8_t)e->arm_power;
    p->leg_power = (uint8_t)e->leg_power;
    p->arm_speed = (uint8_t)e->arm_speed;
    p->leg_speed = (uint8_t)e->leg_speed;
    p->armor = (uint8_t)e->armor;
    p->stun_resistance = (uint8_t)e->stun;
    p->rank = (uint8_t)e->rank;
    p->wins = (uint16_t)(40 - e->rank * 2);
    p->losses = (uint16_t)(e->rank);
    p->money = 5000 + (20 - e->rank) * 1500;

    // Offense/defense preference (100 baseline, higher = more aggressive).
    p->offense = (uint16_t)(110 + (20 - e->rank) * 4);
    p->defense = (uint16_t)(110 + (20 - e->rank) * 3);

    // AI attitude / behaviour, scaled by how far up the ladder they are.
    int aggr = 80 + (20 - e->rank) * 12;
    p->att_normal = 60;
    p->att_hyper = (uint8_t)(20 + (20 - e->rank) * 3);
    p->att_jump = 30;
    p->att_def = 40;
    p->att_sniper = 25;
    p->ap_throw = (int16_t)(aggr / 2);
    p->ap_special = (int16_t)aggr;
    p->ap_jump = 60;
    p->ap_high = 80;
    p->ap_low = 80;
    p->ap_middle = 80;
    p->pref_jump = 40;
    p->pref_fwd = (int16_t)(aggr);
    p->pref_back = 20;
    p->learning = (float)(2 + (20 - e->rank) * 0.5);
    p->forget = 1.0f;

    // Gating for secret bosses.
    p->secret = (uint8_t)e->secret;
    p->only_fight_once = 0;
    p->req_rank = (uint8_t)e->req_rank;
    p->req_max_rank = 0;

    // Pre-fight taunt (locale 0).
    set_str(&p->quotes[0], e->quote);
}

// Ensure the tournament has exactly `count` enemy slots, cloning from existing
// template pilots for any new slots so photo/palette data stays valid. Then
// apply our specs over each slot.
static void build_roster(sd_tournament_file *trn, const enemy_spec *specs, int count) {
    int base = trn->enemy_count;
    if(base <= 0) {
        fprintf(stderr, "template tournament has no enemies to clone\n");
        exit(1);
    }
    // Grow if needed (clone a valid template pilot for photo/palette).
    for(int i = base; i < count; i++) {
        trn->enemies[i] = omf_calloc(1, sizeof(sd_pilot));
        sd_pilot_create(trn->enemies[i]);
        sd_pilot_clone(trn->enemies[i], trn->enemies[i % base]);
    }
    // Shrink if needed.
    for(int i = count; i < base; i++) {
        if(trn->enemies[i]) {
            sd_pilot_free(trn->enemies[i]);
            omf_free(trn->enemies[i]);
            trn->enemies[i] = NULL;
        }
    }
    trn->enemy_count = (uint16_t)count;

    for(int i = 0; i < count; i++) {
        apply_enemy(trn->enemies[i], &specs[i]);
    }
}

// Clear all victory/story text pages in locale 0, then set the given pages.
static void set_story(sd_tournament_file *trn, const char *const *pages, int n) {
    sd_tournament_locale *loc = trn->locales[0];
    for(int har = 0; har < 11; har++) {
        for(int page = 0; page < 10; page++) {
            if(loc->end_texts[har][page]) {
                omf_free(loc->end_texts[har][page]);
                loc->end_texts[har][page] = NULL;
            }
        }
    }
    for(int page = 0; page < n && page < 10; page++) {
        loc->end_texts[0][page] = omf_strdup(pages[page]);
    }
}

static void finalize_locale(sd_tournament_file *trn, const char *title, const char *description) {
    sd_tournament_locale *loc = trn->locales[0];
    set_str(&loc->title, title);
    set_str(&loc->description, description);
    if(loc->stripped_description) {
        omf_free(loc->stripped_description);
        loc->stripped_description = NULL;
    }
    parse_tournament_description(loc);
}

// Replace the tournament logo with a generated raw logo file (.lgo) produced by
// expansion-art/quantize_logo.py. Format: <u16 w><u16 h><40*3 palette RGB><w*h
// index bytes>. Index 0 is transparent; opaque colours use indices 1..39. Those
// 40 palette entries are loaded into the tournament palette range (128..167) and
// the sprite pixels are remapped to match, so the logo renders correctly on the
// tournament-select screen. Reads raw bytes directly to avoid libpng in the
// standalone tool.
static int set_logo_from_lgo(sd_tournament_file *trn, const char *lgo_path, int pos_x, int pos_y) {
    FILE *f = fopen(lgo_path, "rb");
    if(!f) {
        fprintf(stderr, "  WARN: cannot open logo %s (keeping template logo)\n", lgo_path);
        return 1;
    }
    uint8_t hdr[4];
    uint8_t pal[120];
    if(fread(hdr, 1, 4, f) != 4 || fread(pal, 1, 120, f) != 120) {
        fprintf(stderr, "  WARN: short header in %s\n", lgo_path);
        fclose(f);
        return 1;
    }
    int w = hdr[0] | (hdr[1] << 8);
    int h = hdr[2] | (hdr[3] << 8);
    size_t n = (size_t)w * (size_t)h;

    sd_vga_image img;
    if(sd_vga_image_create(&img, w, h) != SD_SUCCESS) {
        fclose(f);
        return 1;
    }
    if(fread(img.data, 1, n, f) != n) {
        fprintf(stderr, "  WARN: short pixel data in %s\n", lgo_path);
        fclose(f);
        sd_vga_image_free(&img);
        return 1;
    }
    fclose(f);

    // Load the 40 palette entries into the tournament palette range 128..167.
    // (index 0 is the transparent marker and is never drawn.)
    for(int k = 0; k < 40; k++) {
        trn->pal.colors[128 + k].r = pal[k * 3 + 0];
        trn->pal.colors[128 + k].g = pal[k * 3 + 1];
        trn->pal.colors[128 + k].b = pal[k * 3 + 2];
    }
    // Remap pixel indices: 0 stays transparent; 1..39 -> 129..167.
    for(size_t p = 0; p < n; p++) {
        uint8_t v = (uint8_t)img.data[p];
        if(v != 0) {
            img.data[p] = (char)(128 + v);
        }
    }

    // Re-encode the locale 0 logo sprite from our image.
    sd_sprite *logo = trn->locales[0]->logo;
    sd_sprite_free(logo);
    sd_sprite_create(logo);
    if(sd_sprite_vga_encode(logo, &img) != SD_SUCCESS) {
        fprintf(stderr, "  WARN: failed to encode logo sprite\n");
        sd_vga_image_free(&img);
        return 1;
    }
    logo->pos_x = (int16_t)pos_x;
    logo->pos_y = (int16_t)pos_y;
    sd_vga_image_free(&img);
    printf("  logo replaced from %s (%dx%d at %d,%d)\n", lgo_path, logo->width, logo->height, pos_x, pos_y);
    return 0;
}

static int save_trn(sd_tournament_file *trn, const char *out_dir, const char *fname) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", out_dir, fname);
    path out;
    path_from_c(&out, buf);
    int ret = sd_tournament_save(trn, &out);
    if(ret != SD_SUCCESS) {
        fprintf(stderr, "FAILED to save %s: %s\n", buf, sd_get_error(ret));
        return 1;
    }
    printf("  wrote %s (%u enemies, bk=%s pic=%s id=%d fee=%d)\n", buf, trn->enemy_count, trn->bk_name, trn->pic_file,
           trn->tournament_id, trn->registration_fee);
    return 0;
}

static int load_base(sd_tournament_file *trn, const char *resource_dir, const char *fname) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", resource_dir, fname);
    path in;
    path_from_c(&in, buf);
    sd_tournament_create(trn);
    int ret = sd_tournament_load(trn, &in);
    if(ret != SD_SUCCESS) {
        fprintf(stderr, "FAILED to load template %s: %s\n", buf, sd_get_error(ret));
        return 1;
    }
    printf("loaded template %s: %u enemies, bk=%s pic=%s id=%d fee=%d\n", buf, trn->enemy_count, trn->bk_name,
           trn->pic_file, trn->tournament_id, trn->registration_fee);
    return 0;
}

// ===========================================================================
// TOURNAMENT 1 -- "Nightshade Concord"
// ===========================================================================
static int gen_nightshade(const char *resource_dir, const char *out_dir, const char *art_dir) {
    sd_tournament_file trn;
    if(load_base(&trn, resource_dir, "NORTH_AM.TRN"))
        return 1;

    // Keep NORTH_AM.BK cutscene from the template. Use NIGHTSHD.PIC for portraits:
    // it is a copy of WORLD.PIC with a brand-new hand-generated Vance face appended
    // (see tools/expansion/mkportrait.c). Each recast classic pilot still shows a
    // real OMF face; only Vance uses the new original portrait at photo index
    // VANCE_PHOTO_ID.
    sd_tournament_set_pic_name(&trn, "NIGHTSHD.PIC");
    trn.tournament_id = 11; // not 4 (4 = World Championship special-case)
    // Post-World-Championship content: fee/value sit just above WORLD (10000/58000).
    trn.registration_fee = 15000;
    trn.assumed_initial_value = 70000;
    trn.winnings_multiplier = 1.5f;

    // Concord enforcers, ordered BOSS-FIRST (roster[0] = rank 1 = fought last).
    // See the enemy_spec ordering note above. Stats are tuned to the World
    // Championship envelope (arm_power <= 6, boss <= 7, difficulty <= 2) so the
    // pack is challenging for a post-World pilot but never one-hit-kills.
    //
    // photo_id VANCE_PHOTO_ID points at the new original Vance portrait in
    // NIGHTSHD.PIC; every other pilot keeps a real WORLD.PIC face index.
    // The Nightshade Concord -- an original cast of masked fight-cult enforcers,
    // ordered BOSS-FIRST (roster[0] = The Cardinal = rank 1 = fought last).
    // Each photo_id points at a hand-generated original face in NIGHTSHD.PIC
    // (see enum concord_face in vance.h). Stats stay inside the World envelope
    // (arm_power <= 6, boss <= 7, difficulty <= 2) so it is hard but fair.
    static const enemy_spec roster[] = {
        {"The Cardinal", PILOT_STEFFAN, FACE_CARDINAL, HAR_NOVA, 2, 26, 16, 22, 6, 6, 6, 6, 6, 6, 1, 0, 0,
         "I am the Cardinal. Your mentor saw our faces and chose silence over life, ~1. Kneel, and I will let you "
         "keep your eyes."},
        {"Requiem", PILOT_RAVEN, FACE_REQUIEM, HAR_SHADOW, 2, 22, 16, 18, 5, 5, 6, 6, 5, 5, 2, 0, 0,
         "I do not gloat, ~1. I keep count. Your mentor was one hundred and nine. You will not get your own number."},
        {"Marrow", PILOT_SHIRRO, FACE_MARROW, HAR_SHREDDER, 2, 22, 8, 18, 5, 5, 4, 4, 5, 5, 3, 0, 0,
         "I opened your mentor to see what a hero is made of, ~1. Just meat and stubbornness. Hold still -- this is "
         "research."},
        {"Bastion", PILOT_IBRAHIM, FACE_BASTION, HAR_GARGOYLE, 1, 20, 6, 20, 5, 5, 3, 3, 6, 6, 4, 0, 0,
         "I am the wall the Concord stands behind, ~1. Everyone who reached for the truth broke on me. Reach."},
        {"Cinder", PILOT_ANGEL, FACE_CINDER, HAR_PYROS, 1, 18, 12, 15, 4, 4, 4, 4, 5, 4, 5, 0, 0,
         "They pay me in screams and firelight, ~1. Your mentor burned quiet. Let us hear if you are louder."},
        {"Seraph", PILOT_CHRISTIAN, FACE_SERAPH, HAR_KATANA, 1, 18, 8, 16, 4, 3, 3, 3, 5, 4, 6, 0, 0,
         "The Concord is the true order of the ring, ~1. Your grief is a prayer to the wrong god. Let me correct it."},
        {"Vesper", PILOT_JEANPAUL, FACE_VESPER, HAR_ELECTRA, 0, 17, 11, 14, 3, 3, 4, 4, 4, 3, 7, 0, 0,
         "I sold them the coordinates that killed your mentor, ~1. You walked in wearing that grudge like a badge. I "
         "see everything."},
        {"Wager", PILOT_MILANO, FACE_WAGER, HAR_JAGUAR, 0, 15, 18, 12, 2, 2, 5, 5, 3, 2, 8, 0, 0,
         "I already booked the odds on you, ~1 -- forty to one, dead by round two. Do not make a liar of me... or do. "
         "I hedged."},
        {"Sparrow", PILOT_COSSETTE, FACE_SPARROW, HAR_CHRONOS, 0, 14, 14, 12, 2, 2, 4, 4, 3, 2, 9, 0, 0,
         "They said if I put you down, I get a real mask, ~1. A real name. Nothing personal -- I just want to be "
         "someone."},
    };
    build_roster(&trn, roster, (int)(sizeof(roster) / sizeof(roster[0])));

    finalize_locale(&trn, "Nightshade Concord",
                    "{WIDTH 276}{CENTER 160}{VMOVE 72}{COLOR 6}Your mentor -- the pilot who taught you the ring -- "
                    "vanished after refusing the Nightshade Concord, the masked syndicate that quietly decides every "
                    "champion. Climb their enforcers to the Cardinal. Entry: 15,000cr.");

    // Generated title banner (see expansion-art/). Falls back to the template
    // logo if the art is missing.
    if(art_dir) {
        char logo[512];
        snprintf(logo, sizeof(logo), "%s/nightshade_logo.lgo", art_dir);
        set_logo_from_lgo(&trn, logo, 10, 4);
    }

    // Victory cutscene -- second person and gender-neutral; ~1 = player name.
    // Pages are kept short so they fit the cutscene text area. The father's
    // squadron pin introduced here is the prop that threads into Iron Reckoning.
    static const char *const story[] = {
        "The Cardinal's NOVA folds to one knee, venting coolant like a last breath. The crowd that paid to watch you "
        "die goes silent, ~1.",
        "You climb to the cockpit and pull the bone-white mask free. Beneath it: a face from your mentor's old squad "
        "photos -- a friend who chose the Concord.",
        "\"We built this to end the wars,\" the Cardinal rasps. \"Then we learned it was easier to own the ring than "
        "to save it. Your mentor wouldn't look away.\"",
        "You do not strike. You take the Concord signet from the collar -- proof, names, dates. \"You buried a "
        "witness,\" you say. \"I'm the receipt.\"",
        "By dawn you broadcast the Concord's ledgers to every feed: the fixed bouts, the vanished pilots, the graves "
        "with no names. Your mentor's is the first cleared.",
        "But every order in the ledger routes upward, to one unsigned hand -- an old-war architect the Cardinal only "
        "calls the Meridian.",
        "You seal the signet into your cockpit, over the throttle where a better pilot's hand once rested. The "
        "syndicate is exposed, ~1. The reckoning has only begun.",
    };
    set_story(&trn, story, (int)(sizeof(story) / sizeof(story[0])));

    int rc = save_trn(&trn, out_dir, "KYRA.TRN");
    sd_tournament_free(&trn);
    return rc;
}

// ===========================================================================
// TOURNAMENT 2 -- "Iron Reckoning" (same characters, different mechs)
// ===========================================================================
static int gen_reckoning(const char *resource_dir, const char *out_dir, const char *art_dir) {
    sd_tournament_file trn;
    if(load_base(&trn, resource_dir, "WAR.TRN"))
        return 1;

    trn.tournament_id = 12;
    // Reuse the same portrait PIC as Nightshade so the recast pilots keep their
    // faces and Vance keeps the new original portrait; Kreissack uses a real
    // WORLD.PIC face index that still exists in the extended copy.
    sd_tournament_set_pic_name(&trn, "NIGHTSHD.PIC");
    // The hardest gauntlet -- sits at the top of the progression.
    trn.registration_fee = 25000;
    trn.assumed_initial_value = 90000;
    trn.winnings_multiplier = 1.8f;

    // Same Concord names -- new, deadlier prototype mechs -- ordered BOSS-FIRST
    // (roster[0] = Kreissack = rank 1 = fought last). Stats are a notch above
    // Nightshade but still capped within the World Championship envelope
    // (arm_power <= 6, boss <= 7; difficulty <= 2) so it stays beatable.
    //
    // Raven flies ELECTRA here (not NOVA) so Kreissack's green NOVA stays the
    // unique "machine in a green machine" reveal and no two fighters share it.
    // Same original cast -- deadlier prototype mechs -- ordered BOSS-FIRST
    // (roster[0] = Meridian, the true architect = rank 1 = fought last). Faces
    // reuse the Concord portraits in NIGHTSHD.PIC. Stats are a notch above
    // Nightshade but still inside the World envelope (arm_power <= 7; boss <= 7).
    static const enemy_spec roster[] = {
        {"The Meridian", PILOT_CRYSTAL, FACE_MERIDIAN, HAR_NOVA, 2, 28, 16, 24, 7, 6, 6, 6, 7, 7, 1, 0, 0,
         "You hunt the hand that signed the orders, ~1. I am that hand. I ended the wars, then priced the peace. Your "
         "mentor was a rounding error."},
        {"The Cardinal", PILOT_STEFFAN, FACE_CARDINAL, HAR_SHADOW, 2, 24, 18, 20, 6, 6, 6, 6, 6, 6, 2, 0, 0,
         "You unmasked me, ~1, so the Meridian rebuilt me sharper. Beat me clean this time -- it is the only apology "
         "I have left to give."},
        {"Requiem", PILOT_RAVEN, FACE_REQUIEM, HAR_ELECTRA, 2, 24, 16, 18, 6, 5, 6, 6, 6, 6, 3, 0, 0,
         "You cost me my count, ~1. I begin again at one. Electra will make sure the tally opens and closes with the "
         "same name."},
        {"Marrow", PILOT_SHIRRO, FACE_MARROW, HAR_THORN, 2, 22, 8, 18, 5, 5, 4, 4, 6, 5, 4, 0, 0,
         "I studied your mentor's wounds and improved on them, ~1. Thorn is my new scalpel. Let us compare our "
         "anatomies."},
        {"Bastion", PILOT_CHRISTIAN, FACE_BASTION, HAR_GARGOYLE, 2, 20, 6, 18, 5, 4, 3, 3, 6, 6, 5, 0, 0,
         "You cracked the wall once, ~1. The Meridian poured me thicker. Grief made you fast -- it will not make you "
         "strong enough twice."},
        {"Seraph", PILOT_ANGEL, FACE_SERAPH, HAR_KATANA, 1, 18, 14, 15, 5, 4, 5, 5, 5, 4, 6, 0, 0,
         "You martyred my brothers and made yourself a saint, ~1. Katana ends false gospels. Kneel and be edited."},
        {"Cinder", PILOT_JEANPAUL, FACE_CINDER, HAR_PYROS, 1, 18, 11, 15, 4, 4, 5, 5, 5, 4, 7, 0, 0,
         "You put out the Concord, ~1. You cannot put out me. Pyros will finish what your mentor's pyre started."},
        {"Wager", PILOT_COSSETTE, FACE_WAGER, HAR_FLAIL, 1, 17, 12, 14, 4, 4, 4, 4, 4, 4, 8, 0, 0,
         "The whole floor is betting against you now, ~1, and I am the house. Flail collects in bone. Nothing "
         "personal -- it is just math."},
        {"Sparrow", PILOT_MILANO, FACE_SPARROW, HAR_CHRONOS, 1, 16, 20, 13, 3, 3, 6, 6, 4, 3, 9, 0, 0,
         "I earned a real mask after you spared me, ~1 -- so now I have to take yours. Chronos will not blink. Neither "
         "will I. Not this time."},
    };
    build_roster(&trn, roster, (int)(sizeof(roster) / sizeof(roster[0])));

    finalize_locale(&trn, "Iron Reckoning",
                    "{WIDTH 276}{CENTER 160}{VMOVE 72}{COLOR 6}The syndicate had a maker: the Meridian, an old-war "
                    "arms architect who turned the ring into a market. The Concord's survivors return in prototype "
                    "iron to guard him. Finish it, ~1. Entry: 25,000cr.");

    if(art_dir) {
        char logo[512];
        snprintf(logo, sizeof(logo), "%s/reckoning_logo.lgo", art_dir);
        set_logo_from_lgo(&trn, logo, 10, 4);
    }

    static const char *const story[] = {
        "The Meridian's NOVA drops to one knee and does not rise. Smoke curls from the cockpit. The arena that funded "
        "his empire for thirty years goes silent, ~1.",
        "The old architect drags himself into the light. \"I gave the world a game instead of a war,\" he rasps. "
        "\"Your mentor called it a cage. Same thing, cheaper.\"",
        "You step past him. The stewards have him now -- the courts after that. Vengeance was the easy sentence. You "
        "hand him the one that lasts: the truth, on every feed.",
        "The Cardinal waits at the gate, mask gone, no longer hiding. \"The Concord dies with him,\" they say. \"What "
        "rises after is on you.\" They offer you the signet, handle first.",
        "You close their fingers back around it. \"Rebuild the ring clean,\" you tell them. \"Honest bouts. Real "
        "names. No more graves without them.\"",
        "One by one the survivors step from the tunnels -- Requiem, Cinder, even Sparrow, the mask they killed for "
        "already cast aside. Not a syndicate now. Just pilots, watching.",
        "In the emptying stands a child tugs a parent's sleeve and points. \"Who is that?\" The answer runs the rows "
        "like current, the way it once ran for your mentor: \"~1. That's ~1.\"",
    };
    set_story(&trn, story, (int)(sizeof(story) / sizeof(story[0])));

    int rc = save_trn(&trn, out_dir, "RECKON.TRN");
    sd_tournament_free(&trn);
    return rc;
}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        fprintf(stderr, "Usage: %s <resource_dir> <output_dir>\n", argv[0]);
        return 1;
    }
    const char *resource_dir = argv[1];
    const char *out_dir = argv[2];
    const char *art_dir = (argc > 3) ? argv[3] : NULL;
    printf("OMF 2097 \"Kyra\" Expansion Pack generator\n");
    int rc = 0;
    rc |= gen_nightshade(resource_dir, out_dir, art_dir);
    rc |= gen_reckoning(resource_dir, out_dir, art_dir);
    if(rc == 0) {
        printf("Done. Two tournaments generated.\n");
    }
    return rc;
}
