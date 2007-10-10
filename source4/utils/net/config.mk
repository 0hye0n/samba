# utils/net subsystem

#################################
# Start BINARY net
[BINARY::net]
OBJ_FILES = \
		utils/net/net.o \
		utils/net/net_password.o \
		utils/net/net_time.o \
		utils/net/net_join.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBSMB \
		LIBNET
# End BINARY net
#################################
