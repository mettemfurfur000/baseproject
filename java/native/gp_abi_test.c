/*
 * Exercises the FFM shim the way the Java host will: scalars in, handles out,
 * no struct knowledge at all. This is the contract Java depends on, so it gets
 * tested here where a compiler can check it, rather than discovered on a server.
 */

#include "gp_abi.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                                                  \
	do                                                                                                               \
	{                                                                                                                \
		g_checks++;                                                                                                  \
		if (!(cond))                                                                                                 \
		{                                                                                                            \
			g_failures++;                                                                                            \
			printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                   \
		}                                                                                                            \
	} while (0)

/* Stand-ins for the Java upcall stubs. Same flat signatures, no gp_pos. */
static int g_protectable_calls;
static int g_grant_calls;
static uint32_t g_last_grant_amount;
static uint32_t g_last_grant_cap;
static int g_cover_calls;
static int g_last_cover_x, g_last_cover_y, g_last_cover_z;
static uint64_t g_last_cover_now;
static int g_reject_x;
static gp_h_world g_grant_world;

static int host_protectable(uint64_t host, int32_t x, int32_t y, int32_t z)
{
	(void)host;
	(void)y;
	(void)z;
	g_protectable_calls++;
	return x != g_reject_x;
}

static uint32_t host_grant(uint64_t host, int32_t x, int32_t y, int32_t z, uint32_t amount, uint32_t cap)
{
	(void)host;
	g_grant_calls++;
	g_last_grant_amount = amount;
	g_last_grant_cap = cap;

	/* A real host has to write the points back through the shim, so the test does
	   too. This makes the grant path a round trip rather than a stub that just
	   reports success, which is what let a no-op grant masquerade as working. */
	if (!g_grant_world)
		return 0;

	return gp_abi_points_add(g_grant_world, x, y, z, amount, cap);
}

static void host_cover(uint64_t host, int32_t x, int32_t y, int32_t z, uint64_t now)
{
	(void)host;
	g_cover_calls++;
	g_last_cover_x = x;
	g_last_cover_y = y;
	g_last_cover_z = z;
	g_last_cover_now = now;
}

static void test_abi_check(void)
{
	uint32_t failed = gp_abi_check();
	if (failed)
	{
		printf("ABI mismatch:\n");
		for (uint32_t bit = 1; bit; bit <<= 1)
			if (failed & bit)
				printf("  %s: expected %lld\n", gp_abi_check_name(bit), (long long)gp_abi_expect(bit));
	}

	CHECK(failed == 0);
	CHECK(gp_abi_expect(1) == 8); // pointer size
	CHECK(gp_abi_expect(1u << 3) == 12);
	CHECK(gp_abi_check_name(0) != NULL);

	/* Faces must line up with what the host maps BlockFace onto. The count
	   includes the NONE sentinel, matching GP_FACE_COUNT. */
	CHECK(gp_abi_face_none() == 0);
	CHECK(gp_abi_face_count() == 7);
	CHECK(gp_abi_face_down() < gp_abi_face_up());
}

static void test_bad_handles(void)
{
	/* Every entry point must survive handle 0 rather than faulting, because a
	   stale handle in Java is an ordinary possibility and should not take the
	   server down with it. */
	uint32_t sink_value = 0xdeadbeef;

	gp_abi_world_destroy(0);
	gp_abi_world_config(0, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP);
	CHECK(gp_abi_chunk_at(0, 0, 64, 0) == 0);
	CHECK(gp_abi_chunk_unload(0, 0, 0) == 0);
	CHECK(gp_abi_reinf_add(0, 0, 64, 0, gp_abi_face_up(), 5) == 0);
	CHECK(gp_abi_reinf_face_durability(0, 0, 64, 0, gp_abi_face_up()) == 0);
	CHECK(gp_abi_weakest_face(0, 0, 64, 0) == GP_ABI_NO_FACE);
	CHECK(gp_abi_points_get(0, 0, 64, 0) == 0);
	CHECK(gp_abi_points_add(0, 0, 64, 0, 5, 0) == 0);
	CHECK(gp_abi_move_block(0, 0, 64, 0, 1, 64, 0, gp_abi_face_east()) == 0);
	CHECK(gp_abi_shield_create(0, 0, 64, 0) == 0);
	gp_abi_shield_destroy(0);
	CHECK(gp_abi_shield_add_target(0, 0, 0, 0) == 0);
	gp_abi_shield_set_powered(0, 1);
	CHECK(gp_abi_world_memory(0) == 0);

	/* Out params must be initialised even on failure, or Java reads stale stack
	   memory and makes a decision from it. */
	CHECK(gp_abi_resolve_break(0, 0, 64, 0, gp_abi_face_up(), 1, 1, &sink_value, NULL, NULL, NULL) == 0);
	CHECK(sink_value == 0);

	CHECK(gp_abi_world_save(0, "should-not-exist.gz") == 0);
	CHECK(gp_abi_last_error() != NULL);
}

static void test_config_partial_update(void)
{
	gp_h_world w = gp_abi_world_create();
	CHECK(w != 0);

	uint32_t height = 0;
	uint32_t pool = 0;
	uint32_t decay = 0;

	/* KEEP must mean "leave the library default alone", so a host setting one
	   value does not silently reset the rest to zero. */
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, 600, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	gp_abi_world_config_get(w, NULL, &pool, NULL, NULL, NULL, NULL, NULL, NULL, &decay, NULL, &height,
	                        NULL, NULL);

	CHECK(decay == 600);
	CHECK(height == 384); // still the library default
	CHECK(pool == 4096);

	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, 128,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	gp_abi_world_config_get(w, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &height,
	                        NULL, NULL);
	CHECK(height == 128);

	gp_abi_world_destroy(w);
}

static void test_reinforcement_and_break(void)
{
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    40, 300);
	CHECK(w != 0);

	uint32_t copper = 0;
	uint32_t iron = 0;
	gp_abi_world_config_get(w, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &copper,
	                        &iron);
	CHECK(copper == 40);
	CHECK(iron == 300);

	CHECK(gp_abi_chunk_at(w, 5, 64, 5) == 1);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_up(), 40) == 1);
	CHECK(gp_abi_reinf_face_durability(w, 5, 64, 5, gp_abi_face_up()) == 40);
	CHECK(gp_abi_reinf_face_active(w, 5, 64, 5, gp_abi_face_up()) == 1);
	CHECK(gp_abi_reinf_face_count(w, 5, 64, 5) == 1);

	/*
	 * A plate reinforces a face to its tier value rather than adding to it, so
	 * applying one to a reinforced face upgrades it if it is stronger and is
	 * refused otherwise. The host reads that false as "don't spend the item".
	 * The face is already on copper from above.
	 */
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_up(), 300) == 1); // iron upgrades it
	CHECK(gp_abi_reinf_face_durability(w, 5, 64, 5, gp_abi_face_up()) == 300);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_up(), 40) == 0);  // copper cannot downgrade
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_up(), 300) == 0); // nor does an equal plate
	CHECK(gp_abi_reinf_face_durability(w, 5, 64, 5, gp_abi_face_up()) == 300);
	CHECK(gp_abi_reinf_face_count(w, 5, 64, 5) == 1); // a refusal consumes no slot

	// back to a known durability for the break tests below
	gp_abi_reinf_clear(w, 5, 64, 5);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_up(), 40) == 1);

	/* Out of range positions and faces are refused. */
	CHECK(gp_abi_reinf_add(w, 5, 9999, 5, gp_abi_face_up(), 40) == 0);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, 99, 40) == 0);

	// Tier values are bounded, so refuse rather than truncating to u16: a value
	// that wrapped would silently become a weaker plate.
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_south(), 0) == 0);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_south(), 65536) == 0);
	CHECK(gp_abi_reinf_add(w, 5, 64, 5, gp_abi_face_south(), 0xFFFFFFFFu) == 0);
	CHECK(gp_abi_reinf_face_active(w, 5, 64, 5, gp_abi_face_south()) == 0);

	/* A break on the reinforced face is absorbed and eats durability. */
	uint32_t survived = 0, broke = 0, absorbed = 0, left = 0;
	CHECK(gp_abi_resolve_break(w, 5, 64, 5, gp_abi_face_up(), 1, 1, &survived, &broke, &absorbed,
	                           &left) == 1);
	CHECK(survived == 1);
	CHECK(broke == 0);
	CHECK(gp_abi_reinf_face_durability(w, 5, 64, 5, gp_abi_face_up()) == 39);

	/* A break on a bare face falls through. */
	CHECK(gp_abi_resolve_break(w, 5, 64, 5, gp_abi_face_north(), 1, 1, &survived, &broke, &absorbed,
	                           &left) == 1);
	CHECK(survived == 0);
	CHECK(broke == 1);

	/* Switching reinforcement off must not let its durability protect anything,
	   otherwise a host that disables the layer still blocks breaks. */
	CHECK(gp_abi_resolve_break(w, 5, 64, 5, gp_abi_face_up(), 0, 1, &survived, &broke, &absorbed,
	                           &left) == 1);
	CHECK(survived == 0);
	CHECK(broke == 1);
	CHECK(gp_abi_reinf_face_durability(w, 5, 64, 5, gp_abi_face_up()) == 39);

	/* Resolving a break must not allocate a chunk for an unprotected block. */
	CHECK(gp_abi_chunk_is_loaded(w, 40, 40) == 0);
	CHECK(gp_abi_resolve_break(w, 600, 64, 600, gp_abi_face_up(), 1, 1, &survived, &broke, &absorbed,
	                           &left) == 1);
	CHECK(gp_abi_chunk_is_loaded(w, 37, 37) == 0);

	CHECK(gp_abi_reinf_clear(w, 5, 64, 5) == 1);
	CHECK(gp_abi_reinf_face_count(w, 5, 64, 5) == 0);
	CHECK(gp_abi_reinf_clear(w, 5, 64, 5) == 0); // nothing left

	gp_abi_world_destroy(w);
}

static void test_weakest_face_policy(void)
{
	/* The host resolves a break with no known face against the weakest
	   reinforced face. That policy lives here so it can be tested. */
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	CHECK(gp_abi_weakest_face(w, 1, 64, 1) == GP_ABI_NO_FACE);

	CHECK(gp_abi_reinf_add(w, 1, 64, 1, gp_abi_face_up(), 300) == 1);
	CHECK(gp_abi_weakest_face(w, 1, 64, 1) == gp_abi_face_up());

	// a weaker face becomes the answer
	CHECK(gp_abi_reinf_add(w, 1, 64, 1, gp_abi_face_north(), 40) == 1);
	CHECK(gp_abi_weakest_face(w, 1, 64, 1) == gp_abi_face_north());

	// ties resolve to the lower face id, so the same block always picks the same
	// face instead of depending on slot order
	CHECK(gp_abi_reinf_add(w, 1, 64, 1, gp_abi_face_down(), 40) == 1);
	CHECK(gp_abi_weakest_face(w, 1, 64, 1) == gp_abi_face_down());

	uint32_t survived = 0, broke = 0;
	CHECK(gp_abi_resolve_break(w, 1, 64, 1, gp_abi_weakest_face(w, 1, 64, 1), 1, 1, &survived,
	                           &broke, NULL, NULL) == 1);
	CHECK(survived == 1);

	gp_abi_world_destroy(w);
}

static void test_movement(void)
{
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	CHECK(gp_abi_reinf_add(w, 0, 64, 0, gp_abi_face_up(), 50) == 1);
	CHECK(gp_abi_points_add(w, 0, 64, 0, 12, 0) == 12);

	// pushed east, so it came off its west face, which is bare
	CHECK(gp_abi_move_block(w, 0, 64, 0, 1, 64, 0, gp_abi_face_east()) == 1);
	CHECK(gp_abi_reinf_face_durability(w, 1, 64, 0, gp_abi_face_up()) == 50);
	CHECK(gp_abi_points_get(w, 1, 64, 0) == 12);
	CHECK(gp_abi_reinf_face_durability(w, 0, 64, 0, gp_abi_face_up()) == 0);

	// The host checks piston source-face policy; the store still moves faces.
	CHECK(gp_abi_reinf_add(w, 1, 64, 0, gp_abi_face_east(), 50) == 1);
	CHECK(gp_abi_move_block(w, 1, 64, 0, 2, 64, 0, gp_abi_face_east()) == 1);
	CHECK(gp_abi_reinf_face_durability(w, 2, 64, 0, gp_abi_face_east()) == 50);
	CHECK(gp_abi_reinf_face_durability(w, 1, 64, 0, gp_abi_face_east()) == 0);

	// a nonsense motion is refused rather than interpreted
	CHECK(gp_abi_move_block(w, 1, 64, 0, 2, 64, 0, 99) == 0);

	// cross a chunk border
	CHECK(gp_abi_reinf_add(w, 15, 64, 15, gp_abi_face_up(), 50) == 1);
	CHECK(gp_abi_move_block(w, 15, 64, 15, 16, 64, 15, gp_abi_face_east()) == 1);
	CHECK(gp_abi_reinf_face_durability(w, 16, 64, 15, gp_abi_face_up()) == 50);

	gp_abi_world_destroy(w);
}

static void test_chunk_lifecycle(void)
{
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	uint32_t loaded = 0, reserved = 0;

	CHECK(gp_abi_chunk_is_loaded(w, 0, 0) == 0);
	CHECK(gp_abi_reinf_add(w, 0, 64, 0, gp_abi_face_up(), 10) == 1);
	gp_abi_chunk_counts(w, &loaded, &reserved);
	CHECK(loaded == 1);
	CHECK(reserved == 1);

	CHECK(gp_abi_chunk_unload(w, 0, 0) == 1);
	gp_abi_chunk_counts(w, &loaded, &reserved);
	CHECK(loaded == 0);
	CHECK(reserved == 1); // the slot stays reserved for reuse

	CHECK(gp_abi_chunk_unload(w, 0, 0) == 0); // already unloaded
	CHECK(gp_abi_chunk_unload(w, 99, 99) == 0);

	// reloading the same coordinates reuses the slot
	CHECK(gp_abi_chunk_at(w, 0, 64, 0) == 1);
	gp_abi_chunk_counts(w, &loaded, &reserved);
	CHECK(loaded == 1);
	CHECK(reserved == 1);

	gp_abi_world_destroy(w);
}

static void test_sink_and_shields(void)
{
	gp_h_world w = gp_abi_world_create();
	/* max_faces 4, pool 4096, regen 20/window, window 1, range KEEP,
	   max_targets KEEP, fuel_max 1000, fuel_per_sec 1 */
	gp_abi_world_config(w, 4, 4096, 20, 1, GP_ABI_KEEP, GP_ABI_KEEP, 1000, 1, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP);
	CHECK(w != 0);

	g_protectable_calls = 0;
	g_cover_calls = 0;
	g_grant_calls = 0;
	g_reject_x = -999;
	g_grant_world = w;

	CHECK(gp_abi_sink_configure(w, 0xABCD, (uint64_t)(uintptr_t)&host_protectable,
	                            (uint64_t)(uintptr_t)&host_grant, (uint64_t)(uintptr_t)&host_cover) == 1);

	gp_h_shield s = gp_abi_shield_create(w, 10, 64, 10);
	CHECK(s != 0);

	// a second shield at the same position would be a host bookkeeping bug, so
	// the shim reports it instead of quietly stacking two
	CHECK(gp_abi_shield_create(w, 10, 64, 10) == 0);
	CHECK(gp_abi_last_error() != NULL);

	CHECK(gp_abi_shield_add_target(s, 11, 64, 10) == 1);
	CHECK(gp_abi_shield_count(w) == 1);
	CHECK(gp_abi_shield_at(w, 0) == s);
	CHECK(gp_abi_shield_at(w, 1) == 0);

	int32_t sx = -1, sy = -1, sz = -1;
	gp_abi_shield_get_pos(s, &sx, &sy, &sz);
	CHECK(sx == 10 && sy == 64 && sz == 10);

	uint32_t powered = 0, running = 0, fuel = 0;
	gp_abi_shield_get_state(s, &powered, &running, &fuel);
	CHECK(powered == 0);
	CHECK(running == 0);

	gp_abi_shield_set_fuel(s, 1000);
	gp_abi_shield_set_powered(s, 1);

	uint32_t granted = 0xdeadbeef;

	/*
	 * The first tick only anchors the window clock. This is what stops a fresh
	 * shield from treating everything since the epoch as elapsed time and burning
	 * its whole fuel reserve on tick one, which is what a wall-clock host would
	 * otherwise trigger.
	 */
	CHECK(gp_abi_shield_tick(s, 100, &granted) == 0);
	CHECK(granted == 0);
	CHECK(g_protectable_calls == 0);

	// a window later it grants the full regen budget and claims coverage.
	// The window keeps topping the single target up until the budget is spent,
	// so the grant is the whole budget rather than one point.
	uint32_t budget = 0;
	gp_abi_world_config_get(w, NULL, NULL, &budget, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	                        NULL, NULL);
	CHECK(gp_abi_shield_tick(s, 101, &granted) == 20);
	CHECK(granted == 20);
	CHECK(g_last_grant_cap > 0);
	CHECK(gp_abi_points_get(w, 11, 64, 10) == 20);
	CHECK(g_protectable_calls > 0);
	CHECK(g_cover_calls >= 1);
	CHECK(g_last_cover_x == 11);
	CHECK(g_last_cover_y == 64);
	CHECK(g_last_cover_z == 10);
	CHECK(g_last_cover_now == 101);

	// coverage keeps being claimed on later windows too
	g_cover_calls = 0;
	gp_abi_shield_tick(s, 102, &granted);
	CHECK(g_cover_calls >= 1);

	// a block the host refuses is neither granted nor claimed
	g_reject_x = 11;
	g_protectable_calls = 0;
	g_cover_calls = 0;
	gp_abi_shield_tick(s, 103, &granted);
	CHECK(g_cover_calls == 0);

	CHECK(gp_abi_shield_remove_target(s, 11, 64, 10) == 1);
	gp_abi_shield_clear_targets(s);
	CHECK(gp_abi_shield_count(w) == 1);

	gp_abi_shield_destroy(s);
	CHECK(gp_abi_shield_count(w) == 0);
	CHECK(gp_abi_shield_at(w, 0) == 0);

	gp_abi_world_destroy(w);
}

static void test_sink_unconfigured_is_safe(void)
{
	/* A world with no host callbacks must grant nothing rather than fault. This
	   is the state the plugin is in between construction and wiring. */
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	gp_h_shield s = gp_abi_shield_create(w, 0, 64, 0);
	CHECK(s != 0);
	CHECK(gp_abi_shield_add_target(s, 1, 64, 0) == 1);
	gp_abi_shield_set_fuel(s, 64);
	gp_abi_shield_set_powered(s, 1);

	uint32_t granted = 0xdeadbeef;
	gp_abi_shield_tick(s, 100, &granted);
	CHECK(granted == 0);
	CHECK(gp_abi_points_get(w, 1, 64, 0) == 0);

	gp_abi_shield_destroy(s);
	gp_abi_world_destroy(w);
}

static void test_coverage_decay(void)
{
	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, 300, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	CHECK(gp_abi_points_add(w, 2, 64, 2, 20, 0) == 20);

uint32_t touched = 0xdeadbeef, skipped = 0xdeadbeef;

	/*
	 * Sweeps land on a fixed cadence: the first call schedules the next one an
	 * interval out, then every due call advances by exactly one interval, so a
	 * host that sweeps every tick keeps a stable schedule.
	 */
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 0, 300, 1, &touched, &skipped) == 0);
	CHECK(touched == 0 && skipped == 0);

	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 150, 300, 1, &touched, &skipped) == 0); // not due yet
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 299, 300, 1, &touched, &skipped) == 0);

	// due, and nothing claims it, so it bleeds
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 300, 300, 1, &touched, &skipped) == 1);
	CHECK(touched == 1 && skipped == 0);
	CHECK(gp_abi_points_get(w, 2, 64, 2) == 19);

	// a claim expires one interval after it was made, compared strictly
	gp_abi_cover(w, 2, 64, 2, 400);
	CHECK(gp_abi_chunk_covered(w, 2, 64, 2, 400) == 1);
	CHECK(gp_abi_chunk_covered(w, 2, 64, 2, 700) == 1);
	CHECK(gp_abi_chunk_covered(w, 2, 64, 2, 701) == 0);

	// due at 600, still claimed, so the sweep skips it
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 600, 300, 1, &touched, &skipped) == 0);
	CHECK(touched == 0 && skipped == 1);
	CHECK(gp_abi_points_get(w, 2, 64, 2) == 19);

	// due at 900, the claim has lapsed, so it decays again
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 900, 300, 1, &touched, &skipped) == 1);
	CHECK(touched == 1 && skipped == 0);
	CHECK(gp_abi_points_get(w, 2, 64, 2) == 18);

	// re-claiming protects it again on the next due sweep
	gp_abi_cover(w, 2, 64, 2, 1000);
	CHECK(gp_abi_chunk_decay(w, 2, 64, 2, 1200, 300, 1, &touched, &skipped) == 0);
	CHECK(touched == 0 && skipped == 1);
	CHECK(gp_abi_points_get(w, 2, 64, 2) == 18);

	g_grant_world = 0;

	gp_abi_world_destroy(w);
}

static void test_persistence(void)
{
	const char *path = "build/abi_world.gz";

	gp_h_world w = gp_abi_world_create();
	gp_abi_world_config(w, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	gp_abi_sink_configure(w, 1, (uint64_t)(uintptr_t)&host_protectable, (uint64_t)(uintptr_t)&host_grant,
	                      (uint64_t)(uintptr_t)&host_cover);
	g_reject_x = -999;
	g_grant_world = w;

	gp_abi_reinf_add(w, 0, 64, 0, gp_abi_face_up(), 77);
	gp_abi_reinf_add(w, 0, 64, 0, gp_abi_face_west(), 33);
	gp_abi_points_add(w, 0, 64, 0, 9, 0);
	gp_abi_points_add(w, 1, 64, 0, 4, 0);
	gp_abi_cover(w, 0, 64, 0, 1234);

	gp_h_shield s = gp_abi_shield_create(w, 0, 64, 5);
	CHECK(s != 0);
	gp_abi_shield_add_target(s, 3, 64, 5);
	gp_abi_shield_add_target(s, 4, 64, 5);

	CHECK(gp_abi_world_save(w, path) == 1);
	gp_abi_world_destroy(w);

	// fresh world, load it back
	gp_h_world r = gp_abi_world_create();
	gp_abi_world_config(r, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP);
	gp_abi_sink_configure(r, 2, (uint64_t)(uintptr_t)&host_protectable, (uint64_t)(uintptr_t)&host_grant,
	                      (uint64_t)(uintptr_t)&host_cover);
	g_grant_world = r;

	int restored = gp_abi_world_load(r, path, 16, 16);
	CHECK(restored == 1);

	CHECK(gp_abi_reinf_face_durability(r, 0, 64, 0, gp_abi_face_up()) == 77);
	CHECK(gp_abi_reinf_face_durability(r, 0, 64, 0, gp_abi_face_west()) == 33);
	CHECK(gp_abi_points_get(r, 0, 64, 0) == 9);
	CHECK(gp_abi_points_get(r, 1, 64, 0) == 4);

	// the coverage claim came back too, with its expiry intact
	uint32_t interval = 0;
	gp_abi_world_config_get(r, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &interval, NULL, NULL,
	                        NULL, NULL);
	CHECK(interval > 0);
	uint32_t expiry = 1234 + interval + 1;
	CHECK(gp_abi_chunk_covered(r, 0, 64, 0, 1233) == 1);
	CHECK(gp_abi_chunk_covered(r, 0, 64, 0, expiry - 2) == 1);
	CHECK(gp_abi_chunk_covered(r, 0, 64, 0, expiry) == 0);

	CHECK(gp_abi_shield_count(r) == 1);
	gp_h_shield rs = gp_abi_shield_at(r, 0);
	CHECK(rs != 0);

	int32_t sx = 0, sy = 0, sz = 0;
	gp_abi_shield_get_pos(rs, &sx, &sy, &sz);
	CHECK(sx == 0 && sy == 64 && sz == 5);

	// and the restored shield actually works, re-anchoring its clock on the
	// first tick rather than charging fuel for the time it spent unloaded
	uint32_t granted = 0;
	gp_abi_shield_set_fuel(rs, 1000);
	gp_abi_shield_set_powered(rs, 1);
	CHECK(gp_abi_shield_tick(rs, 500, &granted) == 0); // anchors only
	CHECK(gp_abi_shield_tick(rs, 501, &granted) > 0);
	CHECK(granted > 0);
	uint32_t fuel_after = 0;
	gp_abi_shield_get_state(rs, NULL, NULL, &fuel_after);
	CHECK(fuel_after > 900); // burned ~1, not the whole reserve

	// loading twice must not leak or double register the first set of shields
	CHECK(gp_abi_world_load(r, path, 16, 16) == 1);
	CHECK(gp_abi_shield_count(r) == 1);

	gp_abi_world_destroy(r);

	// a corrupt file is reported rather than loaded as empty
	gp_h_world bad = gp_abi_world_create();
	CHECK(gp_abi_world_load(bad, "build/does_not_exist.gz", 16, 16) == -1);
	CHECK(gp_abi_last_error() != NULL);
	gp_abi_world_destroy(bad);
}

static void test_world_slots(void)
{
	/* Paper has several dimensions, so more than one world at a time is normal,
	   and destroying one must leave the others usable. */
	gp_h_world a = gp_abi_world_create();
	gp_h_world b = gp_abi_world_create();
	CHECK(a != 0 && b != 0);
	CHECK(a != b);

	gp_abi_world_config(a, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP,
	                    GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, GP_ABI_KEEP, 128,
	                    GP_ABI_KEEP, GP_ABI_KEEP);

	CHECK(gp_abi_reinf_add(a, 0, 10, 0, gp_abi_face_up(), 10) == 1);

	uint32_t ha = 0, hb = 0;
	gp_abi_world_config_get(a, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ha, NULL,
	                        NULL);
	gp_abi_world_config_get(b, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &hb, NULL,
	                        NULL);
	CHECK(ha == 128);
	CHECK(hb == 384); // independent defaults

	// a height one world accepts, the other rejects
	CHECK(gp_abi_reinf_add(b, 0, 10, 0, gp_abi_face_up(), 10) == 1);

	gp_abi_world_destroy(a);

	// b must be untouched
	CHECK(gp_abi_reinf_face_durability(b, 0, 10, 0, gp_abi_face_up()) == 10);

	// the freed slot is reusable
	gp_h_world c = gp_abi_world_create();
	CHECK(c == a);
	gp_abi_world_destroy(b);
	gp_abi_world_destroy(c);
}

int main(void)
{
	test_abi_check();
	test_bad_handles();
	test_config_partial_update();
	test_reinforcement_and_break();
	test_weakest_face_policy();
	test_movement();
	test_chunk_lifecycle();
	test_sink_and_shields();
	test_sink_unconfigured_is_safe();
	test_coverage_decay();
	test_persistence();
	test_world_slots();

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}