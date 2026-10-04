#include "griefprot_chunk.h"
#include <string.h>

/* The per block shield record is {u32 points; u32 cover_expiry}. The expiry is a
   saturating host timestamp, so it only needs 32 bits to outlast any real uptime
   while keeping the record at two words. */
typedef struct
{
	u32 points;
	u32 cover_expiry;
} gp_shield_block;

#define GP_SHIELD_BLOCK_VALUE_SIZE (sizeof(gp_shield_block))
#define COVER_SATURATE(now) ((now) > (u64)UINT32_MAX ? UINT32_MAX : (u32)(now))
#define COVER_FOREVER UINT32_MAX

static void shield_record_read(const u8 *record, gp_shield_block *out)
{
	memcpy(&out->points, record, sizeof(u32));
	memcpy(&out->cover_expiry, record + sizeof(u32), sizeof(u32));
}

static void shield_record_write(u8 *record, const gp_shield_block *in)
{
	memcpy(record, &in->points, sizeof(u32));
	memcpy(record + sizeof(u32), &in->cover_expiry, sizeof(u32));
}

static bool block_covered(const gp_shield_block *block, u64 now)
{
	return block->cover_expiry == COVER_FOREVER || (u64)block->cover_expiry > now;
}

/*
	Packed on-disk record, 10 bytes total:

	  [0..7]  u16 durability for slot 0..3
	  [8..9]  slot 0..3 face ids, 3 bits each

	Slot i holding face GP_FACE_NONE is unused. Reading and writing go through
	gp_block_reinf_t so the packing stays in one place.
*/

static void pack(const gp_block_reinf_t *in, u8 *out)
{
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		out[i * 2 + 0] = (u8)(in->durability[i] & 0xFF);
		out[i * 2 + 1] = (u8)(in->durability[i] >> 8);
	}

	u16 faces = 0;
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		faces |= (u16)(in->face[i] & 0x7) << (i * 3);

	out[8] = (u8)(faces & 0xFF);
	out[9] = (u8)(faces >> 8);
}

static void unpack(const u8 *in, gp_block_reinf_t *out)
{
	u16 faces = (u16)(in[8] | ((u16)in[9] << 8));

	out->count = 0;
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		out->durability[i] = (u16)(in[i * 2 + 0] | ((u16)in[i * 2 + 1] << 8));
		out->face[i] = (u8)((faces >> (i * 3)) & 0x7);
		if (out->face[i] != GP_FACE_NONE)
			out->count++;
	}
}

static bool index_valid(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z)
{
	return x < 16 && z < 16 && y < chunk->chunk_height;
}

static void index_to_xyz(u32 index, u32 *x, u32 *y, u32 *z)
{
	*x = index % 16;
	u32 rest = index / 16;
	*z = rest % 16;
	*y = rest / 16;
}

void gp_chunk_reinf_init(gp_chunk_reinf *chunk, u32 chunk_height)
{
	gp_sparse_init(&chunk->reinf, GP_SPARSE_REINF_VALUE_SIZE);
	gp_sparse_init(&chunk->shield, GP_SHIELD_BLOCK_VALUE_SIZE);
	chunk->chunk_height = chunk_height ? chunk_height : 384;
	chunk->reinf_entries = 0;
	chunk->shield_entries = 0;
	chunk->next_decay = 0;
}

void gp_chunk_reinf_free(gp_chunk_reinf *chunk)
{
	gp_sparse_free(&chunk->reinf);
	gp_sparse_free(&chunk->shield);
	chunk->reinf_entries = 0;
	chunk->shield_entries = 0;
}

static u8 *record_get(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z)
{
	if (!index_valid(chunk, x, y, z))
		return NULL;
	return gp_sparse_get((gp_sparse *)&chunk->reinf, gp_block_index(x, y, z, chunk->chunk_height), false);
}

static u8 *record_get_or_create(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z)
{
	if (!index_valid(chunk, x, y, z))
		return NULL;
	return gp_sparse_get(&chunk->reinf, gp_block_index(x, y, z, chunk->chunk_height), true);
}

bool gp_chunk_reinf_add(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, gp_face_t face, u16 amount)
{
	if (face >= GP_FACE_COUNT || amount == 0)
		return false;

	// Look for the face before allocating anything, so a refused reapplication
	// cannot leave a record behind on a block that had none.
	gp_block_reinf_t state;
	bool have = gp_chunk_reinf_get(chunk, x, y, z, &state);

	if (have)
	{
		for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		{
			if (state.face[i] != face)
				continue;

			/*
			 * The face already carries a plate. Upgrade it in place, never stack
			 * and never downgrade: summing would let a player double a face's
			 * durability with two of the same plate, and overwriting outright
			 * would let a copper plate undo an iron one.
			 *
			 * Returning false means the caller keeps its item, so refusing an
			 * equal or weaker plate costs the player nothing.
			 */
			if (amount <= state.durability[i])
				return false;

			u8 *upgraded = record_get_or_create(chunk, x, y, z);
			if (!upgraded)
				return false;

			state.durability[i] = amount;
			pack(&state, upgraded);
			return true;
		}
	}
	else
	{
		// A block with no record has no reinforced faces, so the slot search below
		// starts from a zeroed state.
		memset(&state, 0, sizeof(state));
	}

	u8 *record = record_get_or_create(chunk, x, y, z);
	if (!record)
		return false;

	// face is not reinforced yet, so it needs a free slot
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (state.face[i] != GP_FACE_NONE)
			continue;

		bool was_empty = state.count == 0;

		state.face[i] = (u8)face;
		state.durability[i] = amount;
		state.count++;
		pack(&state, record);

		if (was_empty)
			chunk->reinf_entries++;

		return true;
	}

	return false; // all four slots taken
}

bool gp_chunk_reinf_face_active(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, gp_face_t face)
{
	u8 *record = record_get(chunk, x, y, z);
	if (!record)
		return false;

	gp_block_reinf_t state;
	unpack(record, &state);

	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
		if (state.face[i] == face)
			return state.durability[i] > 0;

	return false;
}

bool gp_chunk_reinf_get(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, gp_block_reinf_t *out)
{
	u8 *record = record_get(chunk, x, y, z);
	if (!record)
		return false;

	unpack(record, out);
	return out->count > 0;
}

bool gp_chunk_reinf_consume(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, gp_face_t face)
{
	u8 *record = record_get(chunk, x, y, z);
	if (!record)
		return false;

	gp_block_reinf_t state;
	unpack(record, &state);

	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (state.face[i] != face || state.durability[i] == 0)
			continue;

		state.durability[i]--;
		bool survived = state.durability[i] > 0;

		if (!survived)
		{
			state.face[i] = GP_FACE_NONE;
			state.durability[i] = 0;
			state.count--;
			chunk->reinf_entries--;
		}

		if (state.count == 0)
		{
			// block is fully unprotected now, drop the record so the sparse
			// table does not keep paying for it
			gp_sparse_remove(&chunk->reinf, gp_block_index(x, y, z, chunk->chunk_height));
		}
		else
		{
			pack(&state, record);
		}

		return survived;
	}

	return false;
}

void gp_chunk_reinf_clear(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z)
{
	if (!index_valid(chunk, x, y, z))
		return;

	if (record_get(chunk, x, y, z))
		chunk->reinf_entries--;

	gp_sparse_remove(&chunk->reinf, gp_block_index(x, y, z, chunk->chunk_height));
}

/* Unions `moved` into `dest`, summing durability when a face is already present.
   Refuses rather than dropping anything when the union would need more than
   GP_MAX_REINFORCED_FACES distinct faces, so a refused merge leaves both records
   untouched. */
static bool merge_reinf(const gp_block_reinf_t *dest, const gp_block_reinf_t *moved,
                        gp_block_reinf_t *out)
{
	u8 face[GP_MAX_REINFORCED_FACES] = {GP_FACE_NONE, GP_FACE_NONE, GP_FACE_NONE, GP_FACE_NONE};
	u16 dur[GP_MAX_REINFORCED_FACES] = {0, 0, 0, 0};
	u32 occupied = 0;

	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (dest->face[i] == GP_FACE_NONE)
			continue;
		face[occupied] = dest->face[i];
		dur[occupied] = dest->durability[i];
		occupied++;
	}

	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		if (moved->face[i] == GP_FACE_NONE)
			continue;

		u32 j = 0;
		for (; j < occupied; j++)
			if (face[j] == moved->face[i])
				break;

		if (j < occupied)
		{
			u32 sum = (u32)dur[j] + moved->durability[i];
			dur[j] = sum > 0xFFFF ? 0xFFFF : (u16)sum;
			continue;
		}

		if (occupied >= GP_MAX_REINFORCED_FACES)
			return false; // would silently drop a distinct face

		face[occupied] = moved->face[i];
		dur[occupied] = moved->durability[i];
		occupied++;
	}

	out->count = (u8)occupied;
	for (u32 i = 0; i < GP_MAX_REINFORCED_FACES; i++)
	{
		out->face[i] = face[i];
		out->durability[i] = dur[i];
	}

	return true;
}

/* Moves one block's protection within a chunk.

   Faces keep their world space direction, since a piston translates a block
   without rotating it. Durability on a face already present at the destination
   is summed, and a merge that would need a fifth distinct face is refused. */
bool gp_chunk_reinf_move(gp_chunk_reinf *chunk, u32 from_x, u32 from_y, u32 from_z, u32 to_x, u32 to_y,
                         u32 to_z)
{
	return gp_chunk_reinf_transfer(chunk, from_x, from_y, from_z, chunk, to_x, to_y, to_z);
}

/* Moves protection between two chunks, which may be the same one. Each
   coordinate is validated against the chunk it belongs to, so a cross chunk
   caller cannot accidentally address a local cell in the source chunk. */
bool gp_chunk_reinf_transfer(gp_chunk_reinf *src, u32 from_x, u32 from_y, u32 from_z, gp_chunk_reinf *dst,
                             u32 to_x, u32 to_y, u32 to_z)
{
	if (!src || !dst)
		return false;
	if (!index_valid(src, from_x, from_y, from_z) || !index_valid(dst, to_x, to_y, to_z))
		return false;

	// a move onto itself must not clear the record it just read
	if (src == dst && from_x == to_x && from_y == to_y && from_z == to_z)
		return true;

	gp_block_reinf_t moved;
	if (!gp_chunk_reinf_get(src, from_x, from_y, from_z, &moved))
		return false;

	gp_block_reinf_t dest;
	bool had_dest = gp_chunk_reinf_get(dst, to_x, to_y, to_z, &dest);
	if (!had_dest)
		memset(&dest, 0, sizeof(dest)); // GP_FACE_NONE is 0, so this reads as empty

	gp_block_reinf_t merged;
	if (!merge_reinf(&dest, &moved, &merged))
		return false;

	/*
	 * Reserve the destination cell before disturbing anything. Clearing the
	 * source first and allocating afterwards means a failed allocation destroys
	 * both records: the source is gone, and whatever the destination already
	 * held went with it. Here a failure leaves both records exactly as they were
	 * and the caller can retry.
	 *
	 * get_or_create does not touch reinf_entries, so calling it early is free of
	 * counter side effects. It may rehash the table, so the pointer is taken
	 * after every read above and used only once nothing can move it again.
	 */
	u8 *record = record_get_or_create(dst, to_x, to_y, to_z);
	if (!record)
		return false;

	// The destination is now guaranteed, so removing the source cannot lose data.
	gp_sparse_remove(&src->reinf, gp_block_index(from_x, from_y, from_z, src->chunk_height));
	if (src->reinf_entries > 0)
		src->reinf_entries--;

	if (merged.count == 0)
	{
		// Nothing left to write. If the destination cell did not exist before,
		// drop it again rather than leaving a zeroed record behind: the sparse
		// map would otherwise grow for a block with no protection on it.
		if (!had_dest)
		{
			gp_sparse_remove(&dst->reinf, gp_block_index(to_x, to_y, to_z, dst->chunk_height));
		}
		return true;
	}

	// An existing destination cell is rewritten in place and keeps its entry in
	// the count; only a newly created one needs counting.
	pack(&merged, record);
	if (!had_dest)
		dst->reinf_entries++;
	return true;
}

/* Moves shield points between two chunks, summing with the destination's. The
   host's sink re-applies each shield's ceiling on the next regeneration window,
   so this only saturates rather than inventing a cap. */
bool gp_chunk_shield_transfer(gp_chunk_reinf *src, u32 from_x, u32 from_y, u32 from_z,
                              gp_chunk_reinf *dst, u32 to_x, u32 to_y, u32 to_z)
{
	if (!src || !dst)
		return false;
	if (!index_valid(src, from_x, from_y, from_z) || !index_valid(dst, to_x, to_y, to_z))
		return false;

	if (src == dst && from_x == to_x && from_y == to_y && from_z == to_z)
		return true;

	u32 points = gp_chunk_shield_get(src, from_x, from_y, from_z);
	if (points == 0)
		return true; // nothing to carry

	/*
	 * Write the destination before clearing the source, and only clear it once
	 * the write is known to have landed. Doing it the other way round loses the
	 * points outright if the destination cannot allocate: add reports 0 added,
	 * yet the source has already been emptied and the caller is told the move
	 * succeeded.
	 */
	u32 stored = gp_chunk_shield_add(dst, to_x, to_y, to_z, points, 0xFFFFFFFFu);
	if (stored == 0)
		return false;

	gp_chunk_shield_take(src, from_x, from_y, from_z, points);
	return true;
}

void gp_chunk_reinf_visit(const gp_chunk_reinf *chunk, gp_reinf_visit_fn fn, void *user_data)
{
	if (!fn)
		return;

	for (u32 i = 0; i < chunk->reinf.capacity; i++)
	{
		if (chunk->reinf.keys[i] == GP_SPARSE_KEY_EMPTY || chunk->reinf.keys[i] == GP_SPARSE_KEY_DEAD)
			continue;

		gp_block_reinf_t state;
		unpack(chunk->reinf.values + (size_t)i * GP_SPARSE_REINF_VALUE_SIZE, &state);
		if (state.count == 0)
			continue;

		u32 x, y, z;
		index_to_xyz(chunk->reinf.keys[i], &x, &y, &z);
		fn(user_data, x, y, z, &state);
	}
}

u64 gp_chunk_reinf_memory(const gp_chunk_reinf *chunk)
{
	return gp_sparse_memory(&chunk->reinf) + gp_sparse_memory(&chunk->shield);
}

u32 gp_chunk_shield_get(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z)
{
	if (!index_valid(chunk, x, y, z))
		return 0;

	u8 *record = gp_sparse_get((gp_sparse *)&chunk->shield, gp_block_index(x, y, z, chunk->chunk_height), false);
	if (!record)
		return 0;

	gp_shield_block block;
	shield_record_read(record, &block);
	return block.points;
}

u32 gp_chunk_shield_add(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, u32 amount, u32 cap)
{
	if (!index_valid(chunk, x, y, z) || amount == 0)
		return 0;

	u8 *record = gp_sparse_get(&chunk->shield, gp_block_index(x, y, z, chunk->chunk_height), true);
	if (!record)
		return 0;

	gp_shield_block block;
	shield_record_read(record, &block);

	u32 added = amount;
	if (cap && block.points + amount > cap)
		added = cap > block.points ? cap - block.points : 0;

	if (added == 0)
		return 0;

	// a record only ever exists while it holds points, so a fresh key is a new block
	if (block.points == 0)
		chunk->shield_entries++;

	block.points += added;
	shield_record_write(record, &block);
	return added;
}

u32 gp_chunk_shield_take(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, u32 amount)
{
	if (!index_valid(chunk, x, y, z) || amount == 0)
		return 0;

	u32 index = gp_block_index(x, y, z, chunk->chunk_height);
	u8 *record = gp_sparse_get(&chunk->shield, index, false);
	if (!record)
		return 0;

	gp_shield_block block;
	shield_record_read(record, &block);

	u32 taken = block.points < amount ? block.points : amount;
	block.points -= taken;

	if (block.points == 0)
	{
		// the coverage claim goes with the points, so a block that has just
		// emptied out stops being treated as covered
		gp_sparse_remove(&chunk->shield, index);
		if (chunk->shield_entries > 0)
			chunk->shield_entries--;
	}
	else
	{
		shield_record_write(record, &block);
	}

	return taken;
}

void gp_chunk_shield_cover(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, u64 expiry)
{
	if (!index_valid(chunk, x, y, z))
		return;

	// Coverage only ever shields points that already exist. Allocating a record
	// here instead would make every block a shield passes over occupy a slot in
	// the map, which for a range shield is its entire cube.
	u8 *record = gp_sparse_get(&chunk->shield, gp_block_index(x, y, z, chunk->chunk_height), false);
	if (!record)
		return;

	gp_shield_block block;
	shield_record_read(record, &block);

	// keep the furthest claim, so a shield that runs late never shortens another
	// shield's coverage
	if (expiry == UINT64_MAX)
		block.cover_expiry = COVER_FOREVER;
	else if (COVER_SATURATE(expiry) > block.cover_expiry)
		block.cover_expiry = COVER_SATURATE(expiry);

	shield_record_write(record, &block);
}

bool gp_chunk_shield_uncovered(const gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, u64 now)
{
	if (!index_valid(chunk, x, y, z))
		return true;

	u8 *record = gp_sparse_get((gp_sparse *)&chunk->shield, gp_block_index(x, y, z, chunk->chunk_height), false);
	if (!record)
		return true;

	gp_shield_block block;
	shield_record_read(record, &block);
	return !block_covered(&block, now);
}

u32 gp_chunk_shield_covered_count(const gp_chunk_reinf *chunk, u64 now)
{
	if (!chunk)
		return 0;

	u32 covered = 0;
	for (u32 i = 0; i < chunk->shield.capacity; i++)
	{
		if (chunk->shield.keys[i] == GP_SPARSE_KEY_EMPTY || chunk->shield.keys[i] == GP_SPARSE_KEY_DEAD)
			continue;

		gp_shield_block block;
		shield_record_read(chunk->shield.values + (size_t)i * GP_SHIELD_BLOCK_VALUE_SIZE, &block);
		if (block_covered(&block, now))
			covered++;
	}

	return covered;
}

void gp_chunk_shield_visit(const gp_chunk_reinf *chunk, gp_shield_visit_fn fn, void *user_data)
{
	if (!fn)
		return;

	for (u32 i = 0; i < chunk->shield.capacity; i++)
	{
		if (chunk->shield.keys[i] == GP_SPARSE_KEY_EMPTY || chunk->shield.keys[i] == GP_SPARSE_KEY_DEAD)
			continue;

		gp_shield_block block;
		shield_record_read(chunk->shield.values + (size_t)i * GP_SHIELD_BLOCK_VALUE_SIZE, &block);
		if (block.points == 0)
			continue;

		u32 x, y, z;
		index_to_xyz(chunk->shield.keys[i], &x, &y, &z);
		fn(user_data, x, y, z, block.points);
	}
}

bool gp_chunk_resolve_break(gp_chunk_reinf *chunk, u32 x, u32 y, u32 z, gp_face_t face, gp_break_result *out)
{
	if (out)
		memset(out, 0, sizeof(*out));

	if (!chunk || !index_valid(chunk, x, y, z))
		return false;

	// a reinforced face always wins, and the shield is not touched
	if (gp_chunk_reinf_consume(chunk, x, y, z, face))
	{
		if (out)
		{
			out->reinf_survived = true;
			out->shield_left = gp_chunk_shield_get(chunk, x, y, z);
		}
		return true;
	}

	u32 taken = gp_chunk_shield_take(chunk, x, y, z, 1);

	if (out)
	{
		out->shield_absorbed = taken;
		out->shield_left = gp_chunk_shield_get(chunk, x, y, z);
		out->block_broke = taken == 0;
	}

	// the event was always resolved, `out->block_broke` says whether the block
	// actually went through
	return true;
}

u32 gp_chunk_shield_decay(gp_chunk_reinf *chunk, u64 now, u32 interval, u32 amount, u32 *blocks_touched, u32 *blocks_skipped)
{
	if (blocks_touched)
		*blocks_touched = 0;
	if (blocks_skipped)
		*blocks_skipped = 0;

	if (!chunk)
		return 0;

	if (amount == 0)
		return 0;

	// First ever call: pick a due time and give the points a full interval of
	// grace before the first sweep.
	if (chunk->next_decay == 0)
	{
		chunk->next_decay = now + (interval ? interval : 1);
		return 0;
	}

	if (now < chunk->next_decay)
		return 0;

	// Advance from the previous due time rather than from now, so a host that
	// calls this every tick keeps a stable cadence instead of perpetually
	// pushing the sweep into the future.
	u64 next = chunk->next_decay + (interval ? interval : 1);
	if (next <= now)
		next = now + (interval ? interval : 1);
	chunk->next_decay = next;

	if (chunk->shield_entries == 0)
		return 0;

	// collect first, then mutate: removing entries invalidates table positions
	u32 *victims = (u32 *)malloc(sizeof(u32) * chunk->shield_entries);
	if (!victims)
		return 0;

	u32 count = 0;
	for (u32 i = 0; i < chunk->shield.capacity && count < chunk->shield_entries; i++)
	{
		if (chunk->shield.keys[i] == GP_SPARSE_KEY_EMPTY || chunk->shield.keys[i] == GP_SPARSE_KEY_DEAD)
			continue;
		victims[count++] = chunk->shield.keys[i];
	}

	u32 removed = 0;
	for (u32 i = 0; i < count; i++)
	{
		u32 x, y, z;
		index_to_xyz(victims[i], &x, &y, &z);

		// A live shield is holding this block up. Bleeding it here would show a
		// visible dip in durability every window and refill it again moments
		// later, so leave it alone until the coverage claim actually lapses.
		if (!gp_chunk_shield_uncovered(chunk, x, y, z, now))
		{
			if (blocks_skipped)
				(*blocks_skipped)++;
			continue;
		}

		u32 taken = gp_chunk_shield_take(chunk, x, y, z, amount);
		if (taken == 0)
			continue;

		removed += taken;
		if (blocks_touched)
			(*blocks_touched)++;
	}

	free(victims);
	return removed;
}
