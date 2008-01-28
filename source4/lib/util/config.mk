[SUBSYSTEM::LIBSAMBA-UTIL]
#VERSION = 0.0.1
#SO_VERSION = 0
PUBLIC_HEADERS = util.h \
				 attr.h \
				 byteorder.h \
				 data_blob.h \
				 debug.h \
				 mutex.h \
				 safe_string.h \
				 time.h \
				 xfile.h
OBJ_FILES = xfile.o \
		debug.o \
		fault.o \
		signal.o \
		system.o \
		time.o \
		genrand.o \
		dprintf.o \
		util_str.o \
		util_strlist.o \
		util_file.o \
		data_blob.o \
		util.o \
		fsusage.o \
		ms_fnmatch.o \
		mutex.o \
		idtree.o \
		become_daemon.o
PUBLIC_DEPENDENCIES = \
		LIBTALLOC LIBCRYPTO \
		SOCKET_WRAPPER EXT_NSL \
		CHARSET EXECINFO

[SUBSYSTEM::ASN1_UTIL]
PUBLIC_PROTO_HEADER = asn1_proto.h
PUBLIC_HEADERS = asn1.h
OBJ_FILES = asn1.o

[SUBSYSTEM::UNIX_PRIVS]
PRIVATE_PROTO_HEADER = unix_privs.h
OBJ_FILES = unix_privs.o

################################################
# Start SUBSYSTEM WRAP_XATTR
[SUBSYSTEM::WRAP_XATTR]
PUBLIC_PROTO_HEADER = wrap_xattr.h
OBJ_FILES = \
		wrap_xattr.o
PUBLIC_DEPENDENCIES = XATTR
#
# End SUBSYSTEM WRAP_XATTR
################################################

[SUBSYSTEM::UTIL_TDB]
PUBLIC_PROTO_HEADER = util_tdb.h
OBJ_FILES = \
		util_tdb.o
PUBLIC_DEPENDENCIES = LIBTDB

[SUBSYSTEM::UTIL_LDB]
PUBLIC_PROTO_HEADER = util_ldb.h
OBJ_FILES = \
			util_ldb.o
PUBLIC_DEPENDENCIES = LIBLDB
