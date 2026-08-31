#include <assert.h>
#include <limits.h>
#include <ng/handle_table.h>
#include <ng/sync.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool ht_ensure_capacity(struct handle_table *h, int need) {
	assert(need >= 0);

	int old_capacity = h->capacity;

	if (need < h->capacity)
		return true;

	int new_capacity = h->capacity * 2;
	if (need > new_capacity)
		new_capacity = need;
	if (new_capacity < 32)
		new_capacity = 32;

	void *new_entries = realloc(h->entries, new_capacity * sizeof(void *));
	if (!new_entries)
		return false;

	memset(new_entries + old_capacity, 0,
		(new_capacity - old_capacity) * sizeof(*new_entries));

	h->entries = new_entries;
	h->capacity = new_capacity;

	return true;
}

static int ht_get_unused(struct handle_table *h) {
	for (int i = 0; i < h->capacity; i++) {
		if (h->entries[i] == nullptr)
			return i;
	}
	return -1;
}

void handle_table_init(struct handle_table *h) {
	spin_init(&h->guard);
	spin_lock(&h->guard);

	h->capacity = 0;
	h->entries = nullptr;

	ht_ensure_capacity(h, 32);

	spin_unlock(&h->guard);
}

void handle_table_deinit(struct handle_table *h) {
	spin_lock(&h->guard);

	free(h->entries);
	h->capacity = 0;

	spin_lock(&h->guard); // tempted to just leave this locked forever
}

int handle_alloc(struct handle_table *h, void *p) {
	assert(p);
	spin_lock(&h->guard);

	int ix = ht_get_unused(h);
	if (ix < 0) {
		assert(ht_ensure_capacity(h, h->capacity + 1));
		ix = ht_get_unused(h);
		assert(ix >= 0);
	}

	h->entries[ix] = p;

	spin_unlock(&h->guard);
	return ix;
}

void *handle_insert(struct handle_table *h, int i, void *p) {
	assert(i >= 0);
	assert(p);
	spin_lock(&h->guard);

	assert(ht_ensure_capacity(h, i + 1));
	assert(h->entries[i] == nullptr);

	h->entries[i] = p;

	spin_unlock(&h->guard);
	return nullptr;
}

void *handle_replace(struct handle_table *h, int i, void *p) {
	assert(i >= 0);
	assert(p);
	spin_lock(&h->guard);

	assert(ht_ensure_capacity(h, i + 1));

	void *old = h->entries[i];
	h->entries[i] = p;

	spin_unlock(&h->guard);
	return old;
}

void *handle_get(struct handle_table *h, int i) {
	assert(i >= 0);
	spin_lock(&h->guard);

	void *res = nullptr;
	if (i < h->capacity) {
		res = h->entries[i];
	}

	spin_unlock(&h->guard);
	return res;
}

void *handle_remove(struct handle_table *h, int i) {
	assert(i >= 0);
	spin_lock(&h->guard);

	void *res = nullptr;
	if (i < h->capacity) {
		res = h->entries[i];
		h->entries[i] = nullptr;
	}

	spin_unlock(&h->guard);
	return res;
}
