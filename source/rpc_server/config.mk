# DCERPC Server subsystem

################################################
# Start SUBSYSTEM DCERPC_COMMON
[SUBSYSTEM::DCERPC_COMMON]
ADD_OBJ_FILES = \
		rpc_server/common/server_info.o \
		rpc_server/common/share_info.o
#
# End SUBSYSTEM DCERPC_COMMON
################################################

################################################
# Start MODULE dcerpc_dcom
[MODULE::dcerpc_dcom]
ENABLE = NO
INIT_FUNCTION = dcerpc_server_dcom_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/dcom/oxidresolver.o \
		rpc_server/dcom/rot.o \
		rpc_server/dcom/rodb.o \
		rpc_server/dcom/remact.o \
		librpc/gen_ndr/ndr_dcom_d.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON \
		DCOM \
# End MODULE dcerpc_dcom
################################################

################################################
# Start MODULE dcerpc_rpcecho
[MODULE::dcerpc_rpcecho]
INIT_FUNCTION = dcerpc_server_rpcecho_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/echo/rpc_echo.o
REQUIRED_SUBSYSTEMS = NDR_ECHO
# End MODULE dcerpc_rpcecho
################################################

################################################
# Start MODULE dcerpc_epmapper
[MODULE::dcerpc_epmapper]
INIT_FUNCTION = dcerpc_server_epmapper_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/epmapper/rpc_epmapper.o
REQUIRED_SUBSYSTEMS = NDR_EPMAPPER
# End MODULE dcerpc_epmapper
################################################

################################################
# Start MODULE dcerpc_remote
[MODULE::dcerpc_remote]
INIT_FUNCTION = dcerpc_server_remote_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/remote/dcesrv_remote.o
REQUIRED_SUBSYSTEMS = \
		LIBSMB NDR_ALL
# End MODULE dcerpc_remote
################################################

################################################
# Start MODULE dcerpc_srvsvc
[MODULE::dcerpc_srvsvc]
INIT_FUNCTION = dcerpc_server_srvsvc_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/srvsvc/dcesrv_srvsvc.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON NDR_SRVSVC
# End MODULE dcerpc_srvsvc
################################################

################################################
# Start MODULE dcerpc_wkssvc
[MODULE::dcerpc_wkssvc]
INIT_FUNCTION = dcerpc_server_wkssvc_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/wkssvc/dcesrv_wkssvc.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON NDR_WKSSVC
# End MODULE dcerpc_wkssvc
################################################

################################################
# Start MODULE dcerpc_unixinfo
[MODULE::dcerpc_unixinfo]
INIT_FUNCTION = dcerpc_server_unixinfo_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/unixinfo/dcesrv_unixinfo.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON \
		NDR_UNIXINFO
# End MODULE dcerpc_unixinfo
################################################

################################################
# Start MODULE dcerpc_samr
[MODULE::dcerpc_samr]
INIT_FUNCTION = dcerpc_server_samr_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/samr/dcesrv_samr.o
ADD_OBJ_FILES = \
		rpc_server/samr/samr_password.o
REQUIRED_SUBSYSTEMS = \
		SAMDB \
		DCERPC_COMMON \
		NDR_SAMR
# End MODULE dcerpc_samr
################################################

################################################
# Start MODULE dcerpc_winreg
[MODULE::dcerpc_winreg]
INIT_FUNCTION = dcerpc_server_winreg_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/winreg/rpc_winreg.o
REQUIRED_SUBSYSTEMS = \
		REGISTRY NDR_WINREG
# End MODULE dcerpc_winreg
################################################

################################################
# Start MODULE dcerpc_netlogon
[MODULE::dcerpc_netlogon]
INIT_FUNCTION = dcerpc_server_netlogon_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/netlogon/dcerpc_netlogon.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON \
		SCHANNELDB \
		NDR_NETLOGON
# End MODULE dcerpc_netlogon
################################################

################################################
# Start MODULE dcerpc_lsa
[MODULE::dcerpc_lsarpc]
INIT_FUNCTION = dcerpc_server_lsarpc_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/lsa/dcesrv_lsa.o
REQUIRED_SUBSYSTEMS = \
		SAMDB \
		DCERPC_COMMON \
		NDR_LSA
# End MODULE dcerpc_lsa
################################################

################################################
# Start MODULE dcerpc_spoolss
[MODULE::dcerpc_spoolss]
INIT_FUNCTION = dcerpc_server_spoolss_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/spoolss/dcesrv_spoolss.o \
		rpc_server/spoolss/spoolssdb.o
REQUIRED_SUBSYSTEMS = \
		DCERPC_COMMON \
		NDR_SPOOLSS
# End MODULE dcerpc_lsa
################################################

################################################
# Start MODULE dcerpc_drsuapi
[MODULE::dcerpc_drsuapi]
INIT_FUNCTION = dcerpc_server_drsuapi_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/drsuapi/dcesrv_drsuapi.o
ADD_OBJ_FILES = \
		rpc_server/drsuapi/drsuapi_cracknames.o
REQUIRED_SUBSYSTEMS = \
		SAMDB \
		DCERPC_COMMON \
		NDR_DRSUAPI
# End MODULE dcerpc_drsuapi
################################################

################################################
# Start MODULE dcerpc_dssetup
[MODULE::dcerpc_dssetup]
INIT_FUNCTION = dcerpc_server_dssetup_init
SUBSYSTEM = DCERPC
INIT_OBJ_FILES = \
		rpc_server/dssetup/dcesrv_dssetup.o
REQUIRED_SUBSYSTEMS = \
		SAMDB \
		DCERPC_COMMON \
		NDR_DSSETUP
# End MODULE dcerpc_dssetup
################################################

################################################
# Start SUBSYSTEM DCERPC
[SUBSYSTEM::DCERPC]
INIT_OBJ_FILES = \
		rpc_server/dcerpc_server.o
ADD_OBJ_FILES = \
		rpc_server/dcerpc_sock.o \
		rpc_server/dcesrv_auth.o \
		rpc_server/handles.o
REQUIRED_SUBSYSTEMS = \
		LIBCLI_AUTH \
		NDR
#
# End SUBSYSTEM DCERPC
################################################
