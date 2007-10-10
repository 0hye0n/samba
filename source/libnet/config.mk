#################################
# Start SUBSYSTEM LIBNET
[SUBSYSTEM::LIBNET]
INIT_OBJ_FILES = \
		libnet/libnet.o
ADD_OBJ_FILES = \
		libnet/libnet_passwd.o \
		libnet/libnet_time.o \
		libnet/libnet_rpc.o \
		libnet/libnet_join.o \
		libnet/libnet_vampire.o \
		libnet/libnet_samdump.o \
		libnet/libnet_samsync_ldb.o \
		libnet/libnet_user.o \
		libnet/libnet_share.o \
		libnet/libnet_lookup.o \
		libnet/userinfo.o \
		libnet/userman.o \
		libnet/domain.o 
REQUIRED_SUBSYSTEMS = RPC_NDR_SAMR RPC_NDR_LSA RPC_NDR_SRVSVC RPC_NDR_DRSUAPI LIBCLI_COMPOSITE LIBCLI_RESOLVE LIBSAMBA3
# End SUBSYSTEM LIBNET
#################################
