#################################
# Start SUBSYSTEM LIBCLI_AUTH
[SUBSYSTEM::LIBCLI_AUTH]
ADD_OBJ_FILES = libcli/auth/credentials.o \
		libcli/auth/session.o \
		libcli/auth/smbencrypt.o 
REQUIRED_SUBSYSTEMS = \
		AUTH SCHANNELDB GENSEC
# End SUBSYSTEM LIBCLI_AUTH
#################################
