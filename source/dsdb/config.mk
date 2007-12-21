# Directory Service subsystem

include samdb/ldb_modules/config.mk

################################################
# Start SUBSYSTEM SAMDB
[SUBSYSTEM::SAMDB]
PUBLIC_PROTO_HEADER = samdb/samdb_proto.h
PUBLIC_HEADERS = samdb/samdb.h
PUBLIC_DEPENDENCIES = HEIMDAL_KRB5 
PRIVATE_DEPENDENCIES = LIBNDR NDR_MISC NDR_DRSUAPI NDR_DRSBLOBS NSS_WRAPPER \
					   auth_system_session LDAP_ENCODE LIBCLI_AUTH LIBNDR \
					   SAMDB_SCHEMA LDB_WRAP SAMDB_COMMON
OBJ_FILES = \
		samdb/samdb.o \
		samdb/samdb_privilege.o \
		samdb/cracknames.o \
		repl/replicated_objects.o

[SUBSYSTEM::SAMDB_COMMON]
PUBLIC_PROTO_HEADER = common/proto.h
PRIVATE_DEPENDENCIES = LIBLDB
OBJ_FILES = \
		common/sidmap.o \
		common/flag_mapping.o \
		common/util.o

[SUBSYSTEM::SAMDB_SCHEMA]
PUBLIC_PROTO_HEADER = schema/proto.h
PUBLIC_HEADERS = schema/schema.h
PRIVATE_DEPENDENCIES = SAMDB_COMMON NDR_DRSUAPI NDR_DRSBLOBS
OBJ_FILES = \
		schema/schema_init.o \
		schema/schema_syntax.o \
		schema/schema_constructed.o \

#######################
# Start SUBSYSTEM DREPL_SRV
[MODULE::DREPL_SRV]
INIT_FUNCTION = server_service_drepl_init
SUBSYSTEM = service
OBJ_FILES = \
		repl/drepl_service.o \
		repl/drepl_periodic.o \
		repl/drepl_partitions.o \
		repl/drepl_out_pull.o \
		repl/drepl_out_helpers.o
PRIVATE_PROTO_HEADER = repl/drepl_service_proto.h
PRIVATE_DEPENDENCIES = \
		SAMDB \
		process_model 
# End SUBSYSTEM DREPL_SRV
#######################
