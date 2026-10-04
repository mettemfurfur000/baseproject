#ifndef GRIEFPROT_SERIALIZE_H
#define GRIEFPROT_SERIALIZE_H

#include "general.h"
#include "griefprot.h"
#include "griefprot_chunk.h"
#include "griefprot_shield.h"
#include "griefprot_world.h"
#include "arena.h"
#include "data_io.h"
#include "tkv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On-disk format version. Bumped whenever a record layout changes so old data
   is detected instead of misread.
 *
 * Version 2: the per-block shield record grew from 8 to 12 bytes to carry a
 * coverage expiry alongside its point count. Version 1 files are rejected rather
 * than migrated, because a coverage claim cannot be reconstructed from data
 * written before coverage existed, and silently reading those blocks as
 * uncovered would bleed every shield the first time decay ran. */
#define GP_FORMAT_VERSION 2

/* temlib's tkv_object_add_field writes values through an 8192 byte stack buffer,
   so a single array field can never exceed that. Records are therefore split
   across numbered groups. 512 * 14 bytes leaves comfortable headroom. */
#define GP_RECORDS_PER_GROUP 512

/* Largest block index representable, given a 16 x H x 16 chunk with a sane H. */
#define GP_MAX_BLOCK_INDEX 0xFFFFFFFBu

/* --- per chunk -------------------------------------------------------------- */

/* Builds a TKV object describing `chunk`. `out` owns the result, which is small
   enough (well under 8 KB per field) to write straight to a stream.
   Returns NULL on allocation failure or if the chunk holds an impossible
   number of records. */
tkv_object gp_chunk_pack(const gp_chunk_reinf *chunk, arena *out);

/* Restores a chunk from an object produced by gp_chunk_pack. Data already held
   by `chunk` is released first. Returns false on malformed input or a version
   mismatch. */
bool gp_chunk_unpack(tkv_object obj, gp_chunk_reinf *chunk);

/* Writes one self delimiting chunk blob: u32 byte length, then that many bytes.
   Reading one back is `gp_chunk_read_stream`. */
u8 gp_chunk_write_stream(const gp_chunk_reinf *chunk, stream_t *s);
bool gp_chunk_read_stream(stream_t *s, gp_chunk_reinf *chunk, arena *scratch, arena *out);

/* --- shield ----------------------------------------------------------------- */

/* Shield state including the whole target list, stored as flat i32 triples. */
tkv_object gp_shield_pack(const gp_shield *shield, arena *out);

/* Restores into `shield`, which must have room for at least
   `shield->target_capacity` targets. Returns false on malformed input. */
bool gp_shield_unpack(tkv_object obj, gp_shield *shield);

/* --- whole world ------------------------------------------------------------ */

/*
   The world is written as a small metadata TKV object followed by length
   prefixed per chunk and per shield blobs. Nesting every chunk inside one TKV
   object would be quadratic in temlib, and one object cannot exceed 8 KB per
   value, so each chunk gets its own tree instead.

   On a gzip stream the whole container compresses as one unit, which suits
   protection data that is mostly small repeated records.

   Shields are restored into caller supplied storage: `shield_pool` holds up to
   `shield_pool_size` slots, each created with room for
   `shield_target_capacity` targets. They are registered with the world but not
   owned by it; release them with gp_shield_destroy. Pass NULL/0 to load chunk
   data only. Returns the number of shields restored, or -1 on failure.
*/
i32 gp_world_unpack(tkv_object header, stream_t *s, gp_world *world, gp_shield **shield_pool,
                    u32 shield_pool_size, u32 shield_target_capacity, arena *scratch);

u8 gp_world_write_stream(const gp_world *world, stream_t *s);

/* Builds just the world metadata object. Exposed so hosts can inspect a save
   before committing to loading it. */
tkv_object gp_world_pack_header(const gp_world *world, arena *out);

/* --- helpers ---------------------------------------------------------------- */

/* Serialized size of a chunk in bytes, including its length prefix. Measuring a
   real object is exact; this is the cheap upper bound. */
u64 gp_chunk_packed_size(const gp_chunk_reinf *chunk);

#ifdef __cplusplus
}
#endif

#endif // GP_GRIEFPROT_SERIALIZE_H