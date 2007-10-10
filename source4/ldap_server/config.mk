# LDAP server subsystem

#######################
# Start SUBSYSTEM LDAP
[SUBSYSTEM::LDAP]
PRIVATE_PROTO_HEADER = proto.h
OBJ_FILES = \
		ldap_server.o \
		ldap_backend.o \
		ldap_bind.o
PUBLIC_DEPENDENCIES = \
		LIBCLI_LDAP SAMDB process_model
# End SUBSYSTEM SMB
#######################
