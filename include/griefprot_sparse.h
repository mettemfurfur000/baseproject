#ifndef GRIEFPROT_SPARSE_H
#define GRIEFPROT_SPARSE_H

#include "general.h"

/*
	Compact open-addressing map used for the per-chunk sparse protection data.

	Keys are dense block indices, values are fixed size records. The table grows
	to a power of two and is probed with linear probing, so a chunk pays only for
	the blocks that actually carry protection instead of for its whole volume.

	  GP_SPARSE_KEY_EMPTY - never used, terminates a probe chain
	  GP_SPARSE_KEY_DEAD  - used then removed, does not terminate a probe chain
*/

#define GP_SPARSE_KEY_EMPTY 0xFFFFFFFFu
#define GP_SPARSE_KEY_DEAD 0xFFFFFFFEu

typedef struct
{
	u32 *keys;       // EMPTY / DEAD / live key
	u8 *values;      // value_size bytes per slot, parallel to keys
	u32 capacity;    // always a power of two
	u32 value_size;  // bytes per record
	u32 count;       // live keys
	u32 dead;        // tombstones awaiting compaction
} gp_sparse;

void gp_sparse_init(gp_sparse *m, u32 value_size);
void gp_sparse_free(gp_sparse *m);

/* Ensures room for `min_capacity` slots at the configured load factor. */
int gp_sparse_reserve(gp_sparse *m, u32 min_capacity);

/* Returns a pointer to the record for `key`, or NULL when absent. When `create`
   is true a zero filled record is allocated and returned. The pointer stays valid
   until the next insert, remove or compact on this map. */
u8 *gp_sparse_get(gp_sparse *m, u32 key, bool create);

void gp_sparse_remove(gp_sparse *m, u32 key);

/* Rebuilds without tombstones. Called automatically once they dominate. */
void gp_sparse_compact(gp_sparse *m);

static inline u64 gp_sparse_memory(const gp_sparse *m)
{
	return (u64)m->capacity * (sizeof(u32) + m->value_size);
}

/* Dense index of a block inside a 16 x chunk_height x 16 chunk. */
static inline u32 gp_block_index(u32 x, u32 y, u32 z, u32 chunk_height)
{
	(void)chunk_height;
	return (y * 16u + z) * 16u + x;
}

#endif // GP_SPARSE_SPARSE_H
