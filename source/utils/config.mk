# utils subsystem

#################################
# Start BINARY ndrdump
[BINARY::ndrdump]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/ndrdump.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		NDR_ALL \
		RPC
MANPAGE = utils/man/ndrdump.1
# FIXME: ndrdump shouldn't have to depend on RPC...
# End BINARY ndrdump
#################################

#################################
# Start BINARY ntlm_auth
[BINARY::ntlm_auth]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/ntlm_auth.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBSMB \
		RPC
MANPAGE = utils/man/ntlm_auth.1
# End BINARY ntlm_auth
#################################

#################################
# Start BINARY getntacl
[BINARY::getntacl]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/getntacl.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		RPC \
		NDR_XATTR \
		EXT_LIB_XATTR
# End BINARY getntacl
#################################

#################################
# Start BINARY setntacl
[BINARY::setntacl]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/setntacl.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		RPC
# End BINARY setntacl
#################################

#################################
# Start BINARY setnttoken
[BINARY::setnttoken]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/setnttoken.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		RPC \
		NDR_XATTR \
		NDR_SAMR
# End BINARY setnttoken
#################################

#################################
# Start BINARY nmblookup
[BINARY::nmblookup]
INSTALLDIR = BINDIR
OBJ_FILES = \
		utils/nmblookup.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBCLI_NBT \
		LIB_SECURITY_NDR
# End BINARY nmblookup
#################################
