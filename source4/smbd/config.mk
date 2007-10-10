# server subsystem

################################################
# Start MODULE server_service_auth
[MODULE::server_service_auth]
INIT_FUNCTION = server_service_auth_init
SUBSYSTEM = SERVER_SERVICE
REQUIRED_SUBSYSTEMS = \
		AUTH
# End MODULE server_auth
################################################

################################################
# Start MODULE server_service_smb
[MODULE::server_service_smb]
INIT_FUNCTION = server_service_smb_init
SUBSYSTEM = SERVER_SERVICE
REQUIRED_SUBSYSTEMS = \
		SMB
# End MODULE server_smb
################################################

################################################
# Start MODULE server_service_rpc
[MODULE::server_service_rpc]
INIT_FUNCTION = server_service_rpc_init
SUBSYSTEM = SERVER_SERVICE
REQUIRED_SUBSYSTEMS = \
		DCERPC
# End MODULE server_rpc
################################################

################################################
# Start MODULE server_service_ldap
[MODULE::server_service_ldap]
INIT_FUNCTION = server_service_ldap_init
SUBSYSTEM = SERVER_SERVICE
REQUIRED_SUBSYSTEMS = \
		LDAP
# End MODULE server_ldap
################################################

#######################
# Start SUBSYSTEM SERVICE
[SUBSYSTEM::SERVER_SERVICE]
INIT_OBJ_FILES = \
		smbd/service.o \
		smbd/service_stream.o \
		smbd/service_task.o
REQUIRED_SUBSYSTEMS = \
		MESSAGING
# End SUBSYSTEM SERVER
#######################

#################################
# Start BINARY smbd
[BINARY::smbd]
OBJ_FILES = \
		smbd/server.o
REQUIRED_SUBSYSTEMS = \
		PROCESS_MODEL \
		SERVER_SERVICE \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC
# End BINARY smbd
#################################
