#ifndef GRIEFPROT_WORLD_H
#define GRIEFPROT_WORLD_H

#include "general.h"
#include "griefprot.h"
#include "griefprot_chunk.h"
#include "griefprot_shield.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A chunk's protection data plus the coordinates it belongs to. */
typedef struct
{
	i32 cx, cz;
	bool loaded; // false while the chunk's data has been released
	gp_chunk_reinf data;
} gp_chunk_slot;

/* Chunk directory.

   Separate from the per-chunk sparse maps because chunk coordinates need 64 bits
   and block indices only 32. Keeping the block tables narrow is worth a second
   small map here. */
typedef struct
{
	i64 keys;  // packed cx,cz, or GP_CHUNK_DIR_EMPTY
	u32 slot;  // index into gp_world::chunks
} gp_chunk_dir_entry;

#define GP_CHUNK_DIR_EMPTY 0x7FFFFFFFFFFFFFFFLL

typedef struct
{
	gp_chunk_slot *chunks;
	u32 chunk_count;
	u32 chunk_capacity;

	gp_chunk_dir_entry *dir; // open addressed, power of two
	u32 dir_capacity;

	gp_shield **shields;
	u32 shield_count;
	u32 shield_capacity;

	gp_config cfg;
} gp_world;

/* Creates a world with default config. Returns NULL on failure. */
gp_world *gp_world_create(void);
void gp_world_destroy(gp_world *world);

void gp_world_set_config(gp_world *world, const gp_config *cfg);
const gp_config *gp_world_get_config(const gp_world *world);

/* Chunk lookup by chunk coordinates. Returns NULL when the chunk is not tracked.
   `gp_world_chunk` allocates on first access. */
gp_chunk_reinf *gp_world_find_chunk(const gp_world *world, i32 cx, i32 cz);
gp_chunk_reinf *gp_world_chunk(gp_world *world, i32 cx, i32 cz);

/* Releases a chunk's data. Its directory slot stays so the coordinates can be
   re-registered later. Returns false when the chunk was not tracked. */
bool gp_world_unload_chunk(gp_world *world, i32 cx, i32 cz);

/* Convenience: maps a block position to its chunk and local coordinates. */
bool gp_world_pos_to_local(gp_pos pos, i32 chunk_height, i32 *cx, i32 *cz, u32 *lx, u32 *ly, u32 *lz);

gp_chunk_reinf *gp_world_chunk_at(gp_world *world, gp_pos pos);

/* Shield registry, keyed by absolute position. The world does not own the
   shield's target list allocation, only the pointer. */
gp_shield *gp_world_find_shield(const gp_world *world, gp_pos pos);
gp_shield *gp_world_add_shield(gp_world *world, gp_shield *shield);
bool gp_world_remove_shield(gp_world *world, gp_pos pos);

/* Total heap held by all tracked chunk protection data. */
u64 gp_world_memory(const gp_world *world);

/* Block movement, as reported by pistons and droppers.

   `motion` is the direction the block travels. The side it was pushed or pulled
   from is the opposite face, for both cases: a block shoved east came off its
   west side, and one dragged west came off its east side.

   Returns false, changing nothing, when:

   - `motion` is not a real face, or either position is outside the world
   - the destination block's face entered by the moving block is reinforced
   - the destination's faces plus the incoming ones would need more than
     `cfg.max_faces_per_block` distinct faces

   On success reinforcement keeps its world space direction, since a piston
   translates a block without rotating it, durability on a shared face is summed,
   and shield points travel with the block. The host event handler is responsible
   for rejecting motion away from protected source faces, since push and pull
   events use different face semantics. Cross chunk moves are handled here;
   `gp_chunk_reinf_move` and `gp_chunk_shield_move` only cover one chunk.

   A move between two positions with no protection at all is a no-op that still
   returns true, so the host can pass every block event through. */
/* Ready made gp_shield_sink::cover callback for a gp_world, so a host does not
   have to write the chunk lookup itself. Marks the block as covered for one decay
   interval past `now`, which is long enough to outlast a sweep and short enough
   that a shield which has gone away stops protecting its blocks shortly after.

   Set `sink.cover = gp_world_shield_cover` and `sink.user = world`. Blocks in
   chunks that are not loaded are ignored, which is what lets a protected chunk
   decay while the chunk holding the shield sits unloaded. */
void gp_world_shield_cover(void *user, gp_pos pos, u64 now);

bool gp_world_move_block(gp_world *world, gp_pos from, gp_pos to, gp_face_t motion);

#ifdef __cplusplus
}
#endif

#endif // GP_GRIEFPROT_WORLD_H
