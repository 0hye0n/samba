# Directory Service subsystem

################################################
# Start MODULE libldb_samldb
[MODULE::libldb_samldb]
SUBSYSTEM = LIBLDB
INIT_OBJ_FILES = \
		dsdb/samdb/ldb_modules/samldb.o
# End MODULE libldb_samldb
################################################

################################################
# Start SUBSYSTEM SAMDB
[SUBSYSTEM::SAMDB]
INIT_OBJ_FILES = \
		dsdb/samdb/samdb.o
ADD_OBJ_FILES = \
		dsdb/samdb/samdb_privilege.o \
		dsdb/common/flag_mapping.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON \
		LIBLDB
#
# End SUBSYSTEM SAMDB
################################################
