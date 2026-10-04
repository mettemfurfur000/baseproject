#ifndef GRIEFPROT_SHIELD_H
#define GRIEFPROT_SHIELD_H

#include "general.h"
#include "griefprot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the shield needs from the host in order to grant points to a block.

   The library stays ignorant of block types, so the host decides what may be
   protected at all. That covers both "instabreakable" blocks like torches and
   grass, and excluding other shield blocks from coverage.

   Zero initialise this struct. The callbacks after `grant` are optional, and
   filling the first three fields individually leaves the rest as stack
   garbage, which the shield cannot tell apart from a valid function pointer. */
typedef struct
{
	void *user;

	/* True when `pos` may hold shield points. Called for every candidate block,
	   so it should be cheap. */
	bool (*protectable)(void *user, gp_pos pos);

	/* Grants up to `amount` points at `pos`, stopping at `cap`. Returns the
	   number of points actually granted. */
	u32 (*grant)(void *user, gp_pos pos, u32 amount, u32 cap);

	/* Optional. Called for every position a running shield considers covered,
	   including blocks that are already at their ceiling and therefore receive
	   no points. `now` is the host clock reading for this window.

	   Coverage is what keeps decay off a block: a block with no live shield over
	   it should bleed, and a block a shield is actively holding up should not
	   dip between windows. `gp_world_shield_cover` is a ready made
	   implementation that marks the block in whichever chunk owns it.

	   May be NULL, in which case decay cannot tell covered blocks from abandoned
	   ones and will bleed both. */
	void (*cover)(void *user, gp_pos pos, u64 now);
} gp_shield_sink;

/* A shield block's state.

   The pool is not a depletable budget: it defines each target's ceiling as
   ceil(pool / target_count). Regeneration tops targets up towards that ceiling
   indefinitely, and shrinks it if targets are added later.

   `cursor` is the resume point for the round robin. Each regeneration window
   grants up to `regen_per_window` points, one at a time, walking the target
   list from `cursor` and stopping where the budget ran out. */
typedef struct
{
	gp_pos pos;

	gp_pos *targets;
	u32 target_count;
	u32 target_capacity;

	u32 pool;
	u32 regen_per_window;
	u32 window;
	u32 range;
	u32 fuel_per_sec;

	u32 cursor;  // resume index for the next window
	u32 budget;  // points still grantable this window

	u32 fuel;
	bool powered;
	bool running;

	u64 window_start; // timestamp of the current window
bool started; // has ticked at least once, so window_start is meaningful
} gp_shield;

/* Allocates a shield. `capacity` bounds the target list; pass cfg->shield_max_targets.
   Returns NULL on allocation failure. */
gp_shield *gp_shield_create(const gp_config *cfg, gp_pos pos, u32 capacity);
void gp_shield_destroy(gp_shield *shield);

/* Each target's ceiling: ceil(pool / target_count), or 0 when nothing is selected. */
u32 gp_shield_share(const gp_shield *shield);

/* Target list editing. add returns false when already present or the list is full.
   remove keeps `cursor` inside bounds so the round robin stays coherent. */
bool gp_shield_add_target(gp_shield *shield, gp_pos target);
bool gp_shield_remove_target(gp_shield *shield, gp_pos target);
void gp_shield_clear_targets(gp_shield *shield);

/* Crafting: copies `src`'s target list into `dst` (the "split" recipe), and
   appends `src`'s list to `dst` (the "merge" recipe). Duplicates are skipped. */
bool gp_shield_copy_targets(gp_shield *dst, const gp_shield *src);
u32 gp_shield_merge_targets(gp_shield *dst, const gp_shield *src);

void gp_shield_set_fuel(gp_shield *shield, u32 fuel);
void gp_shield_set_powered(gp_shield *shield, bool powered);

/* Advances the shield to `now`, spending at most one window's worth of budget.
   `points_granted` may be NULL. Returns the points granted this call. */
u32 gp_shield_tick(gp_shield *shield, u64 now, const gp_shield_sink *sink, u32 *points_granted);

/* Grants everything the current window allows, for callers that want to catch up
   after a long gap in one go. Returns total points granted.

   `now` is the host clock reading, passed through to the sink's cover callback so
   a running shield can mark the blocks it is holding up as off limits to decay. */
u32 gp_shield_run_window(gp_shield *shield, const gp_shield_sink *sink, u64 now);

#ifdef __cplusplus
}
#endif

#endif // GP_GRIEFPROT_SHIELD_H
