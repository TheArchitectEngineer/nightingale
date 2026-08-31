#pragma once

#include <ng/sync.h>
#include <stddef.h>
#include <sys/cdefs.h>

struct handle_table {
	int capacity;
	void **entries;
	spinlock_t guard;
};

BEGIN_DECLS

int handle_alloc(struct handle_table *, void *);
void *handle_insert(struct handle_table *, int, void *);
void *handle_replace(struct handle_table *, int, void *);
void *handle_get(struct handle_table *, int);
void *handle_remove(struct handle_table *, int);

END_DECLS
