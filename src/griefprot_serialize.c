#include "griefprot_serialize.h"
#include <string.h>

#define GP_REINF_REC_SIZE (sizeof(u32) + GP_SPARSE_REINF_VALUE_SIZE) // 14
#define GP_SHIELD_REC_SIZE (sizeof(u32) * 3)                         // index + points + coverage

/* TKV reads scalars back through stream_read, which converts sizes below 9
   bytes, but array payloads are copied verbatim. Everything multi byte inside a
   record is therefore encoded little endian by hand. */

static void put_u32(u8 *p, u32 v)
{
	p[0] = (u8)(v);
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static u32 get_u32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Scalar field lookup. TKV exposes values as tagged unions, so every read has to
   confirm the type before trusting the payload. */
static bool get_u32_field(tkv_object obj, const char *key, u32 *out)
{
	tkv_value val = tkv_get_value(obj, key);
	if (val.meta.whole == UINT_MAX || val.meta.tkv_value_type != TKV_VALUE_U32)
		return false;

	*out = *(u32 *)val.ptr;
	return true;
}

/* --- small helpers ---------------------------------------------------------- */

/* tkv_object_add_field copies the object into a fresh allocation and returns the
   new pointer, so every field must rebind `obj` rather than mutate it in place. */
#define GP_ADD(obj, key, type, value, arena) \
	((obj) = tkv_object_add_field((obj), (key), (type), TKV_STATE_CONST, (value), (arena)))

static tkv_object add_array(tkv_object obj, const char *key, u16 element_size, u16 count, const u8 *bytes,
                            arena *out)
{
	tkv_array arr = {.element_size = element_size, .array_length = count, .bytes = (u8 *)bytes};
	return tkv_object_add_field(obj, key, TKV_VALUE_ARR, TKV_STATE_CONST, &arr, out);
}

/* Writes a length prefixed TKV blob. tkv_write_to_stream does not report how many
   bytes it consumed, so the object is serialized to a scratch buffer first. */
static u8 write_framed(tkv_object obj, stream_t *s)
{
	if (!obj)
		return FAIL;

	stream_t mem;
	memset(&mem, 0, sizeof(mem));
	mem.mode = STREAM_BUF;

	if (tkv_write_to_stream(obj, &mem) != 0)
		return FAIL;

	u32 len = (u32)mem.handle.raw.bytes.length;
	if (stream_write((const u8 *)&len, sizeof(len), s) != 0)
		return FAIL;
	if (len && stream_write(mem.handle.raw.bytes.data, len, s) != 0)
		return FAIL;

	return 0;
}

static tkv_object read_framed(stream_t *s, arena *scratch, arena *out, u32 *out_len)
{
	u32 len = 0;
	if (stream_read(&len, sizeof(len), s) != 0)
		return NULL;

	if (out_len)
		*out_len = len;
	if (len == 0)
		return NULL;

	/* Bound the frame so a corrupt length cannot drive a huge allocation. The
	   object is validated by the caller; this just refuses the absurd. */
	if (len > (1u << 24))
		return NULL;

	u8 *buf = (u8 *)malloc(len);
	if (!buf)
		return NULL;

	if (stream_read(buf, len, s) != 0)
	{
		free(buf);
		return NULL;
	}

	stream_t mem;
	memset(&mem, 0, sizeof(mem));
	mem.mode = STREAM_BUF;
	mem.handle.raw.bytes.data = buf;
	mem.handle.raw.bytes.length = len;
	mem.handle.raw.bytes.capacity = len;
	mem.handle.raw.read_idx = 0;

	tkv_object obj = tkv_read_from_stream(&mem, scratch, out);
	free(buf);
	return obj;
}

/* --- per chunk -------------------------------------------------------------- */

tkv_object gp_chunk_pack(const gp_chunk_reinf *chunk, arena *out)
{
	if (!chunk || !out)
		return NULL;

	tkv_object obj = tkv_object_create_empty(out);
	if (!obj)
		return NULL;

	u32 ver = GP_FORMAT_VERSION;
	u32 height = chunk->chunk_height;
	u32 nrg = chunk->reinf.count;
	u32 nsd = chunk->shield.count;
	u64 ndc = chunk->next_decay;

	if (!GP_ADD(obj, "ver", TKV_VALUE_U32, &ver, out))
		return NULL;
	if (!GP_ADD(obj, "chh", TKV_VALUE_U32, &height, out))
		return NULL;
	if (!GP_ADD(obj, "ndc", TKV_VALUE_U64, &ndc, out))
		return NULL;
	if (!GP_ADD(obj, "nrg", TKV_VALUE_U32, &nrg, out))
		return NULL;
	if (!GP_ADD(obj, "nsd", TKV_VALUE_U32, &nsd, out))
		return NULL;

	u8 buf[GP_RECORDS_PER_GROUP * GP_REINF_REC_SIZE];
	u16 count = 0;
	u32 group = 0;

	/* One pass over the table, flushing a group whenever it fills. Scanning
	   afresh per group would repack the first records every time. */
	for (u32 i = 0; i < chunk->reinf.capacity; i++)
	{
		u32 key = chunk->reinf.keys[i];
		if (key == GP_SPARSE_KEY_EMPTY || key == GP_SPARSE_KEY_DEAD)
			continue;

		u8 *slot = buf + (u32)count * GP_REINF_REC_SIZE;
		put_u32(slot, key);
		memcpy(slot + sizeof(u32), chunk->reinf.values + (size_t)i * GP_SPARSE_REINF_VALUE_SIZE,
		       GP_SPARSE_REINF_VALUE_SIZE);

		if (++count < GP_RECORDS_PER_GROUP)
			continue;

		char key_name[8];
		snprintf(key_name, sizeof(key_name), "r%u", group++);
		obj = add_array(obj, key_name, GP_REINF_REC_SIZE, count, buf, out);
		if (!obj)
			return NULL;
		count = 0;
	}

	if (count)
	{
		char key_name[8];
		snprintf(key_name, sizeof(key_name), "r%u", group);
		obj = add_array(obj, key_name, GP_REINF_REC_SIZE, count, buf, out);
		if (!obj)
			return NULL;
	}

	u8 sbuf[GP_RECORDS_PER_GROUP * GP_SHIELD_REC_SIZE];
	count = 0;
	group = 0;

	for (u32 i = 0; i < chunk->shield.capacity; i++)
	{
		u32 key = chunk->shield.keys[i];
		if (key == GP_SPARSE_KEY_EMPTY || key == GP_SPARSE_KEY_DEAD)
			continue;

		u8 *slot = sbuf + (u32)count * GP_SHIELD_REC_SIZE;
		put_u32(slot, key);
		// the shield value is {u32 points; u32 cover_expiry}, so stride two words
		put_u32(slot + sizeof(u32), get_u32(chunk->shield.values + (size_t)i * sizeof(u32) * 2));
		put_u32(slot + sizeof(u32) * 2, get_u32(chunk->shield.values + (size_t)i * sizeof(u32) * 2 + sizeof(u32)));

		if (++count < GP_RECORDS_PER_GROUP)
			continue;

		char key_name[8];
		snprintf(key_name, sizeof(key_name), "s%u", group++);
		obj = add_array(obj, key_name, GP_SHIELD_REC_SIZE, count, sbuf, out);
		if (!obj)
			return NULL;
		count = 0;
	}

	if (count)
	{
		char key_name[8];
		snprintf(key_name, sizeof(key_name), "s%u", group);
		obj = add_array(obj, key_name, GP_SHIELD_REC_SIZE, count, sbuf, out);
		if (!obj)
			return NULL;
	}

	return obj;
}

/* Fills `out` with the array for a group key. Returns false when the group is
   absent; sets `*bad` when the key exists but is not a well formed array of the
   expected element size. */
static bool group_array(tkv_object obj, const char *key_name, u32 element_size, tkv_array *out, bool *bad)
{
	*bad = false;

	tkv_value val = tkv_get_value(obj, key_name);
	if (val.meta.whole == UINT_MAX)
		return false;

	if (val.meta.tkv_value_type != TKV_VALUE_ARR)
	{
		*bad = true;
		return false;
	}

	*out = tkv_value_to_arr(val);
	if (out->element_size != element_size)
	{
		*bad = true;
		return false;
	}

	return true;
}

bool gp_chunk_unpack(tkv_object obj, gp_chunk_reinf *chunk)
{
	if (!obj || !chunk)
		return false;

	u32 ver = 0, height = 0;
	u64 ndc = 0;
	u32 nrg = 0, nsd = 0;

	if (!get_u32_field(obj, "ver", &ver) || ver != GP_FORMAT_VERSION)
		return false;
	if (!get_u32_field(obj, "chh", &height) || height == 0)
		return false;
	if (!get_u32_field(obj, "nrg", &nrg) || !get_u32_field(obj, "nsd", &nsd))
		return false;

	tkv_value ndc_val = tkv_get_value(obj, "ndc");
	if (ndc_val.meta.tkv_value_type != TKV_VALUE_U64)
		return false;
	ndc = *(u64 *)ndc_val.ptr;

	u64 volume = 256ull * height;
	if (nrg > volume || nsd > volume)
		return false;

	gp_chunk_reinf_init(chunk, height);
	chunk->next_decay = ndc;

	for (u32 group = 0; group * GP_RECORDS_PER_GROUP < nrg; group++)
	{
		char key_name[8];
		snprintf(key_name, sizeof(key_name), "r%u", group);

		bool bad = false;
		tkv_array arr;
		if (!group_array(obj, key_name, GP_REINF_REC_SIZE, &arr, &bad))
		{
			if (bad)
			{
				gp_chunk_reinf_free(chunk);
				return false;
			}
			continue;
		}

		for (u32 i = 0; i < arr.array_length; i++)
		{
			const u8 *slot = arr.bytes + (size_t)i * GP_REINF_REC_SIZE;
			u32 index = get_u32(slot);
			if (index >= GP_MAX_BLOCK_INDEX)
				continue;

			u32 live_before = chunk->reinf.count;
			u8 *rec = gp_sparse_get(&chunk->reinf, index, true);
			memcpy(rec, slot + sizeof(u32), GP_SPARSE_REINF_VALUE_SIZE);

			// the chunk counters live outside the sparse map and are normally
			// maintained by gp_chunk_reinf_add, so track them here
			if (chunk->reinf.count > live_before)
				chunk->reinf_entries++;
		}
	}

	for (u32 group = 0; group * GP_RECORDS_PER_GROUP < nsd; group++)
	{
		char key_name[8];
		snprintf(key_name, sizeof(key_name), "s%u", group);

		bool bad = false;
		tkv_array arr;
		if (!group_array(obj, key_name, GP_SHIELD_REC_SIZE, &arr, &bad))
		{
			if (bad)
			{
				gp_chunk_reinf_free(chunk);
				return false;
			}
			continue;
		}

		for (u32 i = 0; i < arr.array_length; i++)
		{
			const u8 *slot = arr.bytes + (size_t)i * GP_SHIELD_REC_SIZE;
			u32 index = get_u32(slot);
			if (index >= GP_MAX_BLOCK_INDEX)
				continue;

			u32 live_before = chunk->shield.count;
			u8 *rec = gp_sparse_get(&chunk->shield, index, true);
			if (!rec)
				continue;

			put_u32(rec, get_u32(slot + sizeof(u32)));
			put_u32(rec + sizeof(u32), get_u32(slot + sizeof(u32) * 2));

			if (chunk->shield.count > live_before)
				chunk->shield_entries++;
		}
	}

	return true;
}

u8 gp_chunk_write_stream(const gp_chunk_reinf *chunk, stream_t *s)
{
	if (!chunk || !s)
		return FAIL;

	arena *tmp = arena_create(8192);
	if (!tmp)
		return FAIL;

	tkv_object obj = gp_chunk_pack(chunk, tmp);
	u8 result = write_framed(obj, s);

	arena_destroy(tmp);
	return result;
}

bool gp_chunk_read_stream(stream_t *s, gp_chunk_reinf *chunk, arena *scratch, arena *out)
{
	u32 len = 0;
	tkv_object obj = read_framed(s, scratch, out, &len);
	if (!obj)
		return false;

	return gp_chunk_unpack(obj, chunk);
}

/* --- shield ----------------------------------------------------------------- */

tkv_object gp_shield_pack(const gp_shield *shield, arena *out)
{
	if (!shield || !out)
		return NULL;

	tkv_object obj = tkv_object_create_empty(out);
	if (!obj)
		return NULL;

	u32 ver = GP_FORMAT_VERSION;
	u32 ntarget = shield->target_count;
	u32 px = (u32)shield->pos.x, py = (u32)shield->pos.y, pz = (u32)shield->pos.z;
	bool powered = shield->powered, running = shield->running;

	struct
	{
		const char *key;
		u32 value;
	} scalars[] = {
		{"ver", ver},   {"n", ntarget},   {"px", px},           {"py", py},
		{"pz", pz},     {"pool", shield->pool}, {"rgw", shield->regen_per_window},
		{"win", shield->window},    {"rng", shield->range}, {"fps", shield->fuel_per_sec},
		{"cur", shield->cursor},            {"bud", shield->budget}, {"fuel", shield->fuel},
			};

	for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++)
	{
		GP_ADD(obj, scalars[i].key, TKV_VALUE_U32, &scalars[i].value, out);
		if (!obj)
			return NULL;
	}

	u64 ws = shield->window_start;
	if (!GP_ADD(obj, "ws", TKV_VALUE_U64, &ws, out))
		return NULL;
	if (!GP_ADD(obj, "pw", TKV_VALUE_BOOL, &powered, out))
		return NULL;
	if (!GP_ADD(obj, "run", TKV_VALUE_BOOL, &running, out))
		return NULL;

	if (ntarget * 3u > 0xFFFFu)
		return NULL;

	u8 *targets = NULL;
	if (ntarget)
	{
		targets = (u8 *)arena_alloc(out, (size_t)ntarget * 3 * sizeof(u32));
		for (u32 i = 0; i < ntarget; i++)
		{
			put_u32(targets + (size_t)i * 12 + 0, (u32)shield->targets[i].x);
			put_u32(targets + (size_t)i * 12 + 4, (u32)shield->targets[i].y);
			put_u32(targets + (size_t)i * 12 + 8, (u32)shield->targets[i].z);
		}
	}

	obj = add_array(obj, "tgt", sizeof(u32) * 3, (u16)(ntarget * 3), targets, out);
	if (!obj)
		return NULL;

	return obj;
}

bool gp_shield_unpack(tkv_object obj, gp_shield *shield)
{
	if (!obj || !shield)
		return false;

	/*
	 * gp_shield_unpack fills a caller-provided shield in place rather than
	 * zeroing it, so anything not present in the file is left as the caller left
	 * it. A restored shield must re-anchor its window clock on its next tick
	 * rather than trust whatever was in the struct, so clear it here.
	 */
	shield->started = false;

	u32 ver = 0, ntarget = 0;
	if (!get_u32_field(obj, "ver", &ver) || ver != GP_FORMAT_VERSION)
		return false;
	if (!get_u32_field(obj, "n", &ntarget))
		return false;
	if (ntarget > shield->target_capacity)
		return false;

	u32 px = 0, py = 0, pz = 0;
	get_u32_field(obj, "px", &px);
	get_u32_field(obj, "py", &py);
	get_u32_field(obj, "pz", &pz);

	shield->pos = (gp_pos){.x = (i32)px, .y = (i32)py, .z = (i32)pz};

	u32 value = 0;
	if (get_u32_field(obj, "pool", &value))
		shield->pool = value;
	if (get_u32_field(obj, "rgw", &value))
		shield->regen_per_window = value;
	if (get_u32_field(obj, "win", &value))
		shield->window = value;
	if (get_u32_field(obj, "rng", &value))
		shield->range = value;
	if (get_u32_field(obj, "fps", &value))
		shield->fuel_per_sec = value;
	if (get_u32_field(obj, "cur", &value))
		shield->cursor = value;
	if (get_u32_field(obj, "bud", &value))
		shield->budget = value;
	if (get_u32_field(obj, "fuel", &value))
		shield->fuel = value;

	tkv_value ws_val = tkv_get_value(obj, "ws");
	if (ws_val.meta.tkv_value_type == TKV_VALUE_U64)
		shield->window_start = *(u64 *)ws_val.ptr;

	tkv_value pw = tkv_get_value(obj, "pw");
	shield->powered = pw.meta.tkv_value_type == TKV_VALUE_BOOL && *(bool *)pw.ptr;
	tkv_value run = tkv_get_value(obj, "run");
	shield->running = run.meta.tkv_value_type == TKV_VALUE_BOOL && *(bool *)run.ptr;

	tkv_value tgt = tkv_get_value(obj, "tgt");
	if (tgt.meta.tkv_value_type == TKV_VALUE_ARR)
	{
		tkv_array arr = tkv_value_to_arr(tgt);
		if (arr.element_size == sizeof(u32) * 3)
		{
			u32 triples = arr.array_length / 3;
			if (triples > ntarget)
				ntarget = triples;
			if (ntarget > shield->target_capacity)
				return false;

			for (u32 i = 0; i < triples && i < shield->target_capacity; i++)
			{
				const u8 *p = arr.bytes + (size_t)i * 12;
				shield->targets[i] =
					(gp_pos){.x = (i32)get_u32(p), .y = (i32)get_u32(p + 4), .z = (i32)get_u32(p + 8)};
			}
			shield->target_count = triples;
		}
	}
	else
	{
		shield->target_count = 0;
	}

	return true;
}

/* --- whole world ------------------------------------------------------------ */

tkv_object gp_world_pack_header(const gp_world *world, arena *out)
{
	if (!world || !out)
		return NULL;

	tkv_object obj = tkv_object_create_empty(out);
	if (!obj)
		return NULL;

	u32 ver = GP_FORMAT_VERSION;
	u32 nchunks = 0, nshields = world->shield_count;

	for (u32 i = 0; i < world->chunk_count; i++)
		if (world->chunks[i].loaded)
			nchunks++;

	const gp_config *cfg = &world->cfg;
	struct
	{
		const char *key;
		u32 value;
	} scalars[] = {
		{"ver", ver},           {"nch", nchunks},          {"nsh", nshields},
		{"chh", cfg->chunk_height}, {"pool", cfg->shield_pool}, {"rgw", cfg->shield_regen},
		{"win", cfg->shield_window}, {"rng", cfg->shield_range},
		{"maxt", cfg->shield_max_targets},   {"fmax", cfg->shield_fuel_max},
		{"fps", cfg->shield_fuel_per_sec}, {"dint", cfg->decay_interval},
		{"damt", cfg->decay_amount},
	};

	for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++)
	{
		GP_ADD(obj, scalars[i].key, TKV_VALUE_U32, &scalars[i].value, out);
		if (!obj)
			return NULL;
	}

	return obj;
}

u8 gp_world_write_stream(const gp_world *world, stream_t *s)
{
	if (!world || !s)
		return FAIL;

	arena *meta = arena_create(4096);
	if (!meta)
		return FAIL;

	tkv_object header = gp_world_pack_header(world, meta);
	if (tkv_write_to_stream(header, s) != 0)
	{
		arena_destroy(meta);
		return FAIL;
	}

	arena_destroy(meta);

	arena *tmp = arena_create(16384);
	if (!tmp)
		return FAIL;

	for (u32 i = 0; i < world->chunk_count; i++)
	{
		if (!world->chunks[i].loaded)
			continue;

		/* Coordinates live outside the chunk blob so a chunk can be saved
		   on its own without repeating them. */
		i32 cx = world->chunks[i].cx, cz = world->chunks[i].cz;
		if (stream_write((const u8 *)&cx, sizeof(cx), s) != 0 ||
		    stream_write((const u8 *)&cz, sizeof(cz), s) != 0)
		{
			arena_destroy(tmp);
			return FAIL;
		}

		tkv_object obj = gp_chunk_pack(&world->chunks[i].data, tmp);
		if (write_framed(obj, s) != 0)
		{
			arena_destroy(tmp);
			return FAIL;
		}
	}

	for (u32 i = 0; i < world->shield_count; i++)
	{
		if (!world->shields[i])
			continue;

		tkv_object obj = gp_shield_pack(world->shields[i], tmp);
		if (write_framed(obj, s) != 0)
		{
			arena_destroy(tmp);
			return FAIL;
		}
	}

	arena_destroy(tmp);
	return 0;
}

i32 gp_world_unpack(tkv_object header, stream_t *s, gp_world *world, gp_shield **shield_pool,
                    u32 shield_pool_size, u32 shield_target_capacity, arena *scratch)
{
	if (!header || !s || !world)
		return -1;

	u32 ver = 0;
	if (!get_u32_field(header, "ver", &ver) || ver != GP_FORMAT_VERSION)
		return -1;

	u32 nchunks = 0, nshields = 0;
	get_u32_field(header, "nch", &nchunks);
	get_u32_field(header, "nsh", &nshields);

	gp_config cfg;
	gp_config_defaults(&cfg);

	u32 value = 0;
	if (get_u32_field(header, "chh", &value))
		cfg.chunk_height = value;
	if (get_u32_field(header, "pool", &value))
		cfg.shield_pool = value;
	if (get_u32_field(header, "rgw", &value))
		cfg.shield_regen = value;
	if (get_u32_field(header, "win", &value))
		cfg.shield_window = value;
	if (get_u32_field(header, "rng", &value))
		cfg.shield_range = value;
	if (get_u32_field(header, "maxt", &value))
		cfg.shield_max_targets = value;
	if (get_u32_field(header, "fmax", &value))
		cfg.shield_fuel_max = value;
	if (get_u32_field(header, "fps", &value))
		cfg.shield_fuel_per_sec = value;
	if (get_u32_field(header, "dint", &value))
		cfg.decay_interval = value;
	if (get_u32_field(header, "damt", &value))
		cfg.decay_amount = value;

	gp_world_set_config(world, &cfg);

	if (cfg.chunk_height == 0)
		return -1;

	for (u32 i = 0; i < nchunks; i++)
	{
		i32 cx = 0, cz = 0;
		if (stream_read(&cx, sizeof(cx), s) != 0 || stream_read(&cz, sizeof(cz), s) != 0)
			return -1;

		/* Each chunk gets a throwaway arena: the TKV tree is only needed until
		   gp_chunk_unpack has copied it into the sparse maps. */
		arena *tmp = arena_create(16384);
		if (!tmp)
			return -1;

		tkv_object obj = read_framed(s, scratch, tmp, NULL);
		if (!obj)
		{
			arena_destroy(tmp);
			return -1;
		}

		gp_chunk_reinf *chunk = gp_world_chunk(world, cx, cz);
		bool ok = chunk && gp_chunk_unpack(obj, chunk);
		arena_destroy(tmp);

		if (!ok)
			return -1;
	}

	i32 restored = 0;

	for (u32 i = 0; i < nshields; i++)
	{
		arena *tmp = arena_create(8192);
		if (!tmp)
			return -1;

		tkv_object obj = read_framed(s, scratch, tmp, NULL);
		if (!obj)
		{
			arena_destroy(tmp);
			return -1;
		}

		if (shield_pool && restored < (i32)shield_pool_size)
		{
			gp_shield *shield = gp_shield_create(&cfg, (gp_pos){0, 0, 0}, shield_target_capacity);
			if (shield && gp_shield_unpack(obj, shield) && gp_world_add_shield(world, shield))
			{
				shield_pool[restored++] = shield;
			}
			else if (shield)
			{
				gp_shield_destroy(shield);
				arena_destroy(tmp);
				return -1;
			}
		}

		arena_destroy(tmp);
	}

	return restored;
}

u64 gp_chunk_packed_size(const gp_chunk_reinf *chunk)
{
	if (!chunk)
		return 0;

	u64 reinf_groups = (chunk->reinf.count + GP_RECORDS_PER_GROUP - 1) / GP_RECORDS_PER_GROUP;
	u64 shield_groups = (chunk->shield.count + GP_RECORDS_PER_GROUP - 1) / GP_RECORDS_PER_GROUP;

	return sizeof(u32) + 5 * 16 /* metadata */ + reinf_groups * (12 + GP_RECORDS_PER_GROUP * GP_REINF_REC_SIZE) +
	       shield_groups * (12 + GP_RECORDS_PER_GROUP * GP_SHIELD_REC_SIZE);
}