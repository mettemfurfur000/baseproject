#include "gp_abi.h"

#include "griefprot.h"
#include "griefprot_chunk.h"
#include "griefprot_serialize.h"
#include "griefprot_shield.h"
#include "griefprot_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GP_ABI_MAX_WORLDS 64
#define GP_ABI_MAX_SHIELDS 4096
#define GP_ABI_ERROR_LEN 256

/* Flat signatures the host binds to. gp_pos is taken apart before crossing, so
   no struct is ever part of this contract. */
typedef int (*abi_protectable_fn)(uint64_t host, int32_t x, int32_t y, int32_t z);
typedef uint32_t (*abi_grant_fn)(uint64_t host, int32_t x, int32_t y, int32_t z, uint32_t amount, uint32_t cap);
typedef void (*abi_cover_fn)(uint64_t host, int32_t x, int32_t y, int32_t z, uint64_t now);

typedef struct
{
	gp_world *world;
	gp_shield_sink sink;
	abi_protectable_fn protectable;
	abi_grant_fn grant;
	abi_cover_fn cover;
	uint64_t host_id;
	gp_shield **shield_pool;
	uint32_t shield_pool_size;
	uint32_t shield_target_capacity;
	int used;
} abi_world_slot;

typedef struct
{
	gp_shield *shield;
	uint32_t world_slot;
	int used;
} abi_shield_slot;

static abi_world_slot g_worlds[GP_ABI_MAX_WORLDS];
static abi_shield_slot g_shields[GP_ABI_MAX_SHIELDS];

/*
 * Error text is a single buffer, not per thread. Every world mutation a plugin
 * performs happens on the server thread, and a race here would only ever produce
 * a wrong diagnostic string rather than corrupt state. Worth revisiting only if
 * an async host ever calls the shim.
 */
static char g_error[GP_ABI_ERROR_LEN];

static void abi_fail(const char *msg)
{
	snprintf(g_error, sizeof(g_error), "%s", msg ? msg : "unknown");
}

const char *gp_abi_last_error(void)
{
	return g_error[0] ? g_error : NULL;
}

static abi_world_slot *world_slot(gp_h_world handle)
{
	if (handle == 0 || handle > GP_ABI_MAX_WORLDS)
		return NULL;

	abi_world_slot *slot = &g_worlds[handle - 1];
	return slot->used ? slot : NULL;
}

static abi_shield_slot *shield_slot(gp_h_shield handle)
{
	if (handle == 0 || handle > GP_ABI_MAX_SHIELDS)
		return NULL;

	abi_shield_slot *slot = &g_shields[handle - 1];
	return slot->used ? slot : NULL;
}

/* --- sink bridge ---------------------------------------------------------- */

/*
 * The library wants gp_pos by value; the host wants scalars. These three adapt
 * between the two, which is the whole reason the host never sees the struct.
 */

static bool abi_protectable(void *user, gp_pos pos)
{
	abi_world_slot *slot = user;
	if (!slot || !slot->protectable)
		return false;

	return slot->protectable(slot->host_id, pos.x, pos.y, pos.z) != 0;
}

static u32 abi_grant(void *user, gp_pos pos, u32 amount, u32 cap)
{
	abi_world_slot *slot = user;
	if (!slot || !slot->grant)
		return 0;

	return slot->grant(slot->host_id, pos.x, pos.y, pos.z, amount, cap);
}

static void abi_cover(void *user, gp_pos pos, u64 now)
{
	abi_world_slot *slot = user;
	if (!slot || !slot->cover)
		return;

	slot->cover(slot->host_id, pos.x, pos.y, pos.z, now);
}

/* Finds the loaded chunk owning a position without creating one. */
/*
 * Resolves a world position to its chunk plus the in-chunk coordinates.
 *
 * Both halves are needed by every chunk-level call: the chunk pointer to pass,
 * and the local x/z to pass alongside it. Returning only the chunk (as an
 * earlier version of this did) invites passing absolute coordinates, which are
 * correct by accident inside chunk 0 and wrong everywhere else. So the locals
 * come back explicitly and every caller uses them.
 */
typedef struct
{
	gp_chunk_reinf *chunk;
	i32 cx, cz;
	u32 lx, ly, lz;
} abi_chunk_ref;

/* When `create` is zero, an unloaded chunk yields a NULL chunk and success, so
   the caller can treat "not resident" as "no protection here" without
   allocating anything. */
static int chunk_ref(const abi_world_slot *slot, int32_t x, int32_t y, int32_t z, abi_chunk_ref *out,
                     int create)
{
	if (!slot || !slot->world || !out)
		return 0;

	if (!gp_world_pos_to_local((gp_pos){x, y, z}, (i32)slot->world->cfg.chunk_height, &out->cx, &out->cz,
	                           &out->lx, &out->ly, &out->lz))
		return 0;

	out->chunk = create ? gp_world_chunk(slot->world, out->cx, out->cz)
	                    : gp_world_find_chunk(slot->world, out->cx, out->cz);
	return 1;
}

static gp_chunk_reinf *chunk_at_loaded(const abi_world_slot *slot, int32_t x, int32_t y, int32_t z)
{
	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0))
		return NULL;

	return ref.chunk;
}

static gp_chunk_reinf *chunk_loaded_or_create(abi_world_slot *slot, int32_t x, int32_t y, int32_t z)
{
	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 1))
		return NULL;

	return ref.chunk;
}

/* --- ABI self check ------------------------------------------------------- */

uint32_t gp_abi_check(void)
{
	uint32_t failed = 0;

	if (sizeof(void *) != 8)
		failed |= GP_ABI_CHECK_PTR_SIZE;
	if (sizeof(u32) != 4)
		failed |= GP_ABI_CHECK_U32_SIZE;
	if (sizeof(u64) != 8)
		failed |= GP_ABI_CHECK_U64_SIZE;
	if (sizeof(gp_pos) != 12)
		failed |= GP_ABI_CHECK_POS_SIZE;
	if (sizeof(bool) != 1)
		failed |= GP_ABI_CHECK_BOOL_SIZE;
	/* gp_face_t is a C enum, so its width depends on the compiler and on
	   -fshort-enums. Faces cross the boundary as plain int32 values, so the
	   width is irrelevant; what matters is that the enum values themselves are
	   the ones the host hardcodes. Checking sizeof here would fail on a correct
	   build and pass on a broken one. */
	if ((u32)GP_FACE_NONE != 0 || (u32)GP_FACE_COUNT != 7)
		failed |= GP_ABI_CHECK_FACE_VALUES;
	if (sizeof(gp_break_result) == 0)
		failed |= GP_ABI_CHECK_BREAK_RESULT_SIZE;

	/* gp_pos is documented as three i32 in a row, which is what the host
	   assumes when it splits a position into scalars. Verify rather than trust,
	   since a field reorder would be invisible otherwise. */
	if (offsetof(gp_pos, x) != 0 || offsetof(gp_pos, y) != 4 || offsetof(gp_pos, z) != 8)
		failed |= GP_ABI_CHECK_POS_IS_3XI32;

	return failed;
}

int64_t gp_abi_expect(uint32_t bit)
{
	switch (bit)
	{
	case GP_ABI_CHECK_PTR_SIZE:
		return 8;
	case GP_ABI_CHECK_U32_SIZE:
		return 4;
	case GP_ABI_CHECK_U64_SIZE:
		return 8;
	case GP_ABI_CHECK_POS_SIZE:
		return 12;
	case GP_ABI_CHECK_BOOL_SIZE:
		return 1;
	case GP_ABI_CHECK_FACE_VALUES:
		return 1;
	case GP_ABI_CHECK_POS_IS_3XI32:
		return 1;
	default:
		return -1;
	}
}

const char *gp_abi_check_name(uint32_t bit)
{
	switch (bit)
	{
	case GP_ABI_CHECK_PTR_SIZE:
		return "sizeof(void*)";
	case GP_ABI_CHECK_U32_SIZE:
		return "sizeof(u32)";
	case GP_ABI_CHECK_U64_SIZE:
		return "sizeof(u64)";
	case GP_ABI_CHECK_POS_SIZE:
		return "sizeof(gp_pos)";
	case GP_ABI_CHECK_BOOL_SIZE:
		return "sizeof(bool)";
	case GP_ABI_CHECK_FACE_VALUES:
		return "gp_face_t values (NONE=0, COUNT=7)";
	case GP_ABI_CHECK_BREAK_RESULT_SIZE:
		return "sizeof(gp_break_result)";
	case GP_ABI_CHECK_POS_IS_3XI32:
		return "gp_pos is three i32 in order";
	default:
		return "unknown check";
	}
}

/* --- face ids ------------------------------------------------------------- */

uint32_t gp_abi_face_none(void) { return (uint32_t)GP_FACE_NONE; }
uint32_t gp_abi_face_down(void) { return (uint32_t)GP_FACE_DOWN; }
uint32_t gp_abi_face_up(void) { return (uint32_t)GP_FACE_UP; }
uint32_t gp_abi_face_north(void) { return (uint32_t)GP_FACE_NORTH; }
uint32_t gp_abi_face_south(void) { return (uint32_t)GP_FACE_SOUTH; }
uint32_t gp_abi_face_west(void) { return (uint32_t)GP_FACE_WEST; }
uint32_t gp_abi_face_east(void) { return (uint32_t)GP_FACE_EAST; }
uint32_t gp_abi_face_count(void) { return (uint32_t)GP_FACE_COUNT; }

uint32_t gp_abi_version(void)
{
	return (1u << 16) | 0u;
}

/* --- world lifecycle ------------------------------------------------------ */

gp_h_world gp_abi_world_create(void)
{
	for (uint32_t i = 0; i < GP_ABI_MAX_WORLDS; i++)
	{
		abi_world_slot *slot = &g_worlds[i];
		if (slot->used)
			continue;

		gp_world *world = gp_world_create();
		if (!world)
		{
			abi_fail("gp_world_create failed");
			return 0;
		}

		memset(slot, 0, sizeof(*slot));
		slot->world = world;
		slot->used = 1;
		slot->sink.user = slot;
		slot->sink.protectable = abi_protectable;
		slot->sink.grant = abi_grant;
		slot->sink.cover = abi_cover;
		return i + 1;
	}

	abi_fail("no free world slots");
	return 0;
}

/* Releases shields the world owns bookkeeping for, but not the ones a host
   created and still holds. */
static void forget_shield(uint32_t world_slot_index, gp_shield *shield);

static void release_world_shield_pool(abi_world_slot *slot)
{
	if (!slot->shield_pool)
		return;

	uint32_t index = (uint32_t)(slot - g_worlds);

	for (uint32_t i = 0; i < slot->shield_pool_size; i++)
	{
		if (!slot->shield_pool[i])
			continue;

		gp_world_remove_shield(slot->world, slot->shield_pool[i]->pos);
		forget_shield(index, slot->shield_pool[i]);
		gp_shield_destroy(slot->shield_pool[i]);
		slot->shield_pool[i] = NULL;
	}

	free(slot->shield_pool);
	slot->shield_pool = NULL;
	slot->shield_pool_size = 0;
}

/* Drops a handle from the shield table without destroying the shield. */
static void forget_shield(uint32_t world_slot_index, gp_shield *shield)
{
	if (!shield)
		return;

	for (uint32_t i = 0; i < GP_ABI_MAX_SHIELDS; i++)
	{
		if (g_shields[i].used && g_shields[i].shield == shield && g_shields[i].world_slot == world_slot_index)
		{
			memset(&g_shields[i], 0, sizeof(g_shields[i]));
			return;
		}
	}
}

void gp_abi_world_destroy(gp_h_world handle)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return;

	uint32_t index = handle - 1;

	release_world_shield_pool(slot);

	/* Any shield still registered came from the host. Drop the table entries so
	   their handles do not keep pointing at a world that is gone; the host owns
	   the memory and will destroy them itself. */
	for (uint32_t i = 0; i < GP_ABI_MAX_SHIELDS; i++)
	{
		if (g_shields[i].used && g_shields[i].world_slot == index)
			memset(&g_shields[i], 0, sizeof(g_shields[i]));
	}

	if (slot->world)
		gp_world_destroy(slot->world);

	memset(slot, 0, sizeof(*slot));
}

void gp_abi_world_config(gp_h_world handle, uint32_t max_faces_per_block, uint32_t shield_pool,
                         uint32_t shield_regen, uint32_t shield_window, uint32_t shield_range,
                         uint32_t shield_max_targets, uint32_t shield_fuel_max,
                         uint32_t shield_fuel_per_sec, uint32_t decay_interval, uint32_t decay_amount,
                         uint32_t chunk_height, uint32_t copper_breaks, uint32_t iron_breaks)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return;

	/* Start from the library defaults so a host that sets one value does not have
	   to restate the other twelve. */
	gp_config_defaults(&slot->world->cfg);

	if (max_faces_per_block != GP_ABI_KEEP)
		slot->world->cfg.max_faces_per_block = max_faces_per_block;
	if (shield_pool != GP_ABI_KEEP)
		slot->world->cfg.shield_pool = shield_pool;
	if (shield_regen != GP_ABI_KEEP)
		slot->world->cfg.shield_regen = shield_regen;
	if (shield_window != GP_ABI_KEEP)
		slot->world->cfg.shield_window = shield_window;
	if (shield_range != GP_ABI_KEEP)
		slot->world->cfg.shield_range = shield_range;
	if (shield_max_targets != GP_ABI_KEEP)
		slot->world->cfg.shield_max_targets = shield_max_targets;
	if (shield_fuel_max != GP_ABI_KEEP)
		slot->world->cfg.shield_fuel_max = shield_fuel_max;
	if (shield_fuel_per_sec != GP_ABI_KEEP)
		slot->world->cfg.shield_fuel_per_sec = shield_fuel_per_sec;
	if (decay_interval != GP_ABI_KEEP)
		slot->world->cfg.decay_interval = decay_interval;
	if (decay_amount != GP_ABI_KEEP)
		slot->world->cfg.decay_amount = decay_amount;
	if (chunk_height != GP_ABI_KEEP)
		slot->world->cfg.chunk_height = chunk_height;
	if (copper_breaks != GP_ABI_KEEP)
		slot->world->cfg.copper.breaks = (u16)copper_breaks;
	if (iron_breaks != GP_ABI_KEEP)
		slot->world->cfg.iron.breaks = (u16)iron_breaks;
}

#define CFG_OUT(field, dst)                                                                                           \
	do                                                                                                               \
	{                                                                                                                \
		if (dst)                                                                                                     \
			*(dst) = (uint32_t)(slot->world->cfg.field);                                                             \
	} while (0)

void gp_abi_world_config_get(gp_h_world handle, uint32_t *max_faces_per_block, uint32_t *shield_pool,
                             uint32_t *shield_regen, uint32_t *shield_window, uint32_t *shield_range,
                             uint32_t *shield_max_targets, uint32_t *shield_fuel_max,
                             uint32_t *shield_fuel_per_sec, uint32_t *decay_interval,
                             uint32_t *decay_amount, uint32_t *chunk_height, uint32_t *copper_breaks,
                             uint32_t *iron_breaks)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return;

	CFG_OUT(max_faces_per_block, max_faces_per_block);
	CFG_OUT(shield_pool, shield_pool);
	CFG_OUT(shield_regen, shield_regen);
	CFG_OUT(shield_window, shield_window);
	CFG_OUT(shield_range, shield_range);
	CFG_OUT(shield_max_targets, shield_max_targets);
	CFG_OUT(shield_fuel_max, shield_fuel_max);
	CFG_OUT(shield_fuel_per_sec, shield_fuel_per_sec);
	CFG_OUT(decay_interval, decay_interval);
	CFG_OUT(decay_amount, decay_amount);
	CFG_OUT(chunk_height, chunk_height);

	/* The tiers are nested, so the flat macro cannot reach them. */
	if (copper_breaks)
		*copper_breaks = slot->world->cfg.copper.breaks;
	if (iron_breaks)
		*iron_breaks = slot->world->cfg.iron.breaks;
}

/* --- chunks --------------------------------------------------------------- */

int gp_abi_chunk_at(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);
	if (!chunk_loaded_or_create(slot, x, y, z))
		return 0;

	return 1;
}

int gp_abi_chunk_is_loaded(gp_h_world handle, int32_t cx, int32_t cz)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	return gp_world_find_chunk(slot->world, cx, cz) != NULL;
}

int gp_abi_chunk_unload(gp_h_world handle, int32_t cx, int32_t cz)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	return gp_world_unload_chunk(slot->world, cx, cz) ? 1 : 0;
}

void gp_abi_chunk_counts(gp_h_world handle, uint32_t *loaded, uint32_t *reserved)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return;

	if (loaded)
	{
		uint32_t count = 0;
		for (uint32_t i = 0; i < slot->world->chunk_count; i++)
			if (slot->world->chunks[i].loaded)
				count++;
		*loaded = count;
	}

	if (reserved)
		*reserved = slot->world->chunk_count;
}

uint32_t gp_abi_chunk_decay(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint64_t now,
                            uint32_t interval, uint32_t amount, uint32_t *touched, uint32_t *skipped)
{
	if (touched)
		*touched = 0;
	if (skipped)
		*skipped = 0;

	abi_world_slot *slot = world_slot(handle);
	gp_chunk_reinf *chunk = chunk_at_loaded(slot, x, y, z);
	if (!chunk)
		return 0;

	return gp_chunk_shield_decay(chunk, now, interval, amount, touched, skipped);
}

uint32_t gp_abi_chunk_covered(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint64_t now)
{
	abi_world_slot *slot = world_slot(handle);
	gp_chunk_reinf *chunk = chunk_at_loaded(slot, x, y, z);
	if (!chunk)
		return 0;

	return gp_chunk_shield_covered_count(chunk, now);
}

uint64_t gp_abi_world_memory(gp_h_world handle)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	return gp_world_memory(slot->world);
}

/* --- reinforcement -------------------------------------------------------- */

int gp_abi_reinf_add(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t face, uint32_t amount)
{
	abi_world_slot *slot = world_slot(handle);

	if (face >= (uint32_t)GP_FACE_COUNT)
		return 0;

	/* The tier value decides upgrade versus refusal, so truncating it could turn a
	   stronger plate into a weaker one and let a downgrade through. Refuse
	   anything that will not fit rather than lie about it. */
	if (amount == 0 || amount > 0xFFFFu)
		return 0;

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 1) || !ref.chunk)
		return 0;

	return gp_chunk_reinf_add(ref.chunk, ref.lx, ref.ly, ref.lz, (gp_face_t)face, (u16)amount) ? 1 : 0;
}

uint32_t gp_abi_reinf_face_durability(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t face)
{
	abi_world_slot *slot = world_slot(handle);
	if (face >= (uint32_t)GP_FACE_COUNT)
		return 0;

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	gp_block_reinf_t state;
	if (!gp_chunk_reinf_get(ref.chunk, ref.lx, ref.ly, ref.lz, &state))
		return 0;

	for (uint32_t i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		if (state.face[i] == (gp_face_t)face)
			return state.durability[i];

	return 0;
}

int gp_abi_reinf_face_active(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t face)
{
	abi_world_slot *slot = world_slot(handle);
	if (face >= (uint32_t)GP_FACE_COUNT)
		return 0;

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	return gp_chunk_reinf_face_active(ref.chunk, ref.lx, ref.ly, ref.lz, (gp_face_t)face) ? 1 : 0;
}

int gp_abi_reinf_clear(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	bool any = false;
	for (uint32_t face = 0; face < (uint32_t)GP_FACE_COUNT && !any; face++)
		if (face != (uint32_t)GP_FACE_NONE &&
		    gp_chunk_reinf_face_active(ref.chunk, ref.lx, ref.ly, ref.lz, (gp_face_t)face))
			any = true;

	if (!any)
		return 0;

	gp_chunk_reinf_clear(ref.chunk, ref.lx, ref.ly, ref.lz);
	return 1;
}

uint32_t gp_abi_reinf_face_count(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	gp_block_reinf_t state;
	if (!gp_chunk_reinf_get(ref.chunk, ref.lx, ref.ly, ref.lz, &state))
		return 0;

	uint32_t count = 0;
	for (uint32_t i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		if (state.face[i] != GP_FACE_NONE)
			count++;

	return count;
}

uint32_t gp_abi_weakest_face(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return GP_ABI_NO_FACE;

	gp_block_reinf_t state;
	if (!gp_chunk_reinf_get(ref.chunk, ref.lx, ref.ly, ref.lz, &state))
		return GP_ABI_NO_FACE;

	/* Weakest wins, and a tie goes to the lower face id so the same block always
	   resolves to the same face instead of depending on slot order. Without the
	   tie term the first slot found would win, which is an implementation detail
	   the host would then depend on. */
	uint32_t best = GP_ABI_NO_FACE;
	uint32_t best_durability = 0;

	for (uint32_t i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (state.face[i] == GP_FACE_NONE)
			continue;

		uint32_t face = (uint32_t)state.face[i];
		uint32_t durability = state.durability[i];

		if (best == GP_ABI_NO_FACE || durability < best_durability ||
		    (durability == best_durability && face < best))
		{
			best = face;
			best_durability = durability;
		}
	}

	return best;
}

/* --- break resolution ----------------------------------------------------- */

int gp_abi_resolve_break(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t face,
                         uint32_t use_reinf, uint32_t use_shield, uint32_t *reinf_survived,
                         uint32_t *block_broke, uint32_t *absorbed, uint32_t *left)
{
	if (reinf_survived)
		*reinf_survived = 0;
	if (block_broke)
		*block_broke = 1;
	if (absorbed)
		*absorbed = 0;
	if (left)
		*left = 0;

	abi_world_slot *slot = world_slot(handle);
	/* Never create a chunk to resolve a break on: an unprotected block in an
	   unloaded chunk must not allocate anything. */
	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0))
		return 0;

	/*
	 * gp_chunk_resolve_break always consumes both layers. Masking its output
	 * afterwards would be a lie: a host that disabled reinforcement would still
	 * lose durability, and one that disabled shields would still lose points.
	 *
	 * So this reproduces the core's own ordering per enabled layer: a surviving
	 * reinforced face wins and the shield is left untouched, otherwise the shield
	 * absorbs one point. Keeping that precedence here is what stops the shim and
	 * the library from disagreeing about who protected a block.
	 */
	if (!use_reinf && !use_shield)
		return 1; // both layers off: nothing to consume, nothing protects

	if (face >= (uint32_t)GP_FACE_COUNT)
		return 0;

	/* An unloaded chunk cannot hold protection, so the honest answer is "nothing
	   protected this", not a failure. Returning 0 would leave the host unable to
	   tell that apart from "I could not answer", and a host that treats the
	   latter as protected would make every unloaded block unbreakable. */
	if (!ref.chunk)
		return 1;

	if (use_reinf && gp_chunk_reinf_consume(ref.chunk, ref.lx, ref.ly, ref.lz, (gp_face_t)face))
	{
		if (reinf_survived)
			*reinf_survived = 1;
		if (left)
			*left = gp_chunk_shield_get(ref.chunk, ref.lx, ref.ly, ref.lz);
		if (block_broke)
			*block_broke = 0;
		return 1;
	}

	if (reinf_survived)
		*reinf_survived = 0;

	if (use_shield)
	{
		uint32_t taken = gp_chunk_shield_take(ref.chunk, ref.lx, ref.ly, ref.lz, 1);

		if (absorbed)
			*absorbed = taken;
		if (left)
			*left = gp_chunk_shield_get(ref.chunk, ref.lx, ref.ly, ref.lz);
		if (block_broke)
			*block_broke = taken == 0 ? 1u : 0u;
	}
	else if (block_broke)
		*block_broke = 1;

	return 1;
}

/* --- shield points -------------------------------------------------------- */

uint32_t gp_abi_points_get(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	return gp_chunk_shield_get(ref.chunk, ref.lx, ref.ly, ref.lz);
}

uint32_t gp_abi_points_add(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t amount,
                           uint32_t cap)
{
	abi_world_slot *slot = world_slot(handle);

	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 1) || !ref.chunk)
		return 0;

	return gp_chunk_shield_add(ref.chunk, ref.lx, ref.ly, ref.lz, amount, cap);
}

uint32_t gp_abi_points_take(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint32_t amount)
{
	abi_world_slot *slot = world_slot(handle);

	/* Taking points is a read-then-clear, never a create: spending from a block
	   that has no record should return 0 and leave the chunk unloaded. */
	abi_chunk_ref ref;
	if (!chunk_ref(slot, x, y, z, &ref, 0) || !ref.chunk)
		return 0;

	return gp_chunk_shield_take(ref.chunk, ref.lx, ref.ly, ref.lz, amount);
}

/* --- block movement ------------------------------------------------------- */

int gp_abi_move_block(gp_h_world handle, int32_t from_x, int32_t from_y, int32_t from_z, int32_t to_x,
                      int32_t to_y, int32_t to_z, uint32_t motion)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	if (motion >= (uint32_t)GP_FACE_COUNT)
		return 0;

	gp_pos from = {from_x, from_y, from_z};
	gp_pos to = {to_x, to_y, to_z};

	return gp_world_move_block(slot->world, from, to, (gp_face_t)motion) ? 1 : 0;
}

/* --- sink ----------------------------------------------------------------- */

int gp_abi_sink_configure(gp_h_world handle, uint64_t host_id, uint64_t fn_protectable,
                          uint64_t fn_grant, uint64_t fn_cover)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	slot->host_id = host_id;
	slot->protectable = (abi_protectable_fn)(uintptr_t)fn_protectable;
	slot->grant = (abi_grant_fn)(uintptr_t)fn_grant;
	slot->cover = (abi_cover_fn)(uintptr_t)fn_cover;
	return 1;
}

void gp_abi_cover(gp_h_world handle, int32_t x, int32_t y, int32_t z, uint64_t now)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return;

	gp_world_shield_cover(slot->world, (gp_pos){x, y, z}, now);
}

/* --- shield blocks -------------------------------------------------------- */

gp_h_shield gp_abi_shield_create(gp_h_world handle, int32_t x, int32_t y, int32_t z)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	gp_pos pos = {x, y, z};

	/* The world tracks shields by position and refuses duplicates, so an existing
	   one here means the host lost track. Report it rather than silently leaking
	   a second shield over the same block. */
	if (gp_world_find_shield(slot->world, pos))
	{
		abi_fail("a shield is already registered at that position");
		return 0;
	}

	gp_shield *shield = gp_shield_create(&slot->world->cfg, pos, slot->world->cfg.shield_max_targets);
	if (!shield)
	{
		abi_fail("gp_shield_create failed");
		return 0;
	}

	for (uint32_t i = 0; i < GP_ABI_MAX_SHIELDS; i++)
	{
		if (g_shields[i].used)
			continue;

		g_shields[i].shield = shield;
		g_shields[i].world_slot = handle - 1;
		g_shields[i].used = 1;
		gp_world_add_shield(slot->world, shield);
		return i + 1;
	}

	gp_shield_destroy(shield);
	abi_fail("no free shield slots");
	return 0;
}

void gp_abi_shield_destroy(gp_h_shield handle)
{
	abi_shield_slot *entry = shield_slot(handle);
	if (!entry)
		return;

	abi_world_slot *world = world_slot((gp_h_world)(entry->world_slot + 1));
	if (world && entry->shield)
		gp_world_remove_shield(world->world, entry->shield->pos);

	if (entry->shield)
		gp_shield_destroy(entry->shield);

	memset(entry, 0, sizeof(*entry));
}

/* Two variants because a bare `return 0` guard is wrong in a void function. */
#define SHIELD_ENTRY(handle)                                                                                         \
	abi_shield_slot *entry = shield_slot(handle);                                                                     \
	if (!entry)                                                                                                       \
		return 0

#define SHIELD_ENTRY_VOID(handle)                                                                                    \
	abi_shield_slot *entry = shield_slot(handle);                                                                     \
	if (!entry)                                                                                                       \
		return

int gp_abi_shield_add_target(gp_h_shield handle, int32_t x, int32_t y, int32_t z)
{
	SHIELD_ENTRY(handle);
	return gp_shield_add_target(entry->shield, (gp_pos){x, y, z}) ? 1 : 0;
}

int gp_abi_shield_remove_target(gp_h_shield handle, int32_t x, int32_t y, int32_t z)
{
	SHIELD_ENTRY(handle);
	return gp_shield_remove_target(entry->shield, (gp_pos){x, y, z}) ? 1 : 0;
}

void gp_abi_shield_clear_targets(gp_h_shield handle)
{
	SHIELD_ENTRY_VOID(handle);
	gp_shield_clear_targets(entry->shield);
}

void gp_abi_shield_set_powered(gp_h_shield handle, int powered)
{
	SHIELD_ENTRY_VOID(handle);
	gp_shield_set_powered(entry->shield, powered != 0);
}

void gp_abi_shield_set_fuel(gp_h_shield handle, uint32_t fuel)
{
	SHIELD_ENTRY_VOID(handle);
	gp_shield_set_fuel(entry->shield, fuel);
}

void gp_abi_shield_get_state(gp_h_shield handle, uint32_t *powered, uint32_t *running, uint32_t *fuel)
{
	if (powered)
		*powered = 0;
	if (running)
		*running = 0;
	if (fuel)
		*fuel = 0;

	SHIELD_ENTRY_VOID(handle);

	if (powered)
		*powered = entry->shield->powered ? 1u : 0u;
	if (running)
		*running = entry->shield->running ? 1u : 0u;
	if (fuel)
		*fuel = entry->shield->fuel;
}

void gp_abi_shield_get_pos(gp_h_shield handle, int32_t *x, int32_t *y, int32_t *z)
{
	if (x)
		*x = 0;
	if (y)
		*y = 0;
	if (z)
		*z = 0;

	SHIELD_ENTRY_VOID(handle);

	if (x)
		*x = entry->shield->pos.x;
	if (y)
		*y = entry->shield->pos.y;
	if (z)
		*z = entry->shield->pos.z;
}

uint32_t gp_abi_shield_tick(gp_h_shield handle, uint64_t now, uint32_t *points_granted)
{
	if (points_granted)
		*points_granted = 0;

	SHIELD_ENTRY(handle);

	abi_world_slot *world = world_slot((gp_h_world)(entry->world_slot + 1));
	if (!world)
		return 0;

	/*
	 * The sink is always wired to the shim's adapters, so a host that never
	 * registered callbacks leaves them NULL and the library grants nothing. That
	 * is the safe direction: no points rather than points nobody paid for.
	 *
	 * Returns the points granted, which is also written through `points_granted`.
	 */
	return gp_shield_tick(entry->shield, now, &world->sink, points_granted);
}

gp_h_shield gp_abi_shield_at(gp_h_world handle, uint32_t index)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	uint32_t world_index = handle - 1;
	uint32_t seen = 0;

	for (uint32_t i = 0; i < GP_ABI_MAX_SHIELDS; i++)
	{
		if (!g_shields[i].used || g_shields[i].world_slot != world_index)
			continue;

		if (seen == index)
			return i + 1;

		seen++;
	}

	return 0;
}

uint32_t gp_abi_shield_count(gp_h_world handle)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot)
		return 0;

	uint32_t world_index = handle - 1;
	uint32_t count = 0;

	for (uint32_t i = 0; i < GP_ABI_MAX_SHIELDS; i++)
		if (g_shields[i].used && g_shields[i].world_slot == world_index)
			count++;

	return count;
}

/* --- persistence ---------------------------------------------------------- */

int gp_abi_world_save(gp_h_world handle, const char *path)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot || !path)
	{
		abi_fail("bad world handle or path");
		return 0;
	}

	stream_t stream;
	if (stream_open_write(path, 1, &stream) != 0)
	{
		abi_fail("could not open the save file for writing");
		return 0;
	}

	gp_world_write_stream(slot->world, &stream);
	stream_close(&stream);
	return 1;
}

int gp_abi_world_load(gp_h_world handle, const char *path, uint32_t shield_pool_size,
                      uint32_t shield_target_capacity)
{
	abi_world_slot *slot = world_slot(handle);
	if (!slot || !path)
	{
		abi_fail("bad world handle or path");
		return -1;
	}

	/* Shields from a previous load are stale the moment the new data lands. */
	release_world_shield_pool(slot);

	stream_t stream;
	if (stream_open_read(path, 1, &stream) != 0)
	{
		abi_fail("could not open the save file for reading");
		return -1;
	}

	arena *scratch = arena_create(64 * 1024);
	arena *objects = arena_create(64 * 1024);
	if (!scratch || !objects)
	{
		if (scratch)
			arena_destroy(scratch);
		if (objects)
			arena_destroy(objects);
		stream_close(&stream);
		abi_fail("could not allocate loader arenas");
		return -1;
	}

	gp_shield **pool = NULL;
	if (shield_pool_size)
	{
		pool = (gp_shield **)calloc(shield_pool_size, sizeof(gp_shield *));
		if (!pool)
		{
			arena_destroy(scratch);
			arena_destroy(objects);
			stream_close(&stream);
			abi_fail("could not allocate the shield pool");
			return -1;
		}
	}

	tkv_object header = tkv_read_from_stream(&stream, scratch, objects);
	if (!header)
	{
		free(pool);
		arena_destroy(scratch);
		arena_destroy(objects);
		stream_close(&stream);
		abi_fail("save file is not a readable griefprot header");
		return -1;
	}

	i32 restored = gp_world_unpack(header, &stream, slot->world, pool, shield_pool_size,
	                               shield_target_capacity, scratch);

	arena_destroy(scratch);
	arena_destroy(objects);
	stream_close(&stream);

	if (restored < 0)
	{
		free(pool);
		abi_fail("save file contents were rejected");
		return -1;
	}

	/* Keep the pool array so a later load, or a world teardown, can release these
	   shields instead of leaking them. The shields are already registered with the
	   world by gp_world_unpack. */
	slot->shield_pool = pool;
	slot->shield_pool_size = shield_pool_size;
	slot->shield_target_capacity = shield_target_capacity;

	/* Give the restored shields handles so the host can tick them. */
	for (uint32_t i = 0; i < slot->world->shield_count; i++)
	{
		gp_shield *shield = slot->world->shields[i];
		if (!shield)
			continue;

		for (uint32_t s = 0; s < GP_ABI_MAX_SHIELDS; s++)
		{
			if (g_shields[s].used)
				continue;

			g_shields[s].shield = shield;
			g_shields[s].world_slot = handle - 1;
			g_shields[s].used = 1;
			break;
		}
	}

	return (int)restored;
}
