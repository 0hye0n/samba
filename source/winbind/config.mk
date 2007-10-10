# server subsystem

################################################
# Start MODULE server_service_winbind
[MODULE::server_service_winbind]
INIT_FUNCTION = server_service_winbind_init
SUBSYSTEM = SERVER_SERVICE
INIT_OBJ_FILES = \
		wb_server.o \
		wb_samba3_protocol.o \
		wb_samba3_cmd.o \
		wb_init_domain.o \
		wb_sid2domain.o \
		wb_domain_request.o \
		wb_connect_lsa.o \
		wb_connect_sam.o \
		wb_cmd_lookupname.o \
		wb_cmd_lookupsid.o \
		wb_cmd_getdcname.o \
		wb_cmd_userdomgroups.o \
		wb_cmd_usersids.o \
		wb_cmd_list_trustdom.o \
		wb_pam_auth.o \
		wb_async_helpers.o
REQUIRED_SUBSYSTEMS = RPC_NDR_LSA
# End MODULE server_service_winbind
################################################
