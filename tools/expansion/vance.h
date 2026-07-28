/** @file vance.h
 * @brief Shared constants for the original Nightshade Concord antagonist faces.
 *
 * OMF tournament enemies do not embed a portrait on disk -- each enemy references
 * a face by photo_id into the tournament's shared PIC. The expansion ships an
 * original cast (see tools/expansion/gen.c and expansion-art/portraits/), whose
 * hand-generated faces are appended to a copy of WORLD.PIC as NIGHTSHD.PIC.
 *
 * The face indices are chosen comfortably above the WORLD.PIC face count (~44)
 * so they never collide with a real classic-pilot portrait. Both the portrait
 * injector (mkportrait.c) and the tournament generator (gen.c) include this
 * header, so an enemy's photo_id always resolves to the intended new face.
 *
 * VANCE_PHOTO_ID / VANCE_PIC_NAME are retained as legacy aliases so older code
 * paths keep building; new code should use the CONCORD_* table below.
 * @license MIT
 */

#ifndef EXPANSION_VANCE_H
#define EXPANSION_VANCE_H

// Portrait PIC the expansion tournaments reference (a copy of WORLD.PIC with the
// new original antagonist faces appended).
#define VANCE_PIC_NAME "NIGHTSHD.PIC"

// First slot used by the expansion cast. WORLD.PIC has ~44 real faces; starting
// at 50 leaves a safe gap that mkportrait pads with a valid filler face.
#define CONCORD_FACE_BASE 50

// The original Nightshade Concord cast, in the same order they are appended to
// NIGHTSHD.PIC. Keep this list in sync between mkportrait.c and gen.c.
enum concord_face
{
    FACE_CARDINAL = CONCORD_FACE_BASE, // masked magistrate    (tournament 1 boss)
    FACE_MERIDIAN,                     // old-war architect    (tournament 2 boss)
    FACE_REQUIEM,                      // silent executioner
    FACE_VESPER,                       // spymaster
    FACE_CINDER,                       // pyromaniac
    FACE_SERAPH,                       // zealot
    FACE_BASTION,                      // the wall
    FACE_MARROW,                       // combat-surgeon
    FACE_WAGER,                        // match-fixer gambler
    FACE_SPARROW,                      // hungry rookie prospect
    FACE_CONCORD_COUNT
};

#define CONCORD_FACE_TOTAL (FACE_CONCORD_COUNT - CONCORD_FACE_BASE)

// The highest face index the injector must produce; mkportrait pads up to it.
#define CONCORD_FACE_LAST (FACE_SPARROW)

// Legacy aliases (the previous single-portrait pipeline referenced these).
#define VANCE_PHOTO_ID FACE_CARDINAL

// Maps each cast face to its source .vph filename stem (in expansion-art/portraits/).
// Order MUST match enum concord_face.
#define CONCORD_FACE_STEMS                                                                                             \
    {                                                                                                                  \
        "cardinal", "meridian", "requiem", "vesper", "cinder", "seraph", "bastion", "marrow", "wager", "sparrow"       \
    }

#endif // EXPANSION_VANCE_H
