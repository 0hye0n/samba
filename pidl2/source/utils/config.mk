# utils subsystem

#################################
# Start BINARY ndrdump
[BINARY::ndrdump]
OBJ_FILES = \
		utils/ndrdump.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		NDR_ALL \
		LIBRPC
# FIXME: ndrdump shouldn't have to depend on RPC...
# End BINARY ndrdump
#################################

#################################
# Start BINARY ntlm_auth
[BINARY::ntlm_auth]
OBJ_FILES = \
		utils/ntlm_auth.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBSMB \
		LIBRPC
# End BINARY ntlm_auth
#################################

#################################
# Start BINARY getntacl
[BINARY::getntacl]
OBJ_FILES = \
		utils/getntacl.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBRPC \
		NDR_XATTR
# End BINARY getntacl
#################################

#################################
# Start BINARY setntacl
[BINARY::setntacl]
OBJ_FILES = \
		utils/setntacl.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBRPC
# End BINARY setntacl
#################################

#################################
# Start BINARY setnttoken
[BINARY::setnttoken]
OBJ_FILES = \
		utils/setnttoken.o
REQUIRED_SUBSYSTEMS = \
		CONFIG \
		LIBCMDLINE \
		LIBBASIC \
		LIBRPC \
		NDR_XATTR \
		NDR_SAMR
# End BINARY setnttoken
#################################

#################################
# Start BINARY nmblookup
[BINARY::nmblookup]
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
