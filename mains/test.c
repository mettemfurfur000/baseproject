#include <stdio.h>
#include "griefprot.h"
#include "griefprot_chunk.h"
#include "griefprot_shield.h"
#include "griefprot_serialize.h"
#include "griefprot_world.h"

static int failures = 0;

#define CHECK(cond)                                                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(cond))                                                                                                   \
		{                                                                                                              \
			printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                      \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

static void test_reinf_basics(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	CHECK(gp_chunk_reinf_add(&chunk, 3, 64, 5, GP_FACE_NORTH, 32));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(&chunk, 3, 64, 5, &state));
	CHECK(state.count == 1);
	CHECK(state.face[0] == GP_FACE_NORTH);
	CHECK(state.durability[0] == 32);

	// untouched face is not protected
	CHECK(!gp_chunk_reinf_face_active(&chunk, 3, 64, 5, GP_FACE_SOUTH));
	// untouched block has no record at all
	CHECK(!gp_chunk_reinf_get(&chunk, 3, 64, 6, &state));

	gp_chunk_reinf_free(&chunk);
}

static void test_reinf_slot_cap(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	CHECK(gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_NORTH, 32));
	CHECK(gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_SOUTH, 32));
	CHECK(gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_EAST, 32));
	CHECK(gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_WEST, 32));

	// fifth face has no room left
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_UP, 32));
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_DOWN, 32));

	// an existing face stays reachable even when every slot is taken
	CHECK(gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_NORTH, 256));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(&chunk, 0, 0, 0, &state));
	CHECK(state.count == 4);
	CHECK(state.durability[0] == 256);

	// a refused reapplication must not consume a slot or change anything
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_NORTH, 32));
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_NORTH, 256)); // equal is refused too
	CHECK(gp_chunk_reinf_get(&chunk, 0, 0, 0, &state));
	CHECK(state.count == 4);
	CHECK(state.durability[0] == 256);

	// zero is not a tier value and is refused
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_SOUTH, 0));

	// a refusal on a block with no record must not leave one behind
	CHECK(!gp_chunk_reinf_add(&chunk, 2, 0, 2, GP_FACE_UP, 0));
	CHECK(chunk.reinf_entries == 1);
	CHECK(!gp_chunk_reinf_get(&chunk, 2, 0, 2, &state));

	gp_chunk_reinf_free(&chunk);
}

static void test_reinf_consume(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	CHECK(gp_chunk_reinf_add(&chunk, 1, 70, 1, GP_FACE_EAST, 4));

	// three breaks absorbed, block survives
	CHECK(gp_chunk_reinf_consume(&chunk, 1, 70, 1, GP_FACE_EAST));
	CHECK(gp_chunk_reinf_consume(&chunk, 1, 70, 1, GP_FACE_EAST));
	CHECK(gp_chunk_reinf_consume(&chunk, 1, 70, 1, GP_FACE_EAST));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(&chunk, 1, 70, 1, &state));
	CHECK(state.durability[0] == 1);

	// fourth break exhausts the face, so the block goes through
	CHECK(!gp_chunk_reinf_consume(&chunk, 1, 70, 1, GP_FACE_EAST));
	CHECK(!gp_chunk_reinf_get(&chunk, 1, 70, 1, &state));
	CHECK(chunk.reinf_entries == 0);

	gp_chunk_reinf_free(&chunk);
}

static void test_reinf_faces_are_independent(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_chunk_reinf_add(&chunk, 5, 5, 5, GP_FACE_NORTH, 8);
	gp_chunk_reinf_add(&chunk, 5, 5, 5, GP_FACE_SOUTH, 8);

	for (u32 i = 0; i < 8; i++)
		CHECK(gp_chunk_reinf_consume(&chunk, 5, 5, 5, GP_FACE_NORTH) == (i < 7));

	// north is gone, south must be untouched
	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(&chunk, 5, 5, 5, &state));
	CHECK(state.count == 1);

	u32 south = 0;
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		if (state.face[i] == GP_FACE_SOUTH)
			south = state.durability[i];
	CHECK(south == 8);

	gp_chunk_reinf_free(&chunk);
}

static void test_reinf_move(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_chunk_reinf_add(&chunk, 2, 64, 2, GP_FACE_NORTH, 32);
	gp_chunk_reinf_add(&chunk, 2, 64, 2, GP_FACE_UP, 64);

	CHECK(gp_chunk_reinf_move(&chunk, 2, 64, 2, 3, 64, 3));

	gp_block_reinf_t state;
	CHECK(!gp_chunk_reinf_get(&chunk, 2, 64, 2, &state));
	CHECK(gp_chunk_reinf_get(&chunk, 3, 64, 3, &state));
	CHECK(state.count == 2);
	CHECK(state.durability[0] == 32);
	CHECK(state.durability[1] == 64);

	// moving onto an occupied block merges same-face durability instead of
	// silently dropping it
	gp_chunk_reinf_add(&chunk, 4, 64, 4, GP_FACE_NORTH, 16);
	gp_chunk_reinf_add(&chunk, 4, 64, 4, GP_FACE_DOWN, 8);
	CHECK(gp_chunk_reinf_move(&chunk, 3, 64, 3, 4, 64, 4));

	CHECK(gp_chunk_reinf_get(&chunk, 4, 64, 4, &state));
	CHECK(state.count == 3);

	u32 north = 0, up = 0, down = 0;
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (state.face[i] == GP_FACE_NORTH)
			north = state.durability[i];
		if (state.face[i] == GP_FACE_UP)
			up = state.durability[i];
		if (state.face[i] == GP_FACE_DOWN)
			down = state.durability[i];
	}
	CHECK(north == 48); // 32 + 16
	CHECK(up == 64);
	CHECK(down == 8);

	gp_chunk_reinf_free(&chunk);
}

static void test_shield_points(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 0);
	CHECK(gp_chunk_shield_add(&chunk, 1, 1, 1, 100, 128) == 100);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 100);

	// capped at the pool share
	CHECK(gp_chunk_shield_add(&chunk, 1, 1, 1, 100, 128) == 28);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 128);
	CHECK(gp_chunk_shield_add(&chunk, 1, 1, 1, 100, 128) == 0);

	CHECK(gp_chunk_shield_take(&chunk, 1, 1, 1, 8) == 8);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 120);
	CHECK(gp_chunk_shield_take(&chunk, 1, 1, 1, 500) == 120);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 0);
	CHECK(chunk.shield_entries == 0);

	gp_chunk_reinf_free(&chunk);
}

typedef struct
{
	u32 count;
	u64 total;
} count_ctx;

static void count_shield(void *user, u32 x, u32 y, u32 z, u32 points)
{
	count_ctx *ctx = user;
	(void)x;
	(void)y;
	(void)z;
	ctx->count++;
	ctx->total += points;
}

static void test_sparse_density(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	// 100 reinforced blocks scattered through a 16x384x16 chunk
	for (u32 i = 0; i < 100; i++)
		gp_chunk_reinf_add(&chunk, i % 16, 64 + i / 16, i % 16, GP_FACE_NORTH, 32);

	CHECK(chunk.reinf_entries == 100);

	count_ctx ctx = {0, 0};
	gp_chunk_shield_add(&chunk, 0, 0, 0, 4096, 4096);
	gp_chunk_shield_visit(&chunk, count_shield, &ctx);
	CHECK(ctx.count == 1);
	CHECK(ctx.total == 4096);

	// The whole point: 100 blocks must cost far less than the dense
	// 16 * 384 * 16 volume would.
	u64 dense = 16ull * 384ull * 16ull * 10ull;
	u64 sparse = gp_chunk_reinf_memory(&chunk);
	printf("  100 reinforced blocks: %llu bytes sparse vs %llu dense (%.1fx)\n",
	       (unsigned long long)sparse, (unsigned long long)dense, (double)dense / (double)sparse);
	CHECK(sparse < dense / 4);

	// exhausting every record must let the map shrink back down
	for (u32 i = 0; i < 100; i++)
		for (u32 k = 0; k < 32; k++)
			gp_chunk_reinf_consume(&chunk, i % 16, 64 + i / 16, i % 16, GP_FACE_NORTH);

	CHECK(chunk.reinf_entries == 0);
	// capacity is deliberately not handed back, only the records are, so a
	// wall that gets destroyed and rebuilt does not thrash the allocator
	CHECK(gp_chunk_reinf_memory(&chunk) == sparse);

	gp_chunk_reinf_free(&chunk);
}

static void test_bounds(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	// out of range positions are rejected rather than corrupting the index
	CHECK(!gp_chunk_reinf_add(&chunk, 16, 0, 0, GP_FACE_NORTH, 32));
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 16, GP_FACE_NORTH, 32));
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 384, 0, GP_FACE_NORTH, 32));
	CHECK(!gp_chunk_reinf_add(&chunk, 0, 0, 0, GP_FACE_COUNT, 32));

	// a lower world must not alias the overworld
	gp_chunk_reinf nether;
	gp_chunk_reinf_init(&nether, 128);
	gp_chunk_reinf_add(&nether, 0, 0, 0, GP_FACE_NORTH, 32);
	CHECK(gp_chunk_reinf_get(&nether, 0, 0, 0, &(gp_block_reinf_t){0}));
	gp_chunk_reinf_free(&nether);

	gp_chunk_reinf_free(&chunk);
}

/* --- shield ---------------------------------------------------------------- */

/* Test sink backed by a plain array, so shield logic can be checked without a
   world. Blocks outside the table are simply unprotectable. */
#define SINK_MAX 64

typedef struct
{
	gp_pos pos[SINK_MAX];
	u32 points[SINK_MAX];
	u32 count;
	gp_pos reject; // a position the host refuses to protect
	bool use_reject;
} fake_world;

static int fake_index(const fake_world *w, gp_pos p)
{
	for (u32 i = 0; i < w->count; i++)
		if (gp_pos_eq(w->pos[i], p))
			return (int)i;
	return -1;
}

static void fake_reset(fake_world *w)
{
	memset(w, 0, sizeof(*w));
	w->pos[0] = (gp_pos){0, 0, 0};
	w->count = 1; // origin always present
}

static void fake_fill(fake_world *w, u32 n)
{
	fake_reset(w);
	for (u32 i = 1; i < n; i++)
	{
		w->pos[i] = (gp_pos){(i32)i, 0, 0};
		w->count = i + 1;
	}
}

static bool fake_protectable(void *user, gp_pos p)
{
	fake_world *w = user;
	if (w->use_reject && gp_pos_eq(p, w->reject))
		return false;
	return fake_index(w, p) >= 0;
}

typedef struct
{
	u32 calls;
	gp_pos last_pos;
	u64 last_now;
} cover_log;

/* A sink carries one user pointer for every callback, so the fake world and the
   coverage log have to live in one allocation. fake_world leads, which lets
   protectable and grant keep casting user to fake_world*. */
typedef struct
{
	fake_world w;
	cover_log log;
} cover_ctx;

static void record_cover(void *user, gp_pos p, u64 now)
{
	cover_ctx *ctx = user;
	ctx->log.calls++;
	ctx->log.last_pos = p;
	ctx->log.last_now = now;
}

static u32 fake_grant(void *user, gp_pos p, u32 amount, u32 cap)
{
	fake_world *w = user;
	int i = fake_index(w, p);
	if (i < 0)
		return 0;

	u32 cur = w->points[i];
	if (cap && cur + amount > cap)
		amount = cap > cur ? cap - cur : 0;
	if (amount == 0)
		return 0;

	w->points[i] = cur + amount;
	return amount;
}

static gp_shield_sink fake_sink(fake_world *w)
{
	gp_shield_sink sink;

	// zero first: the optional callbacks have to be NULL when unused, and a
	// partially filled sink would leave them holding stack garbage
	memset(&sink, 0, sizeof(sink));
	sink.user = w;
	sink.protectable = fake_protectable;
	sink.grant = fake_grant;
	return sink;
}

static void test_shield_share(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	CHECK(s != NULL);
	CHECK(gp_shield_share(s) == 0); // nothing selected, no share

	for (u32 i = 0; i < 32; i++)
		gp_shield_add_target(s, (gp_pos){(i32)i, 0, 0});
	// 4096 / 32 == 128 exactly
	CHECK(gp_shield_share(s) == 128);

	gp_shield_clear_targets(s);
	for (u32 i = 0; i < 40; i++)
		gp_shield_add_target(s, (gp_pos){(i32)i, 0, 0});
	// 4096 / 40 == 102.4, rounds up
	CHECK(gp_shield_share(s) == 103);

	gp_shield_destroy(s);
}

static void test_shield_round_robin(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	fake_world w;
	fake_fill(&w, 40); // 40 blocks, budget 20 per window

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){100, 0, 0}, 64);
	for (u32 i = 0; i < 40; i++)
		gp_shield_add_target(s, (gp_pos){(i32)i, 0, 0});

	gp_shield_sink sink = fake_sink(&w);

	// first window: the first 20 targets get one point each, then it stops
	// exactly where it ran out
	u32 granted = gp_shield_run_window(s, &sink, 0);
	CHECK(granted == 20);
	CHECK(s->cursor == 20);
	for (u32 i = 0; i < 20; i++)
		CHECK(w.points[i] == 1);
	for (u32 i = 20; i < 40; i++)
		CHECK(w.points[i] == 0);

	// second window resumes at 20 and finishes the remaining half
	granted = gp_shield_run_window(s, &sink, 0);
	CHECK(granted == 20);
	CHECK(s->cursor == 0); // wrapped all the way round
	for (u32 i = 20; i < 40; i++)
		CHECK(w.points[i] == 1);

	// keep going until every block reaches its share of 103. At 20 points per
	// window over 40 blocks that is 103 * 40 / 20 == 206 windows.
	u32 windows = 2;
	u32 got = 0;
	while (windows < 1000)
	{
		got = gp_shield_run_window(s, &sink, 0);
		windows++;
		if (got == 0)
			break;
	}

	CHECK(got == 0); // loop only exits once a window has nothing left to do
	CHECK(windows < 1000);
	for (u32 i = 0; i < 40; i++)
		CHECK(w.points[i] == 103);

	// once everything is capped, a window is a no-op and the cursor stays put
	u32 cursor_before = s->cursor;
	CHECK(gp_shield_run_window(s, &sink, 0) == 0);
	CHECK(s->cursor == cursor_before);

	gp_shield_destroy(s);
}

static void test_shield_share_shrinks_with_new_targets(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	fake_world w;
	fake_fill(&w, 2); // block 0 and block 1

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	gp_shield_add_target(s, (gp_pos){0, 0, 0});
	CHECK(gp_shield_share(s) == 4096);

	gp_shield_add_target(s, (gp_pos){1, 0, 0});
	CHECK(gp_shield_share(s) == 2048);

	gp_shield_sink sink = fake_sink(&w);

	// one window hands out 20 points total, and the round robin alternates so
	// the budget is split evenly rather than drained into the first target
	gp_shield_run_window(s, &sink, 0);
	CHECK(w.points[0] == 10);
	CHECK(w.points[1] == 10);

	// a block that was topped up under the old, larger share is not drained
	// when the share shrinks, it is simply left above the new ceiling
	for (u32 i = 0; i < 400; i++)
		gp_shield_run_window(s, &sink, 0);
	CHECK(w.points[0] == 2048);
	CHECK(w.points[1] == 2048);

	gp_shield_destroy(s);
}

static void test_shield_fuel_and_power(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	fake_world w;
	fake_fill(&w, 3);

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	for (u32 i = 0; i < 3; i++)
		gp_shield_add_target(s, (gp_pos){(i32)i, 0, 0});

	gp_shield_sink sink = fake_sink(&w);
	u32 granted = 0;

	// unpowered, nothing happens
	gp_shield_tick(s, 1, &sink, &granted);
	CHECK(granted == 0);
	CHECK(!s->running);

	// powered but fuel already gone
	gp_shield_set_powered(s, true);
	gp_shield_set_fuel(s, 0);
	gp_shield_tick(s, 2, &sink, &granted);
	CHECK(granted == 0);
	CHECK(!s->running);

	// powered with fuel
	gp_shield_set_fuel(s, cfg.shield_fuel_max);
	gp_shield_tick(s, 3, &sink, &granted);
	CHECK(s->running);
	CHECK(granted > 0);
	CHECK(s->fuel < cfg.shield_fuel_max); // burned by the elapsed second

	// losing power stops it again
	u32 kept = s->fuel;
	gp_shield_set_powered(s, false);
	gp_shield_tick(s, 4, &sink, &granted);
	CHECK(granted == 0);
	CHECK(!s->running);
	CHECK(s->fuel == kept); // idle shields do not burn fuel

	gp_shield_destroy(s);
}

static void test_shield_range_mode(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.shield_range = 2; // 5x5x5 = 125 blocks

	fake_world w;
	fake_reset(&w);
	w.reject = (gp_pos){1, 0, 0}; // host says this one cannot be protected
	w.use_reject = true;

	// the shield's own block, a rejected neighbour, and two inside range
	w.pos[1] = (gp_pos){0, 0, 1};
	w.pos[2] = (gp_pos){0, 0, 2};
	w.pos[3] = (gp_pos){0, 0, 3}; // outside range 2
	w.count = 4;

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	// no targets selected, so range mode applies
	CHECK(s->target_count == 0);

	gp_shield_sink sink = fake_sink(&w);
	u32 granted = gp_shield_run_window(s, &sink, 0);

	// both in-range blocks are covered, the rejected one is skipped, the
	// out-of-range one is untouched, and the shield never covers itself
	CHECK(granted == 2);
	CHECK(w.points[0] == 0); // the shield itself
	CHECK(w.points[1] > 0);
	CHECK(w.points[2] > 0);
	CHECK(w.points[3] == 0); // outside the range cube

	gp_shield_destroy(s);
}

/* Range mode has two properties worth pinning: the pool is divided across the
   eligible cells instead of handing the whole thing to each one, and successive
   windows resume where the last stopped rather than restarting at the same
   corner of the cube. */
static void test_shield_range_share_and_fairness(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.shield_range = 2; // side 5
	cfg.shield_pool = 100;
	cfg.shield_regen = 1;  // one point per window, so the cursor has to do the work

	fake_world w;
	fake_reset(&w);

	// three protectable cells, spread through the cube. With side 5 the linear
	// cell index is x + z*25, so these are cells 1, 25 and 50.
	w.pos[1] = (gp_pos){1, 0, 0};
	w.pos[2] = (gp_pos){0, 0, 1};
	w.pos[3] = (gp_pos){0, 0, 2};
	w.count = 4;

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	gp_shield_sink sink = fake_sink(&w);

	for (u32 window = 0; window < 3; window++)
		gp_shield_run_window(s, &sink, 0);

	// cell 0 is the shield itself, so exactly three cells are eligible and the
	// ceiling is ceil(100 / 3) = 34
	CHECK(w.points[1] == 1);
	CHECK(w.points[2] == 1);
	CHECK(w.points[3] == 1);
	CHECK(w.points[0] == 0); // the shield never covers itself

	// run it out: every eligible cell tops up at the shared ceiling, never the pool
	for (u32 window = 0; window < 200; window++)
		gp_shield_run_window(s, &sink, 0);

	CHECK(w.points[1] == 34);
	CHECK(w.points[2] == 34);
	CHECK(w.points[3] == 34);

	gp_shield_destroy(s);
}

/* With nothing protectable in range the shield must grant nothing rather than
   divide by zero or cover the origin. */
static void test_shield_range_with_nothing_protectable(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.shield_range = 2;

	fake_world w;
	fake_reset(&w);
	w.use_reject = true;
	w.reject = (gp_pos){5, 5, 5}; // nothing else is registered either

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 64);
	gp_shield_sink sink = fake_sink(&w);

	CHECK(gp_shield_run_window(s, &sink, 0) == 0);
	CHECK(gp_shield_run_window(s, &sink, 0) == 0);
	CHECK(w.points[0] == 0);

	gp_shield_destroy(s);
}

static void test_shield_target_editing(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_shield *s = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 4);
	gp_shield *other = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 4);

	CHECK(gp_shield_add_target(s, (gp_pos){1, 0, 0}));
	CHECK(gp_shield_add_target(s, (gp_pos){2, 0, 0}));
	// duplicates are rejected
	CHECK(!gp_shield_add_target(s, (gp_pos){1, 0, 0}));

	// capacity is respected
	CHECK(gp_shield_add_target(s, (gp_pos){3, 0, 0}));
	CHECK(gp_shield_add_target(s, (gp_pos){4, 0, 0}));
	CHECK(!gp_shield_add_target(s, (gp_pos){5, 0, 0}));
	CHECK(s->target_count == 4);

	// removal keeps the round robin in bounds
	s->cursor = 3;
	CHECK(gp_shield_remove_target(s, (gp_pos){4, 0, 0}));
	CHECK(s->target_count == 3);
	CHECK(s->cursor < s->target_count);

	// split recipe: one filled plus one empty yields two identical lists
	CHECK(gp_shield_copy_targets(other, s));
	CHECK(other->target_count == s->target_count);

	// merge recipe appends without duplicating
	gp_shield *third = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 8);
	CHECK(gp_shield_add_target(third, (gp_pos){2, 0, 0})); // overlaps with s
	CHECK(gp_shield_add_target(third, (gp_pos){9, 0, 0}));
	CHECK(gp_shield_merge_targets(other, third) == 1);
	CHECK(other->target_count == 4);

	gp_shield_destroy(s);
	gp_shield_destroy(other);
	gp_shield_destroy(third);
}

static void test_shield_decay(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	// fresh points get a full interval of grace
	gp_chunk_shield_add(&chunk, 1, 1, 1, 10, 0);
	gp_chunk_shield_add(&chunk, 2, 1, 1, 10, 0);
	CHECK(!gp_chunk_decay_due(&chunk, 299));

	// first sweep schedules rather than fires
	u32 touched = 0;
	CHECK(gp_chunk_shield_decay(&chunk, 299, 300, 1, &touched, NULL) == 0);
	CHECK(chunk.next_decay == 599);

	// nothing due yet
	CHECK(gp_chunk_shield_decay(&chunk, 400, 300, 1, &touched, NULL) == 0);

	// one point off each block when the sweep lands
	u32 removed = gp_chunk_shield_decay(&chunk, 599, 300, 1, &touched, NULL);
	CHECK(removed == 2);
	CHECK(touched == 2);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 9);
	CHECK(gp_chunk_shield_get(&chunk, 2, 1, 1) == 9);
	CHECK(chunk.next_decay == 899);

	// drain both blocks completely, they should leave the sparse map
	gp_chunk_shield_take(&chunk, 1, 1, 1, 9);
	gp_chunk_shield_take(&chunk, 2, 1, 1, 9);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 0);
	CHECK(gp_chunk_shield_get(&chunk, 2, 1, 1) == 0);
	CHECK(chunk.shield_entries == 0);

	// an empty chunk stops being due at all
	CHECK(!gp_chunk_decay_due(&chunk, 99999));

	gp_chunk_reinf_free(&chunk);
}

static void test_break_resolution(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_break_result r;

	// nothing on the block at all, it breaks for free
	CHECK(gp_chunk_resolve_break(&chunk, 1, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.block_broke);
	CHECK(r.shield_absorbed == 0);

	// shield points absorb breaks one at a time
	gp_chunk_shield_add(&chunk, 1, 1, 1, 3, 0);
	CHECK(gp_chunk_resolve_break(&chunk, 1, 1, 1, GP_FACE_NORTH, &r));
	CHECK(!r.block_broke);
	CHECK(r.shield_absorbed == 1);
	CHECK(r.shield_left == 2);

	CHECK(gp_chunk_resolve_break(&chunk, 1, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.shield_left == 1);
	CHECK(gp_chunk_resolve_break(&chunk, 1, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.shield_absorbed == 1);
	CHECK(r.shield_left == 0);

	// fourth break has nothing left
	CHECK(gp_chunk_resolve_break(&chunk, 1, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.block_broke);

	// a reinforced face absorbs the break and the shield is left alone
	gp_chunk_reinf_add(&chunk, 2, 1, 1, GP_FACE_NORTH, 3);
	gp_chunk_shield_add(&chunk, 2, 1, 1, 10, 0);

	CHECK(gp_chunk_resolve_break(&chunk, 2, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.reinf_survived);
	CHECK(r.shield_absorbed == 0);
	CHECK(r.shield_left == 10); // shield untouched
	CHECK(chunk.reinf_entries == 1);
	CHECK(chunk.shield_entries == 1);

	// two breaks absorbed by reinforcement, then the face runs dry and the
	// shield picks up where reinforcement stopped
	CHECK(gp_chunk_resolve_break(&chunk, 2, 1, 1, GP_FACE_NORTH, &r));
	CHECK(r.reinf_survived);
	CHECK(r.shield_absorbed == 0);

	CHECK(gp_chunk_resolve_break(&chunk, 2, 1, 1, GP_FACE_NORTH, &r));
	CHECK(!r.reinf_survived);
	CHECK(r.shield_absorbed == 1);
	CHECK(r.shield_left == 9);
	CHECK(chunk.reinf_entries == 0); // block record dropped once unprotected

	// an unprotected face on a block whose other face is reinforced still
	// falls through to the shield
	gp_chunk_reinf_add(&chunk, 3, 1, 1, GP_FACE_NORTH, 5);
	gp_chunk_shield_add(&chunk, 3, 1, 1, 4, 0);
	CHECK(gp_chunk_resolve_break(&chunk, 3, 1, 1, GP_FACE_SOUTH, &r));
	CHECK(!r.reinf_survived);
	CHECK(r.shield_absorbed == 1);
	CHECK(r.shield_left == 3);
	CHECK(chunk.reinf_entries == 1); // the north face is still reinforced

	gp_chunk_reinf_free(&chunk);
}

/* --- serialization ---------------------------------------------------------- */

/* Round trips a chunk through a TKV object and back, verifying every reinforced
   face and every shield point survives. */
static void test_chunk_pack_roundtrip(void)
{
	gp_chunk_reinf src, dst;
	gp_chunk_reinf_init(&src, 384);

	gp_chunk_reinf_add(&src, 0, 0, 0, GP_FACE_NORTH, 32);
	gp_chunk_reinf_add(&src, 0, 0, 0, GP_FACE_SOUTH, 256);
	gp_chunk_reinf_add(&src, 15, 383, 15, GP_FACE_EAST, 7);
	gp_chunk_reinf_add(&src, 8, 100, 3, GP_FACE_WEST, 1);
	gp_chunk_shield_add(&src, 0, 0, 0, 90, 4096);
	gp_chunk_shield_add(&src, 4, 4, 4, 1, 4096);
	src.next_decay = 1234567;

	CHECK(src.reinf_entries == 3);
	CHECK(src.shield_entries == 2);

	arena *a = arena_create(65536);
	tkv_object obj = gp_chunk_pack(&src, a);
	CHECK(obj != NULL);

	gp_chunk_reinf_init(&dst, 384);
	CHECK(gp_chunk_unpack(obj, &dst));

	CHECK(dst.chunk_height == src.chunk_height);
	CHECK(dst.next_decay == src.next_decay);
	CHECK(dst.reinf_entries == src.reinf_entries);
	CHECK(dst.shield_entries == src.shield_entries);

	/* Compares the meaningful fields rather than the struct bytes, since the
	   packed record does not round trip padding or the slot count. */
	gp_block_reinf_t before, after;
	const u32 probes[][3] = {{0, 0, 0}, {15, 383, 15}, {8, 100, 3}};
	for (size_t p = 0; p < sizeof(probes) / sizeof(probes[0]); p++)
	{
		CHECK(gp_chunk_reinf_get(&src, probes[p][0], probes[p][1], probes[p][2], &before));
		CHECK(gp_chunk_reinf_get(&dst, probes[p][0], probes[p][1], probes[p][2], &after));
		CHECK(before.count == after.count);
		for (u8 s = 0; s < GP_MAX_REINFORCED_FACES; s++)
		{
			CHECK(before.face[s] == after.face[s]);
			CHECK(before.durability[s] == after.durability[s]);
		}
	}

	CHECK(gp_chunk_shield_get(&src, 0, 0, 0) == 90);
	CHECK(gp_chunk_shield_get(&dst, 0, 0, 0) == 90);

	// a block with nothing on it must stay empty rather than materialize
	gp_block_reinf_t absent;
	CHECK(!gp_chunk_reinf_get(&dst, 9, 9, 9, &absent));
	CHECK(gp_chunk_shield_get(&dst, 9, 9, 9) == 0);

	arena_destroy(a);
	gp_chunk_reinf_free(&dst);
	gp_chunk_reinf_free(&src);
}

/* Records must be spread across more than one array group, otherwise only the
   small case is ever exercised. */
static void test_chunk_pack_group_spill(void)
{
	gp_chunk_reinf src, dst;
	gp_chunk_reinf_init(&src, 384);

	const u32 total = GP_RECORDS_PER_GROUP + 200;
	for (u32 i = 0; i < total; i++)
		gp_chunk_reinf_add(&src, i % 16, i / 256, (i / 16) % 16, GP_FACE_NORTH, (u16)(i % 251 + 1));

	CHECK(src.reinf_entries == total);

	arena *a = arena_create(1 << 20);
	tkv_object obj = gp_chunk_pack(&src, a);
	CHECK(obj != NULL);

	gp_chunk_reinf_init(&dst, 384);
	CHECK(gp_chunk_unpack(obj, &dst));
	CHECK(dst.reinf_entries == total);

	for (u32 i = 0; i < total; i++)
	{
		gp_block_reinf_t got;
		CHECK(gp_chunk_reinf_get(&dst, i % 16, i / 256, (i / 16) % 16, &got));
		CHECK(got.durability[0] == (u16)(i % 251 + 1));
	}

	arena_destroy(a);
	gp_chunk_reinf_free(&dst);
	gp_chunk_reinf_free(&src);
}

static void test_shield_pack_roundtrip(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_pos pos = {.x = -17, .y = -60, .z = 4096};
	gp_shield *src = gp_shield_create(&cfg, pos, 8);
	CHECK(src != NULL);

	gp_shield_add_target(src, (gp_pos){.x = 1, .y = 2, .z = 3});
	gp_shield_add_target(src, (gp_pos){.x = -4, .y = 5, .z = -6});
	gp_shield_add_target(src, (gp_pos){.x = 7, .y = 8, .z = 9});
	src->cursor = 2;
	src->budget = 13;
	src->fuel = 40;
	src->powered = true;
	src->running = true;
	src->window_start = 987654;
	src->pool = 777;
	src->range = 5;
	src->fuel_per_sec = 3;
	src->window = 2;

	arena *a = arena_create(65536);
	tkv_object obj = gp_shield_pack(src, a);
	CHECK(obj != NULL);

	gp_shield *dst = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, 8);
	CHECK(gp_shield_unpack(obj, dst));

	CHECK(gp_pos_eq(dst->pos, src->pos));
	CHECK(dst->target_count == src->target_count);
	CHECK(dst->pool == src->pool);
	CHECK(dst->range == src->range);
	CHECK(dst->window == src->window);
	CHECK(dst->fuel_per_sec == src->fuel_per_sec);
	CHECK(dst->cursor == src->cursor);
	CHECK(dst->budget == src->budget);
	CHECK(dst->fuel == src->fuel);
	CHECK(dst->powered == src->powered);
	CHECK(dst->running == src->running);
	CHECK(dst->window_start == src->window_start);

	for (u32 i = 0; i < src->target_count; i++)
		CHECK(gp_pos_eq(dst->targets[i], src->targets[i]));

	arena_destroy(a);
	gp_shield_destroy(dst);
	gp_shield_destroy(src);
}

/* Whole world round trip through a gzip stream, covering several chunks,
   coordinates, config and registered shields. */
static void test_world_pack_roundtrip(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.chunk_height = 384;

	gp_world *src = gp_world_create();
	gp_world_set_config(src, &cfg);

	gp_chunk_reinf *c00 = gp_world_chunk(src, 0, 0);
	gp_chunk_reinf *c12 = gp_world_chunk(src, 1, -2);
	gp_chunk_reinf *cm9 = gp_world_chunk(src, -9, 40);
	gp_chunk_reinf_add(c00, 1, 2, 3, GP_FACE_NORTH, 32);
	gp_chunk_reinf_add(c00, 1, 2, 3, GP_FACE_UP, 5);
	gp_chunk_shield_add(c00, 1, 2, 3, 17, 4096);
	gp_chunk_reinf_add(c12, 15, 383, 15, GP_FACE_DOWN, 256);
	gp_chunk_shield_add(c12, 0, 0, 0, 3, 4096);
	gp_chunk_reinf_add(cm9, 7, 70, 7, GP_FACE_WEST, 64);

	// an unloaded chunk must not reach the save
	gp_chunk_reinf *c34 = gp_world_chunk(src, 3, 4);
	gp_chunk_reinf_add(c34, 0, 0, 0, GP_FACE_EAST, 9);
	gp_world_unload_chunk(src, 3, 4);

	gp_shield *shield = gp_shield_create(&cfg, (gp_pos){5, 6, 7}, 16);
	gp_shield_add_target(shield, (gp_pos){.x = 1, .y = 2, .z = 3});
	gp_shield_add_target(shield, (gp_pos){.x = -1, .y = 2, .z = -3});
	shield->fuel = 11;
	shield->powered = true;
	gp_world_add_shield(src, shield);

	const char *path = "build/serialize_test.gz";

	stream_t out;
	CHECK(stream_open_write(path, 1, &out) == 0);
	CHECK(gp_world_write_stream(src, &out) == 0);
	stream_close(&out);

	gp_world *dst = gp_world_create();
	gp_shield *pool[4] = {NULL, NULL, NULL, NULL};

	stream_t in;
	CHECK(stream_open_read(path, 1, &in) == 0);

	arena *scratch = arena_create(4096);
	arena *meta = arena_create(4096);

	tkv_object header = tkv_read_from_stream(&in, scratch, meta);
	CHECK(header != NULL);

	i32 restored = gp_world_unpack(header, &in, dst, pool, 4, 16, scratch);
	CHECK(restored == 1);

	stream_close(&in);

	CHECK(dst->cfg.chunk_height == 384);
	CHECK(dst->chunk_count == 3); // the unloaded chunk was not written

	gp_block_reinf_t got;
	CHECK(gp_chunk_reinf_get(gp_world_find_chunk(dst, 0, 0), 1, 2, 3, &got));
	CHECK(got.face[0] == GP_FACE_NORTH && got.durability[0] == 32);
	CHECK(got.face[1] == GP_FACE_UP && got.durability[1] == 5);

	CHECK(gp_chunk_reinf_get(gp_world_find_chunk(dst, 1, -2), 15, 383, 15, &got));
	CHECK(got.face[0] == GP_FACE_DOWN && got.durability[0] == 256);

	CHECK(gp_chunk_reinf_get(gp_world_find_chunk(dst, -9, 40), 7, 70, 7, &got));
	CHECK(got.face[0] == GP_FACE_WEST && got.durability[0] == 64);

	CHECK(gp_world_find_chunk(dst, 3, 4) == NULL);

CHECK(gp_chunk_shield_get(gp_world_find_chunk(dst, 0, 0), 1, 2, 3) == 17);
	CHECK(gp_chunk_shield_get(gp_world_find_chunk(dst, 1, -2), 0, 0, 0) == 3);

	CHECK(restored == 1 && pool[0] != NULL);
	CHECK(gp_pos_eq(pool[0]->pos, (gp_pos){.x = 5, .y = 6, .z = 7}));
	CHECK(pool[0]->target_count == 2);
	CHECK(pool[0]->fuel == 11);
	CHECK(pool[0]->powered);
	CHECK(gp_world_find_shield(dst, (gp_pos){.x = 5, .y = 6, .z = 7}) == pool[0]);

	arena_destroy(scratch);
	arena_destroy(meta);

	for (u32 i = 0; i < 4; i++)
		gp_shield_destroy(pool[i]);

	gp_shield_destroy(shield);
	gp_world_destroy(dst);
	gp_world_destroy(src);
}

/* A bad version or a truncated frame must be refused rather than accepted as
   an empty chunk, otherwise a corrupt save silently wipes protection data. */
static void test_pack_rejects_bad_input(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_chunk_reinf src, dst;
	gp_chunk_reinf_init(&src, 384);
	gp_chunk_reinf_add(&src, 0, 0, 0, GP_FACE_NORTH, 32);

	arena *a = arena_create(65536);
	tkv_object obj = gp_chunk_pack(&src, a);
	CHECK(obj != NULL);

	// the wrong version must not load
	gp_chunk_reinf_init(&dst, 384);
	CHECK(!gp_chunk_unpack(NULL, &dst));

	arena *b = arena_create(65536);
	tkv_object empty = tkv_object_create_empty(b);
	u32 bogus = GP_FORMAT_VERSION + 1;
	tkv_object_add_field(empty, "ver", TKV_VALUE_U32, TKV_STATE_CONST, &bogus, b);
	CHECK(!gp_chunk_unpack(empty, &dst));

	arena *c = arena_create(65536);
	tkv_object nover = tkv_object_create_empty(c);
	u32 missing = 0;
	tkv_object_add_field(nover, "chh", TKV_VALUE_U32, TKV_STATE_CONST, &missing, c);
	CHECK(!gp_chunk_unpack(nover, &dst));

	arena_destroy(a);
	arena_destroy(b);
	arena_destroy(c);

	gp_chunk_reinf_free(&dst);
	gp_chunk_reinf_free(&src);
}

/* A fifth distinct face at the destination is refused outright rather than
   silently discarded, and a move onto itself must not destroy the record. */
static void test_reinf_move_refuses_overflow(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_chunk_reinf_add(&chunk, 0, 64, 0, GP_FACE_NORTH, 10);
	gp_chunk_reinf_add(&chunk, 1, 64, 0, GP_FACE_SOUTH, 20);
	gp_chunk_reinf_add(&chunk, 1, 64, 0, GP_FACE_EAST, 30);
	gp_chunk_reinf_add(&chunk, 1, 64, 0, GP_FACE_WEST, 40);
	gp_chunk_reinf_add(&chunk, 1, 64, 0, GP_FACE_UP, 50);

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(&chunk, 1, 64, 0, &state));
	CHECK(state.count == 4);

	// the source would bring a fifth distinct face
	CHECK(!gp_chunk_reinf_move(&chunk, 0, 64, 0, 1, 64, 0));

	// nothing moved: the source is intact and so is the destination
	CHECK(gp_chunk_reinf_get(&chunk, 0, 64, 0, &state));
	CHECK(state.count == 1);
	CHECK(state.durability[0] == 10);
	CHECK(gp_chunk_reinf_get(&chunk, 1, 64, 0, &state));
	CHECK(state.count == 4);

	// a move onto itself is a no-op that reports success
	u32 before_entries = chunk.reinf_entries;
	CHECK(gp_chunk_reinf_move(&chunk, 1, 64, 0, 1, 64, 0));
	CHECK(chunk.reinf_entries == before_entries);
	CHECK(gp_chunk_reinf_get(&chunk, 1, 64, 0, &state));
	CHECK(state.count == 4);

	gp_chunk_reinf_free(&chunk);
}

static void test_shield_move_follows_block(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_chunk_shield_add(&chunk, 4, 64, 4, 30, 4096);
	gp_chunk_shield_add(&chunk, 5, 64, 5, 7, 4096);

	CHECK(gp_chunk_shield_transfer(&chunk, 4, 64, 4, &chunk, 5, 64, 5));
	CHECK(gp_chunk_shield_get(&chunk, 4, 64, 4) == 0);
	CHECK(gp_chunk_shield_get(&chunk, 5, 64, 5) == 37); // summed with what was there

	gp_chunk_reinf_free(&chunk);
}

/* Cross chunk movement has to validate each coordinate against the chunk it
   belongs to. Validating the destination against the source would silently write
   to the wrong cell, so this pins the local coordinates on both sides. */
static void test_world_move_across_chunks(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.chunk_height = 384;

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);

	// x 15 of chunk 0 is adjacent to x 0 of chunk 1
	gp_pos from = {.x = 15, .y = 64, .z = 7};
	gp_pos to = {.x = 16, .y = 64, .z = 7};

	gp_chunk_reinf *src = gp_world_chunk(w, 0, 0);
	gp_chunk_reinf_add(src, 15, 64, 7, GP_FACE_NORTH, 32);
	gp_chunk_reinf_add(src, 15, 64, 7, GP_FACE_UP, 64);
	gp_chunk_shield_add(src, 15, 64, 7, 25, 4096);

	CHECK(gp_world_move_block(w, from, to, GP_FACE_EAST));

	// the record must be in chunk 1 at local x 0, not in chunk 0
	gp_chunk_reinf *dst = gp_world_chunk(w, 1, 0);
	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(dst, 0, 64, 7, &state));
	CHECK(state.count == 2);
	CHECK(state.face[0] == GP_FACE_NORTH && state.durability[0] == 32);
	CHECK(state.face[1] == GP_FACE_UP && state.durability[1] == 64);

	CHECK(!gp_chunk_reinf_get(src, 15, 64, 7, &state));
	CHECK(gp_chunk_shield_get(dst, 0, 64, 7) == 25);
	CHECK(gp_chunk_shield_get(src, 15, 64, 7) == 0);

	CHECK(src->reinf_entries == 0);
	CHECK(dst->reinf_entries == 1);
	CHECK(src->shield_entries == 0);
	CHECK(dst->shield_entries == 1);

	gp_world_destroy(w);
}

/* Reinforcement on a side other than the one a block leaves travels with it. */
static void test_world_move_carries_non_source_protection(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);

	gp_chunk_reinf *c = gp_world_chunk(w, 0, 0);
	gp_chunk_reinf_add(c, 4, 64, 4, GP_FACE_UP, 32);
	gp_chunk_reinf_add(c, 4, 64, 4, GP_FACE_NORTH, 16);
	gp_chunk_reinf_add(c, 8, 64, 4, GP_FACE_UP, 24);

	gp_pos pull_from = {.x = 4, .y = 64, .z = 4};
	gp_pos pull_to = {.x = 3, .y = 64, .z = 4};
	gp_pos push_from = {.x = 8, .y = 64, .z = 4};
	gp_pos push_to = {.x = 9, .y = 64, .z = 4};

	CHECK(gp_world_move_block(w, pull_from, pull_to, GP_FACE_WEST));
	CHECK(gp_world_move_block(w, push_from, push_to, GP_FACE_EAST));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(c, 3, 64, 4, &state));
	CHECK(state.count == 2);
	CHECK(state.face[0] == GP_FACE_UP && state.durability[0] == 32);
	CHECK(state.face[1] == GP_FACE_NORTH && state.durability[1] == 16);
	CHECK(!gp_chunk_reinf_get(c, 4, 64, 4, &state));

	CHECK(gp_chunk_reinf_get(c, 9, 64, 4, &state));
	CHECK(state.count == 1);
	CHECK(state.face[0] == GP_FACE_UP && state.durability[0] == 24);
	CHECK(!gp_chunk_reinf_get(c, 8, 64, 4, &state));

	// a bogus direction is rejected
	CHECK(!gp_world_move_block(w, pull_from, pull_to, GP_FACE_NONE));

	gp_world_destroy(w);
}

/* A moving block cannot enter a reinforced face, whether the mover itself is
   protected or not. A rejected move must leave both positions unchanged. */
static void test_world_move_refuses_protected_destination_face(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);
	gp_chunk_reinf *chunk = gp_world_chunk(w, 0, 0);
	gp_chunk_reinf_add(chunk, 4, 64, 4, GP_FACE_UP, 8);
	gp_chunk_reinf_add(chunk, 5, 64, 4, GP_FACE_WEST, 32);

	gp_pos from = {.x = 4, .y = 64, .z = 4};
	gp_pos to = {.x = 5, .y = 64, .z = 4};
	CHECK(!gp_world_move_block(w, from, to, GP_FACE_EAST));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(chunk, 4, 64, 4, &state));
	CHECK(state.count == 1);
	CHECK(state.face[0] == GP_FACE_UP && state.durability[0] == 8);
	CHECK(gp_chunk_reinf_get(chunk, 5, 64, 4, &state));
	CHECK(state.count == 1);
	CHECK(state.face[0] == GP_FACE_WEST && state.durability[0] == 32);

	// This pull's destination face points east, toward its source.
	gp_chunk_reinf_clear(chunk, 5, 64, 4);
	CHECK(gp_chunk_reinf_add(chunk, 3, 64, 4, GP_FACE_EAST, 24));
	CHECK(!gp_world_move_block(w, from, (gp_pos){.x = 3, .y = 64, .z = 4}, GP_FACE_WEST));
	CHECK(gp_chunk_reinf_get(chunk, 4, 64, 4, &state));
	CHECK(gp_chunk_reinf_get(chunk, 3, 64, 4, &state));
	CHECK(state.count == 1);
	CHECK(state.face[0] == GP_FACE_EAST && state.durability[0] == 24);

	gp_world_destroy(w);
}

/* A cross chunk move into a cell that would overflow the destination's faces is
   refused without touching either chunk. */
static void test_world_move_refuses_cross_chunk_overflow(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);

	gp_chunk_reinf *src = gp_world_chunk(w, 0, 0);
	gp_chunk_reinf_add(src, 15, 64, 7, GP_FACE_UP, 8);

	gp_chunk_reinf *dst = gp_world_chunk(w, 1, 0);
	gp_chunk_reinf_add(dst, 0, 64, 7, GP_FACE_NORTH, 1);
	gp_chunk_reinf_add(dst, 0, 64, 7, GP_FACE_SOUTH, 2);
	gp_chunk_reinf_add(dst, 0, 64, 7, GP_FACE_EAST, 3);
	gp_chunk_reinf_add(dst, 0, 64, 7, GP_FACE_WEST, 4);

	gp_pos from = {.x = 15, .y = 64, .z = 7};
	gp_pos to = {.x = 16, .y = 64, .z = 7};

	// UP is a fifth distinct face at the destination
	CHECK(!gp_world_move_block(w, from, to, GP_FACE_EAST));

	gp_block_reinf_t state;
	CHECK(gp_chunk_reinf_get(src, 15, 64, 7, &state));
	CHECK(state.count == 1);
	CHECK(gp_chunk_reinf_get(dst, 0, 64, 7, &state));
	CHECK(state.count == 4);
	CHECK(src->reinf_entries == 1);
	CHECK(dst->reinf_entries == 1);

	gp_world_destroy(w);
}

/* Moving a block that carries no protection is a no-op the host can pass
   through, including when the destination chunk has never been loaded. */
static void test_world_move_without_protection(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);

	gp_pos from = {.x = 100, .y = 64, .z = 100};
	gp_pos to = {.x = 101, .y = 64, .z = 100};

	CHECK(gp_world_move_block(w, from, to, GP_FACE_EAST)); // no chunks tracked yet
	CHECK(gp_world_find_chunk(w, 6, 6) == NULL);            // nothing was created

	gp_chunk_reinf *c = gp_world_chunk(w, 0, 0);
	gp_chunk_reinf_add(c, 4, 64, 4, GP_FACE_UP, 5);

	// the source has a chunk but no reinforcement, so still nothing to do
	CHECK(gp_world_move_block(w, (gp_pos){.x = 4, .y = 64, .z = 4}, (gp_pos){.x = 5, .y = 64, .z = 4},
	                          GP_FACE_EAST));
	CHECK(!gp_world_move_block(w, (gp_pos){.x = 4, .y = 9999, .z = 4},
	                          (gp_pos){.x = 5, .y = 64, .z = 4}, GP_FACE_EAST));

	gp_world_destroy(w);
}

static void test_shield_cover_decay(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);

	gp_chunk_shield_add(&chunk, 1, 1, 1, 10, 0);
	gp_chunk_shield_add(&chunk, 2, 1, 1, 10, 0);

	// both blocks start uncovered
	CHECK(gp_chunk_shield_uncovered(&chunk, 1, 1, 1, 0));
	CHECK(gp_chunk_shield_uncovered(&chunk, 2, 1, 1, 0));

	// a live shield claims the first block until t=1000
	gp_chunk_shield_cover(&chunk, 1, 1, 1, 1000);
	CHECK(!gp_chunk_shield_uncovered(&chunk, 1, 1, 1, 0));
	CHECK(!gp_chunk_shield_uncovered(&chunk, 1, 1, 1, 999));
	CHECK(gp_chunk_shield_uncovered(&chunk, 2, 1, 1, 0)); // untouched
	CHECK(gp_chunk_shield_covered_count(&chunk, 0) == 1);

	// a sweep inside the claim leaves the covered block alone and still bleeds
	// the other one
	u32 touched = 0;
	u32 skipped = 0;
	gp_chunk_shield_decay(&chunk, 299, 300, 1, &touched, &skipped); // schedules only
	CHECK(gp_chunk_shield_decay(&chunk, 599, 300, 1, &touched, &skipped) == 1);
	CHECK(touched == 1);
	CHECK(skipped == 1);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 10); // held at full strength
	CHECK(gp_chunk_shield_get(&chunk, 2, 1, 1) == 9);

	// once the claim lapses the block starts bleeding too. Both blocks are
	// uncovered by now, so this sweep takes from each.
	CHECK(gp_chunk_shield_uncovered(&chunk, 1, 1, 1, 1000));
	CHECK(gp_chunk_shield_decay(&chunk, 1199, 300, 1, &touched, &skipped) == 2);
	CHECK(gp_chunk_shield_get(&chunk, 1, 1, 1) == 9);
	CHECK(gp_chunk_shield_get(&chunk, 2, 1, 1) == 8);

	// a later claim never shortens an existing one, so a slow shield cannot
	// hand a block back to decay while a fast one still holds it
	gp_chunk_shield_cover(&chunk, 1, 1, 1, 5000);
	gp_chunk_shield_cover(&chunk, 1, 1, 1, 2000);
	CHECK(!gp_chunk_shield_uncovered(&chunk, 1, 1, 1, 4999));

	// covering a block that holds no points must not create an entry, or every
	// block a range shield passes over would occupy a slot
	gp_chunk_shield_cover(&chunk, 5, 1, 1, 5000);
	CHECK(gp_chunk_shield_get(&chunk, 5, 1, 1) == 0);
	CHECK(gp_chunk_shield_covered_count(&chunk, 0) == 1);

	gp_chunk_reinf_free(&chunk);
}

static void test_shield_cover_no_budget(void)
{
	// The regression that motivated splitting coverage out of the grant pass:
	// once every target is topped up the budget is spent, and a shield that tied
	// its claim to the grant pass would stop protecting a fully shielded block.
	cover_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	fake_fill(&ctx.w, 2);
	ctx.w.points[0] = 50; // already at the ceiling of a single target

	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.shield_pool = 50; // one target, so the share is 50
	gp_shield *shield = gp_shield_create(&cfg, (gp_pos){.x = 0, .y = 0, .z = 0}, 8);
	CHECK(gp_shield_add_target(shield, (gp_pos){.x = 0, .y = 0, .z = 0}));

	gp_shield_sink sink = fake_sink(&ctx.w);
	sink.cover = record_cover;
	sink.user = &ctx;

	// a window that can grant nothing still has to claim coverage
	CHECK(gp_shield_run_window(shield, &sink, 100) == 0);
	CHECK(ctx.log.calls == 1);
	CHECK(ctx.log.last_now == 100);
	CHECK(ctx.w.points[0] == 50);

	// and a block the sink rejects is never claimed
	memset(&ctx, 0, sizeof(ctx));
	fake_fill(&ctx.w, 2);
	ctx.w.use_reject = true;
	ctx.w.reject = (gp_pos){.x = 1, .y = 0, .z = 0};

	gp_shield *shield2 = gp_shield_create(&cfg, (gp_pos){.x = 0, .y = 0, .z = 0}, 8);
	CHECK(gp_shield_add_target(shield2, (gp_pos){.x = 0, .y = 0, .z = 0}));
	CHECK(gp_shield_add_target(shield2, (gp_pos){.x = 1, .y = 0, .z = 0}));

	gp_shield_sink sink2 = fake_sink(&ctx.w);
	sink2.cover = record_cover;
	sink2.user = &ctx;

	gp_shield_run_window(shield2, &sink2, 200);
	CHECK(ctx.log.calls == 1);
	CHECK(gp_pos_eq(ctx.log.last_pos, (gp_pos){.x = 0, .y = 0, .z = 0}));

	gp_shield_destroy(shield);
	gp_shield_destroy(shield2);
}

static void test_world_shield_cover_end_to_end(void)
{
	// The whole point: a running shield keeps its blocks alive across a decay
	// sweep, and stops once it stops running.
	gp_config cfg;
	gp_config_defaults(&cfg);
	cfg.decay_interval = 300;

	gp_world *w = gp_world_create();
	gp_world_set_config(w, &cfg);

	gp_pos a = {.x = 3, .y = 64, .z = 3};

	gp_chunk_reinf *chunk = gp_world_chunk(w, 0, 0);
	gp_chunk_shield_add(chunk, 3, 64, 3, 20, 0);
	gp_chunk_shield_add(chunk, 4, 64, 3, 20, 0);

	// schedule the chunk's first sweep
	gp_chunk_shield_decay(chunk, 0, cfg.decay_interval, 1, NULL, NULL);
	CHECK(chunk->next_decay == 300);

	// the shield runs and covers `a` through the world helper
	gp_world_shield_cover(w, a, 100);
	CHECK(!gp_chunk_shield_uncovered(chunk, 3, 64, 3, 100));
	CHECK(gp_chunk_shield_covered_count(chunk, 100) == 1);

	// the sweep lands inside the claim: `a` survives, `b` bleeds
	u32 touched = 0;
	u32 skipped = 0;
	CHECK(gp_chunk_shield_decay(chunk, 300, cfg.decay_interval, 1, &touched, &skipped) == 1);
	CHECK(skipped == 1);
	CHECK(gp_chunk_shield_get(chunk, 3, 64, 3) == 20);
	CHECK(gp_chunk_shield_get(chunk, 4, 64, 3) == 19);

	// the shield unloads. Nothing tells the chunk, so the claim has to lapse on
	// its own rather than freezing decay forever.
	CHECK(gp_chunk_shield_uncovered(chunk, 3, 64, 3, 300 + cfg.decay_interval));

	// covering a block whose chunk is not loaded is a no-op, which is what lets a
	// protected chunk decay while the shield's own chunk sits unloaded
	gp_pos far = {.x = 1000, .y = 64, .z = 1000};
	gp_world_shield_cover(w, far, 100);
	CHECK(gp_world_find_chunk(w, 62, 62) == NULL);

	// out of bounds positions are ignored rather than crashing
	gp_world_shield_cover(w, (gp_pos){.x = 3, .y = 99999, .z = 3}, 100);
	gp_world_shield_cover(NULL, a, 100);

	gp_world_destroy(w);
}

static void test_cover_survives_serialization(void)
{
	gp_chunk_reinf chunk;
	gp_chunk_reinf_init(&chunk, 384);
	gp_chunk_shield_add(&chunk, 1, 2, 3, 7, 0);
	gp_chunk_shield_add(&chunk, 2, 2, 3, 9, 0);
	gp_chunk_shield_cover(&chunk, 1, 2, 3, 4242);

	arena *out = arena_create(4096);
	tkv_object obj = gp_chunk_pack(&chunk, out);

	gp_chunk_reinf loaded;
	gp_chunk_reinf_init(&loaded, 384);
	CHECK(gp_chunk_unpack(obj, &loaded));

	CHECK(gp_chunk_shield_get(&loaded, 1, 2, 3) == 7);
	CHECK(gp_chunk_shield_get(&loaded, 2, 2, 3) == 9);

	// the claim came back too, so a reloaded world does not start bleeding a
	// block that is still covered
	CHECK(!gp_chunk_shield_uncovered(&loaded, 1, 2, 3, 4241));
	CHECK(gp_chunk_shield_uncovered(&loaded, 1, 2, 3, 4242));
	CHECK(gp_chunk_shield_uncovered(&loaded, 2, 2, 3, 0));

	arena_destroy(out);
	gp_chunk_reinf_free(&chunk);
	gp_chunk_reinf_free(&loaded);
}

int main(void)
{
	gp_config cfg;
	gp_config_defaults(&cfg);
	printf("config: copper=%u iron=%u pool=%u regen=%u range=%u decay=%u/%us\n", cfg.copper.breaks,
	       cfg.iron.breaks, cfg.shield_pool, cfg.shield_regen, cfg.shield_range, cfg.decay_amount,
	       cfg.decay_interval);

	test_reinf_basics();
	test_reinf_slot_cap();
	test_reinf_consume();
	test_reinf_faces_are_independent();
	test_reinf_move();
	test_reinf_move_refuses_overflow();
	test_shield_move_follows_block();
	test_world_move_across_chunks();
	test_world_move_carries_non_source_protection();
	test_world_move_refuses_protected_destination_face();
	test_world_move_refuses_cross_chunk_overflow();
	test_world_move_without_protection();
	test_shield_cover_decay();
	test_shield_cover_no_budget();
	test_world_shield_cover_end_to_end();
	test_cover_survives_serialization();
	test_shield_points();
	test_sparse_density();
	test_bounds();
	test_shield_share();
	test_shield_round_robin();
	test_shield_share_shrinks_with_new_targets();
	test_shield_fuel_and_power();
	test_shield_range_mode();
	test_shield_range_share_and_fairness();
	test_shield_range_with_nothing_protectable();
	test_shield_target_editing();
	test_shield_decay();
	test_break_resolution();
	test_chunk_pack_roundtrip();
	test_chunk_pack_group_spill();
	test_shield_pack_roundtrip();
	test_world_pack_roundtrip();
	test_pack_rejects_bad_input();

	if (failures == 0)
	{
		printf("all tests passed\n");
		return 0;
	}

	printf("%d check(s) failed\n", failures);
	return 1;
}
