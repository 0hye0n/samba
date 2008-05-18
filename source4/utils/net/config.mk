# $(utilssrcdir)/net subsystem

#################################
# Start BINARY net
[BINARY::net]
INSTALLDIR = BINDIR
PRIVATE_PROTO_HEADER = net_proto.h
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		LIBSAMBA-NET \
		LIBPOPT \
		POPT_SAMBA \
		POPT_CREDENTIALS
# End BINARY net
#################################

net_OBJ_FILES = $(addprefix $(utilssrcdir)/net/,  \
		net.o \
		net_password.o \
		net_time.o \
		net_join.o \
		net_vampire.o \
		net_user.o)

