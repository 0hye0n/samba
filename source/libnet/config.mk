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
		libnet/libnet_user.o \
		libnet/userinfo.o
REQUIRED_SUBSYSTEMS = RPC_NDR_SAMR RPC_NDR_SRVSVC
# End SUBSYSTEM LIBNET
#################################
