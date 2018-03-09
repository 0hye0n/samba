if [ -z "$TEST_VAR_DIR" ] ; then
	die "TEST_VAR_DIR unset"
fi

export SIMPLE_TESTS_VAR_DIR="${TEST_VAR_DIR}/simple"
# Don't remove old directory since state is retained between tests
mkdir -p "$SIMPLE_TESTS_VAR_DIR"

# onnode needs CTDB_BASE to be set when run in-tree
if [ -z "$CTDB_BASE" ] ; then
    export CTDB_BASE="$TEST_SUBDIR"
fi

if [ -n "$TEST_LOCAL_DAEMONS" ] ; then
    . "${TEST_SUBDIR}/scripts/local_daemons.bash"
fi
