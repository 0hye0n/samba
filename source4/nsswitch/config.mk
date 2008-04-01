[SUBSYSTEM::LIBWINBIND-CLIENT]
OBJ_FILES = wb_common.o
PRIVATE_DEPENDENCIES = SOCKET_WRAPPER

#################################
# Start BINARY nsstest
[BINARY::nsstest]
INSTALLDIR = BINDIR
OBJ_FILES = \
		nsstest.o
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-UTIL \
		LIBREPLACE_EXT \
		LIBSAMBA-HOSTCONFIG
# End BINARY nsstest
#################################

#################################
# Start BINARY wbinfo
[BINARY::wbinfo]
INSTALLDIR = BINDIR
OBJ_FILES = \
		wbinfo.o
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-UTIL \
		LIBREPLACE_EXT \
		LIBCLI_AUTH \
		LIBPOPT \
		POPT_SAMBA \
		LIBWINBIND-CLIENT
# End BINARY nsstest
#################################
