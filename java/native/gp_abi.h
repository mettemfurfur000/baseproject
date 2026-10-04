/*
 * gp_abi.h - a flat, FFM friendly surface over the griefprot C library.
 *
 * Why this exists
 * ---------------
 * The library's own API is idiomatic C: structs by value, opaque pointers,
 * function pointer callbacks. Binding that directly from Java means hand
 * writing a GroupLayout for every struct and trusting that it matches the C
 * compiler's idea of the layout. Get one offset wrong and the JVM corrupts
 * memory instead of throwing.
 *
 * So this shim is the entire boundary. Java sees:
 *
 *   - u32 handles, never raw pointers to C objects
 *   - i32 / u32 / u64 scalars
 *   - pointers only for caller allocated out parameters and scratch
 *
 * There is no struct in this header that Java has to lay out. gp_pos crosses
 * no boundary at all; it is taken apart into x, y, z. gp_config is never
 * built in Java; the shim fills it from the library defaults and then patches
 * the fields it was asked to change. The one struct that does cross, the sink,
 * is assembled here in C from three plain function addresses that Java passes
 * as integers.
 *
 * Out parameters use pointers because that maps straight onto a Java
 * MemorySegment and avoids packing multi-value returns into bit fields, which
 * is a class of bug best avoided in code nobody can debug.
 *
 * Every function here is safe to call with handle 0; it returns a failure
 * value instead. Handles are recycled, so a stale handle refers to whatever
 * world took that slot next.
 */

#ifndef GP_ABI_H
#define GP_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles. 0 is always invalid; values are index + 1. */
typedef uint32_t gp_h_world;
typedef uint32_t gp_h_shield;

/* --- ABI self check ------------------------------------------------------- */

/*
 * Verifies the assumptions the Java side is built on, so a toolchain or
 * platform mismatch fails loudly at startup instead of corrupting state later.
 *
 * Returns a bitmask of failed checks. 0 means everything matched.
 */
uint32_t gp_abi_check(void);

/* Expected value of check bit `bit`, and a human readable name for it. Used by
   the host to print a useful diff rather than just "ABI mismatch". */
int64_t gp_abi_expect(uint32_t bit);
const char *gp_abi_check_name(uint32_t bit);

/* Check bit ids, exposed so Java does not hardcode magic numbers. */
enum
{
	GP_ABI_CHECK_PTR_SIZE = 1u << 0,
	GP_ABI_CHECK_U32_SIZE = 1u << 1,
	GP_ABI_CHECK_U64_SIZE = 1u << 2,
	GP_ABI_CHECK_POS_SIZE = 1u << 3,
	GP_ABI_CHECK_BOOL_SIZE = 1u << 4,
	GP_ABI_CHECK_FACE_VALUES = 1u << 5,
	GP_ABI_CHECK_BREAK_RESULT_SIZE = 1u << 6,
	GP_ABI_CHECK_POS_IS_3XI32 = 1u << 7,
};

/* --- face ids, mirrored for the host -------------------------------------- */
/* These match gp_face_t. Exposed so the host does not hardcode them either. */
uint32_t gp_abi_face_none(void);
uint32_t gp_abi_face_down(void);
uint32_t gp_abi_face_up(void);
uint32_t gp_abi_face_north(void);
uint32_t gp_abi_face_south(void);
uint32_t gp_abi_face_west(void);
uint32_t gp_abi_face_east(void);
uint32_t gp_abi_face_count(void);

/* Sentinel returned by gp_abi_weakest_face when a block carries no
   reinforcement at all. */
#define GP_ABI_NO_FACE 0xFFFFFFFFu

/* --- world lifecycle ------------------------------------------------------ */

gp_h_world gp_abi_world_create(void);
void gp_abi_world_destroy(gp_h_world world);

/*
 * Applies configuration. Starts from the library defaults, then overrides the
 * fields whose value is not GP_ABI_KEEP, so a host can set one value without
 * having to restate the rest.
 *
 * tier names are left as the library sets them; the library does not read them.
 */
#define GP_ABI_KEEP 0xFFFFFFFFu

void gp_abi_world_config(gp_h_world world, uint32_t max_faces_per_block, uint32_t shield_pool,
                         uint32_t shield_regen, uint32_t shield_window, uint32_t shield_range,
                         uint32_t shield_max_targets, uint32_t shield_fuel_max,
                         uint32_t shield_fuel_per_sec, uint32_t decay_interval, uint32_t decay_amount,
                         uint32_t chunk_height, uint32_t copper_breaks, uint32_t iron_breaks);

/* Reads back the configuration actually in force, one field per out param.
   Any out param may be NULL. */
void gp_abi_world_config_get(gp_h_world world, uint32_t *max_faces_per_block, uint32_t *shield_pool,
                             uint32_t *shield_regen, uint32_t *shield_window, uint32_t *shield_range,
                             uint32_t *shield_max_targets, uint32_t *shield_fuel_max,
                             uint32_t *shield_fuel_per_sec, uint32_t *decay_interval,
                             uint32_t *decay_amount, uint32_t *chunk_height, uint32_t *copper_breaks,
                             uint32_t *iron_breaks);

/* --- chunks --------------------------------------------------------------- */

/*
 * Loads or creates the chunk owning a position. Returns 1 when a loaded chunk
 * was found or created, 0 when the position is outside the configured height or
 * the world handle is bad.
 */
int gp_abi_chunk_at(gp_h_world world, int32_t x, int32_t y, int32_t z);

/* 1 when a chunk is currently loaded, without creating one. */
int gp_abi_chunk_is_loaded(gp_h_world world, int32_t cx, int32_t cz);

/* Returns 1 when a chunk was loaded and is now released. */
int gp_abi_chunk_unload(gp_h_world world, int32_t cx, int32_t cz);

/* Number of chunks currently loaded, and how many slots the world has reserved
   in total. Both out params optional. */
void gp_abi_chunk_counts(gp_h_world world, uint32_t *loaded, uint32_t *reserved);

/*
 * Runs one decay sweep on the chunk owning a position. Points removed is
 * returned; touched and skipped may be NULL.
 */
uint32_t gp_abi_chunk_decay(gp_h_world world, int32_t x, int32_t y, int32_t z, uint64_t now,
                            uint32_t interval, uint32_t amount, uint32_t *touched, uint32_t *skipped);

/* How many blocks in this chunk a live shield is holding up at `now`. */
uint32_t gp_abi_chunk_covered(gp_h_world world, int32_t x, int32_t y, int32_t z, uint64_t now);

/* Total bytes of protection data held by the world. */
uint64_t gp_abi_world_memory(gp_h_world world);

/* --- reinforcement -------------------------------------------------------- */

/*
 * Applies `amount` break events to one face. Returns 1 on success, 0 when the
 * block is out of range, that face is already reinforced, or the merge would
 * need more distinct faces than the world allows.
 */
int gp_abi_reinf_add(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t face, uint32_t amount);

/* Remaining break events on a face, 0 when the face carries none. */
uint32_t gp_abi_reinf_face_durability(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t face);

int gp_abi_reinf_face_active(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t face);

/* Drops every reinforced face on a block. Returns 1 when something was there. */
int gp_abi_reinf_clear(gp_h_world world, int32_t x, int32_t y, int32_t z);

/* How many distinct faces carry reinforcement, 0..6. */
uint32_t gp_abi_reinf_face_count(gp_h_world world, int32_t x, int32_t y, int32_t z);

/*
 * The reinforced face with the least durability left, or GP_ABI_NO_FACE.
 *
 * This is what a host resolves a break against when the server did not say
 * which face was struck: the attacker can always aim for the weakest face, so
 * assuming any stronger one would promise protection the block does not have.
 */
uint32_t gp_abi_weakest_face(gp_h_world world, int32_t x, int32_t y, int32_t z);

/* --- break resolution ----------------------------------------------------- */

/*
 * Resolves one break. `use_reinf` and `use_shield` let a host decide which
 * layers participate. reinf_survived, block_broke, absorbed and left may each be
 * NULL.
 *
 * Returns 1 on success.
 */
int gp_abi_resolve_break(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t face,
                         uint32_t use_reinf, uint32_t use_shield, uint32_t *reinf_survived,
                         uint32_t *block_broke, uint32_t *absorbed, uint32_t *left);

/* --- shield points -------------------------------------------------------- */

uint32_t gp_abi_points_get(gp_h_world world, int32_t x, int32_t y, int32_t z);
uint32_t gp_abi_points_add(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t amount,
                           uint32_t cap);
uint32_t gp_abi_points_take(gp_h_world world, int32_t x, int32_t y, int32_t z, uint32_t amount);

/* --- block movement ------------------------------------------------------- */

/*
 * Moves one block's protection. `motion` is the direction the block travels,
 * matching the piston event's getDirection. Returns 1 on success, 0 when
 * refused, in which case nothing was changed.
 */
int gp_abi_move_block(gp_h_world world, int32_t from_x, int32_t from_y, int32_t from_z, int32_t to_x,
                      int32_t to_y, int32_t to_z, uint32_t motion);

/* --- shield blocks -------------------------------------------------------- */

/*
 * Installs the host callbacks the library uses to decide what may be protected
 * and to hand out points.
 *
 * Each function address is a pointer to a C function with the signature the
 * library expects, which Java obtains from an FFM upcall stub and passes here as
 * an integer. Pass 0 to clear a callback.
 *
 *   protectable: int (*)(uint64_t host, int32_t x, int32_t y, int32_t z)
 *   grant:       uint32_t (*)(uint64_t host, int32_t x, int32_t y, int32_t z,
 *                             uint32_t amount, uint32_t cap)
 *   cover:       void (*)(uint64_t host, int32_t x, int32_t y, int32_t z, uint64_t now)
 *
 * Returns 1 on success.
 */
int gp_abi_sink_configure(gp_h_world world, uint64_t host_id, uint64_t fn_protectable,
                          uint64_t fn_grant, uint64_t fn_cover);

/* Marks a block covered until one decay interval past `now`, without going
   through a shield. Optional; the cover callback already does this. */
void gp_abi_cover(gp_h_world world, int32_t x, int32_t y, int32_t z, uint64_t now);

/* Creates a shield block at a position. Returns a handle, 0 on failure. */
gp_h_shield gp_abi_shield_create(gp_h_world world, int32_t x, int32_t y, int32_t z);
void gp_abi_shield_destroy(gp_h_shield shield);

int gp_abi_shield_add_target(gp_h_shield shield, int32_t x, int32_t y, int32_t z);
int gp_abi_shield_remove_target(gp_h_shield shield, int32_t x, int32_t y, int32_t z);
void gp_abi_shield_clear_targets(gp_h_shield shield);

void gp_abi_shield_set_powered(gp_h_shield shield, int powered);
void gp_abi_shield_set_fuel(gp_h_shield shield, uint32_t fuel);
void gp_abi_shield_get_state(gp_h_shield shield, uint32_t *powered, uint32_t *running, uint32_t *fuel);
void gp_abi_shield_get_pos(gp_h_shield shield, int32_t *x, int32_t *y, int32_t *z);

/*
 * Advances one shield and returns the points granted. `now` is a host monotonic
 * clock in seconds; the library only compares differences. points_granted may
 * be NULL.
 */
uint32_t gp_abi_shield_tick(gp_h_shield shield, uint64_t now, uint32_t *points_granted);

/* Handles for iterating the world's shields, for chunk unload bookkeeping.
   Pass index 0 upwards; returns 0 past the end. */
gp_h_shield gp_abi_shield_at(gp_h_world world, uint32_t index);
uint32_t gp_abi_shield_count(gp_h_world world);

/* --- persistence ---------------------------------------------------------- */

/*
 * Writes the whole world to `path` as gzip. Returns 1 on success; on failure
 * gp_abi_last_error describes what went wrong.
 */
int gp_abi_world_save(gp_h_world world, const char *path);

/*
 * Reads a world back into an existing handle, replacing its contents. Shields
 * already attached to the world are released first. `shield_pool_size` bounds
 * how many shields may be restored and `shield_target_capacity` how many targets
 * each may hold.
 *
 * Returns the number of shields restored, or -1 on failure.
 */
int gp_abi_world_load(gp_h_world world, const char *path, uint32_t shield_pool_size,
                      uint32_t shield_target_capacity);

/* Last failure message for this thread, or NULL. Valid until the next failing
   call on the same thread. */
const char *gp_abi_last_error(void);

/* --- library version ------------------------------------------------------ */

/* Packed as (major << 16) | minor, so a host can assert it bound the library it
   was built against. */
uint32_t gp_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* GP_ABI_H */