# LIB BASIC subsystem

##############################
# Start SUBSYSTEM LIBREPLACE
[SUBSYSTEM::LIBREPLACE]
INIT_OBJ_FILES = lib/replace.o
ADD_OBJ_FILES = \
		lib/snprintf.o
# End SUBSYSTEM LIBREPLACE
##############################

##############################
# Start SUBSYSTEM LIBNETIF
[SUBSYSTEM::LIBNETIF]
INIT_OBJ_FILES = \
		lib/netif/interface.o
ADD_OBJ_FILES = \
		lib/netif/netif.o
# End SUBSYSTEM LIBNETIF
##############################

##############################
# Start SUBSYSTEM LIBCRYPTO
[SUBSYSTEM::LIBCRYPTO]
NOPROTO = YES
INIT_OBJ_FILES = \
		lib/crypto/crc32.o
ADD_OBJ_FILES = \
		lib/crypto/md5.o \
		lib/crypto/hmacmd5.o \
		lib/crypto/md4.o
# End SUBSYSTEM LIBCRYPTO
##############################

##############################
# Start SUBSYSTEM LIBBASIC
[SUBSYSTEM::LIBBASIC]
INIT_OBJ_FILES = lib/version.o
ADD_OBJ_FILES = \
		lib/debug.o \
		lib/fault.o \
		lib/getsmbpass.o \
		lib/pidfile.o \
		lib/signal.o \
		lib/system.o \
		lib/time.o \
		lib/genrand.o \
		lib/dprintf.o \
		lib/xfile.o \
		lib/util_str.o \
		lib/util_strlist.o \
		lib/util_unistr.o \
		lib/util_file.o \
		lib/data_blob.o \
		lib/util.o \
		lib/util_sock.o \
		lib/substitute.o \
		lib/fsusage.o \
		lib/ms_fnmatch.o \
		lib/select.o \
		lib/pam_errors.o \
		intl/lang_tdb.o \
		lib/mutex.o \
		lib/server_mutex.o \
		lib/idtree.o \
		lib/unix_privs.o \
		lib/db_wrap.o \
		lib/gencache.o
REQUIRED_SUBSYSTEMS = \
		LIBLDB CHARSET LIBREPLACE LIBNETIF LIBCRYPTO EXT_LIB_DL LIBTALLOC
# End SUBSYSTEM LIBBASIC
##############################
