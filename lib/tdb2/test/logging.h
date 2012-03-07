#ifndef TDB2_TEST_LOGGING_H
#define TDB2_TEST_LOGGING_H
#include "tdb2.h"
#include <stdbool.h>
#include <string.h>

extern bool suppress_logging;
extern const char *log_prefix;
extern unsigned tap_log_messages;
extern union tdb_attribute tap_log_attr;
extern char *log_last;

void tap_log_fn(struct tdb_context *tdb,
		enum tdb_log_level level,
		enum TDB_ERROR ecode,
		const char *message, void *priv);
#endif /* TDB2_TEST_LOGGING_H */
