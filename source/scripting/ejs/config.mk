[SUBSYSTEM::EJSRPC]
OBJ_FILES = \
		ejsrpc.o

[MODULE::smbcalls_config]
OBJ_FILES = smbcalls_config.o
OUTPUT_TYPE = MERGED_OBJ
SUBSYSTEM = smbcalls
INIT_FUNCTION = smb_setup_ejs_config

[MODULE::smbcalls_ldb]
OBJ_FILES = smbcalls_ldb.o
OUTPUT_TYPE = MERGED_OBJ
SUBSYSTEM = smbcalls
INIT_FUNCTION = smb_setup_ejs_ldb
PRIVATE_DEPENDENCIES = LIBLDB SAMDB LIBNDR

[MODULE::smbcalls_reg]
OBJ_FILES = smbcalls_reg.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_reg
PRIVATE_DEPENDENCIES = registry SAMDB LIBNDR

[MODULE::smbcalls_nbt]
OBJ_FILES = smbcalls_nbt.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_nbt

[MODULE::smbcalls_rand]
OBJ_FILES = smbcalls_rand.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_random

[MODULE::smbcalls_nss]
OBJ_FILES = smbcalls_nss.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_nss
PRIVATE_DEPENDENCIES = NSS_WRAPPER

[MODULE::smbcalls_data]
OBJ_FILES = smbcalls_data.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_datablob

[MODULE::smbcalls_auth]
OBJ_FILES = smbcalls_auth.o
OUTPUT_TYPE = MERGED_OBJ
SUBSYSTEM = smbcalls
INIT_FUNCTION = smb_setup_ejs_auth
PRIVATE_DEPENDENCIES = auth

[MODULE::smbcalls_string]
OBJ_FILES = smbcalls_string.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_string

[MODULE::smbcalls_sys]
OBJ_FILES = smbcalls_sys.o
SUBSYSTEM = smbcalls
OUTPUT_TYPE = MERGED_OBJ
INIT_FUNCTION = smb_setup_ejs_system

mkinclude ejsnet/config.mk

[SUBSYSTEM::smbcalls]
PRIVATE_PROTO_HEADER = proto.h
OBJ_FILES = \
		smbcalls.o \
		smbcalls_cli.o \
		smbcalls_rpc.o \
		smbcalls_options.o \
		smbcalls_creds.o \
		smbcalls_param.o \
		mprutil.o \
		literal.o
PRIVATE_DEPENDENCIES = \
		EJS LIBSAMBA-UTIL \
		EJSRPC MESSAGING \
		LIBSAMBA-NET LIBCLI_SMB LIBPOPT \
		CREDENTIALS POPT_CREDENTIALS POPT_SAMBA \
		dcerpc \
		NDR_TABLE

#######################
# Start BINARY SMBSCRIPT
[BINARY::smbscript]
INSTALLDIR = BINDIR
OBJ_FILES = \
		smbscript.o
PRIVATE_DEPENDENCIES = EJS LIBSAMBA-UTIL smbcalls LIBSAMBA-CONFIG
# End BINARY SMBSCRIPT
#######################
