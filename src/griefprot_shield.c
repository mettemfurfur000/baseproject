#include "griefprot_shield.h"
#include <stdlib.h>
#include <string.h>

gp_shield *gp_shield_create(const gp_config *cfg, gp_pos pos, u32 capacity)
{
	if (!cfg)
		return NULL;

	gp_shield *shield = (gp_shield *)calloc(1, sizeof(gp_shield));
	if (!shield)
		return NULL;

	shield->targets = (gp_pos *)calloc(capacity ? capacity : 1, sizeof(gp_pos));
	if (!shield->targets)
	{
		free(shield);
		return NULL;
	}

	shield->pos = pos;
	shield->target_capacity = capacity ? capacity : 1;
	shield->target_count = 0;
	shield->pool = cfg->shield_pool;
	shield->regen_per_window = cfg->shield_regen;
	shield->window = cfg->shield_window ? cfg->shield_window : 1;
	shield->range = cfg->shield_range;
	shield->fuel_per_sec = cfg->shield_fuel_per_sec;
	shield->cursor = 0;
	shield->budget = 0;
	shield->fuel = cfg->shield_fuel_max;
	shield->powered = false;
	shield->running = false;
	shield->window_start = 0;
	shield->started = false;
	return shield;
}

void gp_shield_destroy(gp_shield *shield)
{
	if (!shield)
		return;
	free(shield->targets);
	free(shield);
}

u32 gp_shield_share(const gp_shield *shield)
{
	if (!shield || shield->target_count == 0)
		return 0;

	// rounded up, so a small list still gets whole points
	return (shield->pool + shield->target_count - 1) / shield->target_count;
}

bool gp_shield_add_target(gp_shield *shield, gp_pos target)
{
	if (!shield)
		return false;

	for (u32 i = 0; i < shield->target_count; i++)
		if (gp_pos_eq(shield->targets[i], target))
			return false; // already selected

	if (shield->target_count >= shield->target_capacity)
		return false;

	shield->targets[shield->target_count++] = target;
	return true;
}

bool gp_shield_remove_target(gp_shield *shield, gp_pos target)
{
	if (!shield || shield->target_count == 0)
		return false;

	for (u32 i = 0; i < shield->target_count; i++)
	{
		if (!gp_pos_eq(shield->targets[i], target))
			continue;

		for (u32 j = i; j + 1 < shield->target_count; j++)
			shield->targets[j] = shield->targets[j + 1];
		shield->target_count--;

		// keep the round robin pointing at a live entry
		if (shield->target_count == 0)
			shield->cursor = 0;
		else if (shield->cursor >= shield->target_count)
			shield->cursor = shield->target_count - 1;

		return true;
	}

	return false;
}

void gp_shield_clear_targets(gp_shield *shield)
{
	if (!shield)
		return;
	shield->target_count = 0;
	shield->cursor = 0;
	shield->budget = 0;
}

bool gp_shield_copy_targets(gp_shield *dst, const gp_shield *src)
{
	if (!dst || !src)
		return false;

	if (src->target_count > dst->target_capacity)
		return false;

	memcpy(dst->targets, src->targets, (size_t)src->target_count * sizeof(gp_pos));
	dst->target_count = src->target_count;
	dst->cursor = 0;
	return true;
}

u32 gp_shield_merge_targets(gp_shield *dst, const gp_shield *src)
{
	if (!dst || !src)
		return 0;

	u32 added = 0;
	for (u32 i = 0; i < src->target_count; i++)
		if (gp_shield_add_target(dst, src->targets[i]))
			added++;
	return added;
}

void gp_shield_set_fuel(gp_shield *shield, u32 fuel)
{
	if (shield)
		shield->fuel = fuel;
}

void gp_shield_set_powered(gp_shield *shield, bool powered)
{
	if (shield)
		shield->powered = powered;
}

/* One pass of the round robin.

   Walks the target list from `cursor`, handing out single points, and stops as
   soon as the budget is spent or the list has been covered once. The index where
   it stopped becomes the next window's `cursor`. */
static u32 grant_pass(gp_shield *shield, const gp_shield_sink *sink)
{
	u32 share = gp_shield_share(shield);
	if (share == 0 || shield->budget == 0)
		return 0;

	u32 granted = 0;
	u32 visited = 0;

	while (shield->budget > 0 && visited < shield->target_count)
	{
		gp_pos target = shield->targets[shield->cursor];

		if (sink && sink->protectable && !sink->protectable(sink->user, target))
		{
			// not protectable, skip it without spending budget
			shield->cursor = (shield->cursor + 1) % shield->target_count;
			visited++;
			continue;
		}

		u32 got = sink ? sink->grant(sink->user, target, 1, share) : 1;

		// either the budget ran dry or this target is already at its ceiling,
		// both of which mean the next window resumes at this same index
		shield->cursor = (shield->cursor + 1) % shield->target_count;
		visited++;

		if (got == 0)
			continue; // already topped up, no cost

		granted += got;
		shield->budget -= got > 0 ? got : 1;
		if (got > 1)
			break; // sink granted more than the single point we asked for
	}

	return granted;
}

/* Position of the `index`th cell of the (2r+1)^3 fallback cube, walked in a
   fixed order so `cursor` can resume mid-cube between windows. */
static gp_pos range_cell(const gp_shield *shield, u32 index, u32 side, i32 r)
{
	u32 x = index % side;
	u32 y = (index / side) % side;
	u32 z = index / (side * side);

	return (gp_pos){.x = shield->pos.x + (i32)x - r, .y = shield->pos.y + (i32)y - r,
	                .z = shield->pos.z + (i32)z - r};
}

/* Fallback coverage for a shield with no selected targets: everything inside a
   (2r+1)^3 cube centred on the shield. The sink filters out the shield itself
   and any other shield, plus whatever the host considers unprotectable.

   The pool is shared out over every eligible cell, so the ceiling is
   ceil(pool / eligible) rather than the pool itself. Eligible cells are counted
   first because the sink is the only thing that knows which ones qualify.

   `cursor` doubles as a linear cell index here, which is safe because the cube
   and the target list are never active at the same time. Without it every window
   would restart at the same corner and the far side of the cube would starve. */
/* Marks every block this shield is responsible for as covered.

   This deliberately runs over the whole eligible set rather than riding along
   with the grant pass. The grant pass stops as soon as the window's budget is
   spent, and it does nothing at all once every target is topped up, so tying
   coverage to it would leave fully protected blocks unclaimed and let decay
   bleed them.

   Only positions the sink accepts are claimed: covering a block that is not
   actually protectable would suppress decay somewhere the shield is not
   responsible. */
static void claim_coverage(const gp_shield *shield, const gp_shield_sink *sink, u64 now)
{
	if (!sink || !sink->cover)
		return;

	// no targets means range mode, matching gp_shield_run_window
	if (shield->target_count == 0)
	{
		if (!sink->protectable || shield->range == 0)
			return;

		i32 r = (i32)shield->range;
		u32 side = (u32)(2 * r + 1);
		u32 cells = side * side * side;

		for (u32 i = 0; i < cells; i++)
		{
			gp_pos pos = range_cell(shield, i, side, r);
			if (!gp_pos_eq(pos, shield->pos) && sink->protectable(sink->user, pos))
				sink->cover(sink->user, pos, now);
		}
		return;
	}

	for (u32 i = 0; i < shield->target_count; i++)
	{
		gp_pos target = shield->targets[i];
		if (!sink->protectable || sink->protectable(sink->user, target))
			sink->cover(sink->user, target, now);
	}
}

static u32 grant_range(gp_shield *shield, const gp_shield_sink *sink)
{
	if (!sink || !sink->protectable || shield->budget == 0 || shield->range == 0)
		return 0;

	i32 r = (i32)shield->range;
	u32 side = (u32)(2 * r + 1);
	u32 cells = side * side * side;
	if (cells == 0)
		return 0;

	u32 eligible = 0;
	for (u32 i = 0; i < cells; i++)
	{
		gp_pos p = range_cell(shield, i, side, r);
		if (gp_pos_eq(p, shield->pos))
			continue;
		if (!sink->protectable(sink->user, p))
			continue;
		eligible++;
	}

	if (eligible == 0)
		return 0;

	u32 share = (shield->pool + eligible - 1) / eligible; // ceil, same rule as targets
	if (share == 0)
		return 0;

	u32 granted = 0;
	u32 visited = 0;

	while (shield->budget > 0 && visited < cells)
	{
		gp_pos p = range_cell(shield, shield->cursor, side, r);

		// resume after this cell next window, whether or not it was usable
		shield->cursor = (shield->cursor + 1) % cells;
		visited++;

		if (gp_pos_eq(p, shield->pos))
			continue;
		if (!sink->protectable(sink->user, p))
			continue;

		u32 got = sink->grant(sink->user, p, 1, share);
		if (got == 0)
			continue; // already topped up, no cost

		granted += got;
		shield->budget -= got;
	}

	return granted;
}

u32 gp_shield_run_window(gp_shield *shield, const gp_shield_sink *sink, u64 now)
{
	if (!shield)
		return 0;

	// Claim coverage even when the window ends up granting nothing, and even
	// when there is no budget left to give away.
	claim_coverage(shield, sink, now);

	shield->budget = shield->regen_per_window;

	if (shield->target_count == 0)
		return grant_range(shield, sink);

	// a window can cover the list several times over, e.g. 20 targets and a
	// budget of 100. Stop when a full pass grants nothing new.
	u32 total = 0;
	u32 spent = 0;

	for (u32 round = 0; round < 64; round++)
	{
		u32 before = shield->budget;
		u32 got = grant_pass(shield, sink);
		total += got;
		spent += before - shield->budget;

		if (spent == 0)
			break; // nothing left to top up
		if (shield->budget == 0)
			break;
	}

	return total;
}

u32 gp_shield_tick(gp_shield *shield, u64 now, const gp_shield_sink *sink, u32 *points_granted)
{
	if (points_granted)
		*points_granted = 0;

	if (!shield)
		return 0;

	/*
	 * Anchor the clock on the first tick. Without this, window_start stays 0 and
	 * a host that passes wall-clock time (millis, or a tick counter) makes every
	 * new shield treat the whole span since the epoch as elapsed, which burns its
	 * entire fuel reserve on the first tick and kills it permanently. Anchoring
	 * here costs one tick of generation and makes the host free to pass any
	 * monotonic timestamp it likes.
	 *
	 * A shield restored from disk is also unstarted, so it re-anchors instead of
	 * charging fuel for time it was not running.
	 */
	if (!shield->started)
	{
		shield->window_start = now;
		shield->started = true;
		return 0;
	}

	shield->running = shield->powered && shield->fuel > 0;
	if (!shield->running)
	{
		// an idle shield keeps its resume point so it does not restart the
		// round robin every time it comes back online
		shield->window_start = now;
		shield->started = true;
		return 0;
	}

	if (now < shield->window_start)
	{
		shield->window_start = now; // clock went backwards, resync
		return 0;
	}

	u64 elapsed = now - shield->window_start;
	if (elapsed == 0)
		return 0;

	// burn fuel for the time that passed, then stop if it ran out
	u32 burn = (u32)(elapsed * shield->fuel_per_sec);
	if (burn >= shield->fuel)
	{
		shield->fuel = 0;
		shield->running = false;
		shield->window_start = now;
		return 0;
	}
	shield->fuel -= burn;

	shield->window_start += elapsed; // carry the remainder, do not drift
	u32 granted = gp_shield_run_window(shield, sink, now);

	if (points_granted)
		*points_granted = granted;
	return granted;
}
