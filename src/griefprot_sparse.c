#include "griefprot_sparse.h"
#include <stdlib.h>
#include <string.h>

static u32 hash_key(u32 key)
{
	return key * 2654435761u; // fibonacci hashing, spreads dense indices well
}

/* Returns the slot holding `key`, or the slot where it would be inserted. */
static u32 probe(const u32 *keys, u32 capacity, u32 key, bool *found)
{
	u32 mask = capacity - 1;
	u32 i = hash_key(key) & mask;

	for (u32 step = 0; step < capacity; step++)
	{
		u32 k = keys[i];
		if (k == GP_SPARSE_KEY_EMPTY)
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
	return capacity; // no empty slot left anywhere
}

static int grow(gp_sparse *m, u32 new_capacity)
{
	u32 *old_keys = m->keys;
	u8 *old_values = m->values;
	u32 old_capacity = m->capacity;
	u32 value_size = m->value_size;

	u32 *keys = (u32 *)malloc(sizeof(u32) * new_capacity);
	u8 *values = (u8 *)calloc((size_t)new_capacity, value_size);
	if (!keys || !values)
	{
		free(keys);
		free(values);
		return FAIL;
	}

	for (u32 i = 0; i < new_capacity; i++)
		keys[i] = GP_SPARSE_KEY_EMPTY;

	m->keys = keys;
	m->values = values;
	m->capacity = new_capacity;
	m->count = 0;
	m->dead = 0;

	for (u32 i = 0; i < old_capacity; i++)
	{
		if (old_keys[i] == GP_SPARSE_KEY_EMPTY || old_keys[i] == GP_SPARSE_KEY_DEAD)
			continue;

		bool found = false;
		u32 slot = probe(keys, new_capacity, old_keys[i], &found);
		keys[slot] = old_keys[i];
		memcpy(values + (size_t)slot * value_size, old_values + (size_t)i * value_size, value_size);
		m->count++;
	}

	free(old_keys);
	free(old_values);
	return SUCCESS;
}

void gp_sparse_init(gp_sparse *m, u32 value_size)
{
	m->keys = NULL;
	m->values = NULL;
	m->capacity = 0;
	m->value_size = value_size ? value_size : 1;
	m->count = 0;
	m->dead = 0;
}

int gp_sparse_reserve(gp_sparse *m, u32 min_capacity)
{
	if (m->capacity >= min_capacity)
		return SUCCESS;

	u32 capacity = m->capacity ? m->capacity : 16;
	while (capacity < min_capacity)
	{
		if (capacity > (1u << 30))
			return FAIL;
		capacity *= 2;
	}

	return grow(m, capacity);
}

void gp_sparse_free(gp_sparse *m)
{
	free(m->keys);
	free(m->values);
	m->keys = NULL;
	m->values = NULL;
	m->capacity = 0;
	m->count = 0;
	m->dead = 0;
}

u8 *gp_sparse_get(gp_sparse *m, u32 key, bool create)
{
	if (!m)
		return NULL;

	if (m->capacity == 0)
	{
		if (!create)
			return NULL;
		if (gp_sparse_reserve(m, 16) != SUCCESS)
			return NULL;
	}
	else if (create && (m->count + m->dead + 1) * 10 >= m->capacity * 7)
	{
		// keep the load factor, tombstones included, under 0.7
		if (gp_sparse_reserve(m, m->capacity * 2) != SUCCESS)
			return NULL;
	}

	bool found = false;
	u32 slot = probe(m->keys, m->capacity, key, &found);

	if (found)
		return m->values + (size_t)slot * m->value_size;

	if (!create || slot == m->capacity)
		return NULL;

	if (m->keys[slot] == GP_SPARSE_KEY_DEAD && m->dead > 0)
		m->dead--;

	m->keys[slot] = key;
	m->count++;
	return m->values + (size_t)slot * m->value_size;
}

void gp_sparse_remove(gp_sparse *m, u32 key)
{
	if (!m || m->capacity == 0)
		return;

	bool found = false;
	u32 slot = probe(m->keys, m->capacity, key, &found);

	if (!found || m->keys[slot] == GP_SPARSE_KEY_DEAD)
		return;

	m->keys[slot] = GP_SPARSE_KEY_DEAD;
	m->count--;
	m->dead++;

	if (m->dead > m->count && m->dead > 32)
		gp_sparse_compact(m);
}

void gp_sparse_compact(gp_sparse *m)
{
	if (!m || m->capacity == 0 || m->dead == 0)
		return;

	u32 needed = m->count ? m->count : 1;
	u32 capacity = 16;
	while (capacity * 7 <= needed * 10)
		capacity *= 2;

	if (capacity < m->capacity)
		capacity = m->capacity; // growing back instead of shrinking avoids churn

	grow(m, capacity);
}
