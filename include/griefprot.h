#ifndef GRIEFPROT_H
#define GRIEFPROT_H

#include "general.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	i32 x, y, z;
} gp_pos;

static inline bool gp_pos_eq(gp_pos a, gp_pos b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

static inline gp_pos gp_pos_add(gp_pos a, gp_pos b)
{
	gp_pos r = {a.x + b.x, a.y + b.y, a.z + b.z};
	return r;
}

/* Absolute value distance, used for the cube shaped fallback range. */
static inline u32 gp_pos_chebyshev(gp_pos a, gp_pos b)
{
	i32 dx = a.x > b.x ? a.x - b.x : b.x - a.x;
	i32 dy = a.y > b.y ? a.y - b.y : b.y - a.y;
	i32 dz = a.z > b.z ? a.z - b.z : b.z - a.z;
	u32 m = (u32)dx;
	if ((u32)dy > m)
		m = (u32)dy;
	if ((u32)dz > m)
		m = (u32)dz;
	return m;
}

/* Reinforcement tiers: a consumable applied to a face adds `breaks` extra break
   events to that face. Values are configurable per deployment because balance
   differs wildly between communities. */
typedef struct
{
	const char *name;
	u16 breaks;
} gp_reinf_tier;

typedef struct
{
	/* reinforcement */
	gp_reinf_tier copper;    // default 32
	gp_reinf_tier iron;      // default 256
	u32 max_faces_per_block; // default 4

	/* shield */
	u32 shield_pool;         // 4096, defines each target's share rather than a depletable budget
	u32 shield_regen;        // points granted per regeneration window, default 20
	u32 shield_window;       // length of one regeneration window in seconds, default 1
	u32 shield_range;        // fallback cube radius when no targets are selected, default 4
	u32 shield_max_targets;  // default 64
	u32 shield_fuel_max;     // fuel units a shield holds, default 64
	u32 shield_fuel_per_sec; // fuel burnt per second while running, default 1

	/* shield decay for blocks no longer covered by a running shield */
	u32 decay_interval; // seconds between decay passes, default 300
	u32 decay_amount;   // points removed per pass, default 1

	/* world dimensions the library is tuned for */
	u32 chunk_height; // default 384 overworld, 128 nether
} gp_config;

/* Fills `cfg` with the defaults listed above. */
void gp_config_defaults(gp_config *cfg);

#ifdef __cplusplus
}
#endif

#endif // GRIEFPROT_H
