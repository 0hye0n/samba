#######################
# Start LIBRARY EJSRPC
[SUBSYSTEM::EJSRPC]
OBJ_FILES = \
		ejsrpc.o
REQUIRED_SUBSYSTEMS = RPC_EJS
NOPROTO = YES
# End SUBSYSTEM EJSRPC
#######################

#######################
# Start LIBRARY SMBCALLS
[SUBSYSTEM::SMBCALLS]
OBJ_FILES = \
		smbcalls.o \
		smbcalls_config.o \
		smbcalls_ldb.o \
		smbcalls_nbt.o \
		smbcalls_cli.o \
		smbcalls_rpc.o \
		smbcalls_auth.o \
		smbcalls_options.o \
		smbcalls_nss.o \
		smbcalls_string.o \
		smbcalls_data.o \
		smbcalls_rand.o \
		smbcalls_sys.o \
		smbcalls_creds.o \
		smbcalls_samba3.o \
		smbcalls_param.o \
		ejsnet.o \
		mprutil.o
REQUIRED_SUBSYSTEMS = AUTH EJS LIBBASIC EJSRPC MESSAGING LIBSAMBA3 LIBNET
# End SUBSYSTEM SMBCALLS
#######################

#######################
# Start BINARY SMBSCRIPT
[BINARY::smbscript]
INSTALLDIR = BINDIR
OBJ_FILES = \
		smbscript.o
REQUIRED_SUBSYSTEMS = EJS LIBBASIC SMBCALLS CONFIG LIBSMB LIBRPC LIBCMDLINE
# End BINARY SMBSCRIPT
#######################
