#include "tdb2-source.h"
#include "tap-interface.h"
#include <stdlib.h>
#include "logging.h"

int main(int argc, char *argv[])
{
	struct tdb_context *tdb;
	union tdb_attribute incompat_hash_attr;

	incompat_hash_attr.base.attr = TDB_ATTRIBUTE_HASH;
	incompat_hash_attr.base.next = &tap_log_attr;
	incompat_hash_attr.hash.fn = tdb1_incompatible_hash;

	plan_tests(8);

	/* Old format (with zeroes in the hash magic fields) should
	 * open with any hash (since we don't know what hash they used). */
	tdb = tdb_open("test/old-nohash-le.tdb1", TDB_VERSION1, O_RDWR, 0,
		       &tap_log_attr);
	ok1(tdb);
	ok1(tdb_check(tdb, NULL, NULL) == TDB_SUCCESS);
	tdb_close(tdb);

	tdb = tdb_open("test/old-nohash-be.tdb1", TDB_VERSION1, O_RDWR, 0,
		       &tap_log_attr);
	ok1(tdb);
	ok1(tdb_check(tdb, NULL, NULL) == TDB_SUCCESS);
	tdb_close(tdb);

	tdb = tdb_open("test/old-nohash-le.tdb1", TDB_VERSION1, O_RDWR, 0,
		       &incompat_hash_attr);
	ok1(tdb);
	ok1(tdb_check(tdb, NULL, NULL) == TDB_SUCCESS);
	tdb_close(tdb);

	tdb = tdb_open("test/old-nohash-be.tdb1", TDB_VERSION1, O_RDWR, 0,
		       &incompat_hash_attr);
	ok1(tdb);
	ok1(tdb_check(tdb, NULL, NULL) == TDB_SUCCESS);
	tdb_close(tdb);

	return exit_status();
}
