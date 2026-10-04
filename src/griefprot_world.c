#include "griefprot_world.h"
#include <stdlib.h>
#include <string.h>

static i64 pack_chunk_key(i32 cx, i32 cz)
{
	return ((i64)(u32)cx << 32) | (u32)cz;
}

/* --- chunk directory -------------------------------------------------------- */

static u32 dir_probe(const gp_chunk_dir_entry *dir, u32 capacity, i64 key, bool *found)
{
	u32 mask = capacity - 1;
	u32 i = (u32)((u64)key * 0x9E3779B97F4A7C15ull >> 32) & mask;

	for (u32 step = 0; step < capacity; step++)
	{
		i64 k = dir[i].keys;
		if (k == GP_CHUNK_DIR_EMPTY)
		{
			*found = false;
			return i;
		}
		if (k == key)
		{
			*found = true;
			return i;
		}
		i = (i + 1) & mask;
	}

	*found = false;
	return capacity;
}

static bool dir_grow(gp_world *world, u32 new_capacity)
{
	gp_chunk_dir_entry *dir = (gp_chunk_dir_entry *)calloc(new_capacity, sizeof(gp_chunk_dir_entry));
	if (!dir)
		return false;

	for (u32 i = 0; i < new_capacity; i++)
		dir[i].keys = GP_CHUNK_DIR_EMPTY;

	for (u32 i = 0; i < world->dir_capacity; i++)
	{
		i64 key = world->dir[i].keys;
		if (key == GP_CHUNK_DIR_EMPTY)
			continue;

		bool found = false;
		u32 slot = dir_probe(dir, new_capacity, key, &found);
		dir[slot] = world->dir[i];
	}

	free(world->dir);
	world->dir = dir;
	world->dir_capacity = new_capacity;
	return true;
}

static gp_chunk_dir_entry *dir_find(const gp_world *world, i32 cx, i32 cz)
{
	if (world->dir_capacity == 0)
		return NULL;

	i64 key = pack_chunk_key(cx, cz);
	bool found = false;
	u32 slot = dir_probe(world->dir, world->dir_capacity, key, &found);

	return found ? &world->dir[slot] : NULL;
}

static bool dir_insert(gp_world *world, i32 cx, i32 cz, u32 chunk_index)
{
	u32 needed = world->dir_capacity;
	u32 used = 0;
	for (u32 i = 0; i < world->dir_capacity; i++)
		if (world->dir[i].keys != GP_CHUNK_DIR_EMPTY)
			used++;

	if (world->dir_capacity == 0 || (used + 1) * 10 >= world->dir_capacity * 7)
	{
		needed = world->dir_capacity ? world->dir_capacity : 64;
		while ((used + 1) * 10 >= needed * 7)
			needed *= 2;
		if (!dir_grow(world, needed))
			return false;
	}

	i64 key = pack_chunk_key(cx, cz);
	bool found = false;
	u32 slot = dir_probe(world->dir, world->dir_capacity, key, &found);

	world->dir[slot].keys = key;
	world->dir[slot].slot = chunk_index;
	return true;
}

/* --- lifecycle -------------------------------------------------------------- */

gp_world *gp_world_create(void)
{
	gp_world *world = (gp_world *)calloc(1, sizeof(gp_world));
	if (!world)
		return NULL;

	gp_config_defaults(&world->cfg);
	return world;
}

void gp_world_destroy(gp_world *world)
{
	if (!world)
		return;

	for (u32 i = 0; i < world->chunk_count; i++)
		gp_chunk_reinf_free(&world->chunks[i].data);

	free(world->chunks);
	free(world->dir);

	// shields are owned by the host, only the pointers are tracked here
	free(world->shields);
	free(world);
}

void gp_world_set_config(gp_world *world, const gp_config *cfg)
{
	if (world && cfg)
		world->cfg = *cfg;
}

const gp_config *gp_world_get_config(const gp_world *world)
{
	return world ? &world->cfg : NULL;
}

/* --- chunks ----------------------------------------------------------------- */

static gp_chunk_slot *chunk_slot(gp_world *world, i32 cx, i32 cz)
{
	if (!world)
		return NULL;

	gp_chunk_dir_entry *entry = dir_find(world, cx, cz);
	if (!entry || entry->slot >= world->chunk_count)
		return NULL;

	gp_chunk_slot *slot = &world->chunks[entry->slot];
	if (!slot->loaded)
		return NULL;

	return slot;
}

gp_chunk_reinf *gp_world_find_chunk(const gp_world *world, i32 cx, i32 cz)
{
	gp_chunk_slot *slot = chunk_slot((gp_world *)world, cx, cz);
	return slot ? &slot->data : NULL;
}

gp_chunk_reinf *gp_world_chunk(gp_world *world, i32 cx, i32 cz)
{
	gp_chunk_slot *slot = chunk_slot(world, cx, cz);
	if (slot)
		return &slot->data;

	if (!world)
		return NULL;

	gp_chunk_dir_entry *entry = dir_find(world, cx, cz);

	// reuse the released slot for these coordinates if there is one
	if (entry && entry->slot < world->chunk_count)
	{
		slot = &world->chunks[entry->slot];
	}
	else
	{
		if (world->chunk_count == world->chunk_capacity)
		{
			u32 cap = world->chunk_capacity ? world->chunk_capacity * 2 : 64;
			gp_chunk_slot *chunks = (gp_chunk_slot *)realloc(world->chunks, cap * sizeof(gp_chunk_slot));
			if (!chunks)
				return NULL;
			world->chunks = chunks;
			world->chunk_capacity = cap;
		}

		slot = &world->chunks[world->chunk_count];
		slot->cx = cx;
		slot->cz = cz;
		slot->loaded = false;

		if (!dir_insert(world, cx, cz, world->chunk_count))
			return NULL;

		world->chunk_count++;
	}

	gp_chunk_reinf_init(&slot->data, world->cfg.chunk_height);
	slot->loaded = true;
	return &slot->data;
}

bool gp_world_unload_chunk(gp_world *world, i32 cx, i32 cz)
{
	gp_chunk_slot *slot = chunk_slot(world, cx, cz);
	if (!slot)
		return false;

	gp_chunk_reinf_free(&slot->data);
	memset(&slot->data, 0, sizeof(slot->data));
	slot->loaded = false;
	return true;
}

bool gp_world_pos_to_local(gp_pos pos, i32 chunk_height, i32 *cx, i32 *cz, u32 *lx, u32 *ly, u32 *lz)
{
	if (!cx || !cz || !lx || !ly || !lz)
		return false;

	i32 bx = pos.x >> 4;
	i32 bz = pos.z >> 4;
	i32 ox = pos.x - (bx << 4);
	i32 oz = pos.z - (bz << 4);

	if (ox < 0 || ox > 15 || oz < 0 || oz > 15)
		return false;
	if (pos.y < 0 || (u32)pos.y >= (u32)chunk_height)
		return false;

	*cx = bx;
	*cz = bz;
	*lx = (u32)ox;
	*ly = (u32)pos.y;
	*lz = (u32)oz;
	return true;
}

gp_chunk_reinf *gp_world_chunk_at(gp_world *world, gp_pos pos)
{
	if (!world)
		return NULL;

	i32 cx, cz;
	u32 lx, ly, lz;
	if (!gp_world_pos_to_local(pos, (i32)world->cfg.chunk_height, &cx, &cz, &lx, &ly, &lz))
		return NULL;

	return gp_world_chunk(world, cx, cz);
}

/* --- shields ---------------------------------------------------------------- */

gp_shield *gp_world_find_shield(const gp_world *world, gp_pos pos)
{
	if (!world)
		return NULL;

	for (u32 i = 0; i < world->shield_count; i++)
		if (world->shields[i] && gp_pos_eq(world->shields[i]->pos, pos))
			return world->shields[i];

	return NULL;
}

gp_shield *gp_world_add_shield(gp_world *world, gp_shield *shield)
{
	if (!world || !shield)
		return NULL;

	if (gp_world_find_shield(world, shield->pos))
		return NULL; // already registered

	if (world->shield_count == world->shield_capacity)
	{
		u32 cap = world->shield_capacity ? world->shield_capacity * 2 : 16;
		gp_shield **shields = (gp_shield **)realloc(world->shields, cap * sizeof(gp_shield *));
		if (!shields)
			return NULL;
		world->shields = shields;
		world->shield_capacity = cap;
	}

	world->shields[world->shield_count++] = shield;
	return shield;
}

bool gp_world_remove_shield(gp_world *world, gp_pos pos)
{
	if (!world)
		return false;

	for (u32 i = 0; i < world->shield_count; i++)
	{
		if (!world->shields[i] || !gp_pos_eq(world->shields[i]->pos, pos))
			continue;

		for (u32 j = i; j + 1 < world->shield_count; j++)
			world->shields[j] = world->shields[j + 1];
		world->shield_count--;
		return true;
	}

	return false;
}

u64 gp_world_memory(const gp_world *world)
{
	if (!world)
		return 0;

	u64 total = 0;
	for (u32 i = 0; i < world->chunk_count; i++)
		total += gp_chunk_reinf_memory(&world->chunks[i].data);
	return total;
}

/* --- block movement --------------------------------------------------------- */

static gp_face_t face_opposite(gp_face_t face)
{
	switch (face)
	{
	case GP_FACE_DOWN:
		return GP_FACE_UP;
	case GP_FACE_UP:
		return GP_FACE_DOWN;
	case GP_FACE_NORTH:
		return GP_FACE_SOUTH;
	case GP_FACE_SOUTH:
		return GP_FACE_NORTH;
	case GP_FACE_WEST:
		return GP_FACE_EAST;
	case GP_FACE_EAST:
		return GP_FACE_WEST;
	default:
		return GP_FACE_NONE;
	}
}

void gp_world_shield_cover(void *user, gp_pos pos, u64 now)
{
	gp_world *world = (gp_world *)user;
	if (!world)
		return;

	i32 cx, cz;
	u32 lx, ly, lz;
	if (!gp_world_pos_to_local(pos, (i32)world->cfg.chunk_height, &cx, &cz, &lx, &ly, &lz))
		return;

	// find, never create. A range shield covers a whole cube, and claiming
	// coverage for blocks in chunks that are not loaded would pull every one of
	// them into memory just to stamp them.
	gp_chunk_reinf *chunk = gp_world_find_chunk(world, cx, cz);
	if (!chunk)
		return;

	// Reach a full interval past the sweep, so a sweep landing between two
	// windows still sees the block as covered. u64 because next_decay and the
	// host clock are both u64 and the sum must not wrap.
	u64 interval = (u64)(world->cfg.decay_interval ? world->cfg.decay_interval : 1);
	u64 grace = now + interval + 1;
	if (grace < now)
		grace = UINT64_MAX;

	gp_chunk_shield_cover(chunk, lx, ly, lz, grace);
}

/* Splits a world position into a chunk plus local coordinates. Unlike
   gp_world_chunk this never creates a chunk. */
static bool locate(const gp_world *world, gp_pos pos, i32 *cx, i32 *cz, u32 *lx, u32 *ly, u32 *lz)
{
	return gp_world_pos_to_local(pos, (i32)world->cfg.chunk_height, cx, cz, lx, ly, lz);
}

bool gp_world_move_block(gp_world *world, gp_pos from, gp_pos to, gp_face_t motion)
{
	if (!world || motion <= GP_FACE_NONE || motion >= GP_FACE_COUNT)
		return false;

	if (gp_pos_eq(from, to))
		return true;

	i32 fcx, fcz, tcx, tcz;
	u32 fx, fy, fz, tx, ty, tz;

	// a nonsense position is refused, unlike an untracked chunk
	if (!locate(world, from, &fcx, &fcz, &fx, &fy, &fz))
		return false;
	if (!locate(world, to, &tcx, &tcz, &tx, &ty, &tz))
		return false;

	gp_chunk_reinf *src = gp_world_find_chunk(world, fcx, fcz);
	if (!src)
		return true; // the source chunk holds no protection to move

	gp_block_reinf_t moved;
	bool has_reinf = gp_chunk_reinf_get(src, fx, fy, fz, &moved);
	u32 points = gp_chunk_shield_get(src, fx, fy, fz);

	if (!has_reinf && points == 0)
		return true; // nothing to carry, so nothing to decide

	if (has_reinf)
	{
		// the side the block came off must not be protected, otherwise whatever
		// relies on that face is left with a hole where the block used to be
		gp_face_t source_face = face_opposite(motion);
		for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
			if (moved.face[i] == source_face)
				return false;
	}

	// the destination chunk is created on demand: landing somewhere that has
	// never been loaded is legitimate
	gp_chunk_reinf *dst = gp_world_chunk(world, tcx, tcz);
	if (!dst)
		return false;

	if (has_reinf)
	{
		gp_block_reinf_t dest;
		if (!gp_chunk_reinf_get(dst, tx, ty, tz, &dest))
			memset(&dest, 0, sizeof(dest)); // GP_FACE_NONE is 0, so this reads as empty

		// count the distinct faces the destination would end up carrying
		u8 seen[GP_FACE_COUNT] = {0};
		u32 distinct = 0;

		for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		{
			if (dest.face[i] == GP_FACE_NONE || seen[dest.face[i]])
				continue;
			seen[dest.face[i]] = 1;
			distinct++;
		}
		for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		{
			if (moved.face[i] == GP_FACE_NONE || seen[moved.face[i]])
				continue;
			seen[moved.face[i]] = 1;
			distinct++;
		}

		u32 cap = world->cfg.max_faces_per_block;
		if (cap > GP_MAX_REINFORCED_FACES)
			cap = GP_MAX_REINFORCED_FACES;

		if (distinct > cap)
			return false; // refuse rather than drop a face

		if (!gp_chunk_reinf_transfer(src, fx, fy, fz, dst, tx, ty, tz))
			return false;
	}

	// shield points follow the block. The two are independent stores, and the
	// host's sink trims each block back to its share on the next window.
	if (points)
		gp_chunk_shield_transfer(src, fx, fy, fz, dst, tx, ty, tz);

	return true;
}