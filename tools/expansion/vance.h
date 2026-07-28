/** @file vance.h
 * @brief Shared constants for the new original antagonist portrait (Vance).
 *
 * VANCE_PHOTO_ID is the photo index, inside the expansion's portrait PIC
 * (NIGHTSHD.PIC), where the hand-generated Vance face is stored. It is chosen
 * comfortably above the WORLD.PIC face count (~44) so it never collides with a
 * real classic-pilot portrait. Both the portrait injector (mkportrait.c) and the
 * tournament generator (gen.c) use this same value, so the Vance enemy's
 * photo_id always resolves to the new face.
 * @license MIT
 */

#ifndef EXPANSION_VANCE_H
#define EXPANSION_VANCE_H

#define VANCE_PHOTO_ID 63

// Portrait PIC the expansion tournaments reference (a copy of WORLD.PIC with the
// new Vance face appended at VANCE_PHOTO_ID).
#define VANCE_PIC_NAME "NIGHTSHD.PIC"

#endif // EXPANSION_VANCE_H
