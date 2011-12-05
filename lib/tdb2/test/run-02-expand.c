#include <ccan/failtest/failtest_override.h>
#include "tdb2-source.h"
#include <ccan/tap/tap.h>
#include <ccan/failtest/failtest.h>
#include "logging.h"
#include "failtest_helper.h"

int main(int argc, char *argv[])
{
	unsigned int i;
	uint64_t val;
	struct tdb_context *tdb;
	int flags[] = { TDB_INTERNAL, TDB_DEFAULT, TDB_NOMMAP,
			TDB_INTERNAL|TDB_CONVERT, TDB_CONVERT,
			TDB_NOMMAP|TDB_CONVERT };

	plan_tests(sizeof(flags) / sizeof(flags[0]) * 11 + 1);

	failtest_init(argc, argv);
	failtest_hook = block_repeat_failures;
	failtest_exit_check = exit_check_log;

	for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
		failtest_suppress = true;
		tdb = tdb_open("run-expand.tdb", flags[i],
			       O_RDWR|O_CREAT|O_TRUNC, 0600, &tap_log_attr);
		if (!ok1(tdb))
			break;

		val = tdb->file->map_size;
		/* Need some hash lock for expand. */
		ok1(tdb_lock_hashes(tdb, 0, 1, F_WRLCK, TDB_LOCK_WAIT) == 0);
		failtest_suppress = false;
		if (!ok1(tdb_expand(tdb, 1) == 0)) {
			failtest_suppress = true;
			tdb_close(tdb);
			break;
		}
		failtest_suppress = true;

		ok1(tdb->file->map_size >= val + 1 * TDB_EXTENSION_FACTOR);
		ok1(tdb_unlock_hashes(tdb, 0, 1, F_WRLCK) == 0);
		ok1(tdb_check(tdb, NULL, NULL) == 0);

		val = tdb->file->map_size;
		ok1(tdb_lock_hashes(tdb, 0, 1, F_WRLCK, TDB_LOCK_WAIT) == 0);
		failtest_suppress = false;
		if (!ok1(tdb_expand(tdb, 1024) == 0)) {
			failtest_suppress = true;
			tdb_close(tdb);
			break;
		}
		failtest_suppress = true;
		ok1(tdb_unlock_hashes(tdb, 0, 1, F_WRLCK) == 0);
		ok1(tdb->file->map_size >= val + 1024 * TDB_EXTENSION_FACTOR);
		ok1(tdb_check(tdb, NULL, NULL) == 0);
		tdb_close(tdb);
	}

	ok1(tap_log_messages == 0);
	failtest_exit(exit_status());
}
