#ifndef TDB2_TEST_LAYOUT_H
#define TDB2_TEST_LAYOUT_H
#include "private.h"

struct tdb_layout *new_tdb_layout(void);
void tdb_layout_add_freetable(struct tdb_layout *layout);
void tdb_layout_add_free(struct tdb_layout *layout, tdb_len_t len,
			 unsigned ftable);
void tdb_layout_add_used(struct tdb_layout *layout,
			 TDB_DATA key, TDB_DATA data,
			 tdb_len_t extra);
void tdb_layout_add_capability(struct tdb_layout *layout,
			       uint64_t type,
			       bool write_breaks,
			       bool check_breaks,
			       bool open_breaks,
			       tdb_len_t extra);

#if 0 /* FIXME: Allow allocation of subtables */
void tdb_layout_add_hashtable(struct tdb_layout *layout,
			      int htable_parent, /* -1 == toplevel */
			      unsigned int bucket,
			      tdb_len_t extra);
#endif
/* freefn is needed if we're using failtest_free. */
struct tdb_context *tdb_layout_get(struct tdb_layout *layout,
				   void (*freefn)(void *),
				   union tdb_attribute *attr);
void tdb_layout_write(struct tdb_layout *layout, void (*freefn)(void *),
		       union tdb_attribute *attr, const char *filename);

void tdb_layout_free(struct tdb_layout *layout);

enum layout_type {
	FREETABLE, FREE, DATA, HASHTABLE, CAPABILITY
};

/* Shared by all union members. */
struct tle_base {
	enum layout_type type;
	tdb_off_t off;
};

struct tle_freetable {
	struct tle_base base;
};

struct tle_free {
	struct tle_base base;
	tdb_len_t len;
	unsigned ftable_num;
};

struct tle_used {
	struct tle_base base;
	TDB_DATA key;
	TDB_DATA data;
	tdb_len_t extra;
};

struct tle_hashtable {
	struct tle_base base;
	int parent;
	unsigned int bucket;
	tdb_len_t extra;
};

struct tle_capability {
	struct tle_base base;
	uint64_t type;
	tdb_len_t extra;
};

union tdb_layout_elem {
	struct tle_base base;
	struct tle_freetable ftable;
	struct tle_free free;
	struct tle_used used;
	struct tle_hashtable hashtable;
	struct tle_capability capability;
};

struct tdb_layout {
	unsigned int num_elems;
	union tdb_layout_elem *elem;
};
#endif /* TDB2_TEST_LAYOUT_H */
