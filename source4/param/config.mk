[LIBRARY::LIBSAMBA-CONFIG]
DESCRIPTION = Reading Samba configuration files
VERSION = 0.0.1
SO_VERSION = 0
OBJ_FILES = loadparm.o \
			params.o \
			generic.o \
			util.o \
			../lib/version.o
PUBLIC_DEPENDENCIES = LIBSAMBA-UTIL DYNCONFIG
PUBLIC_PROTO_HEADER = proto.h
PUBLIC_HEADERS = param.h
