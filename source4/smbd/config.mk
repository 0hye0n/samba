# server subsystem

[SUBSYSTEM::service]
PRIVATE_PROTO_HEADER = service_proto.h
PRIVATE_DEPENDENCIES = \
		MESSAGING samba-socket

service_OBJ_FILES = $(addprefix $(smbdsrcdir)/, \
		service.o \
		service_stream.o \
		service_task.o)

[SUBSYSTEM::PIDFILE]
PRIVATE_PROTO_HEADER = pidfile.h

PIDFILE_OBJ_FILES = $(smbdsrcdir)/pidfile.o

#################################
# Start BINARY smbd
[BINARY::smbd]
INSTALLDIR = SBINDIR
PRIVATE_DEPENDENCIES = \
		process_model \
		service \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		POPT_SAMBA \
		PIDFILE \
		LIBPOPT \
		gensec \
		registry \
		ntptr \
		ntvfs \
		share \
		CLUSTER

smbd_OBJ_FILES = $(smbdsrcdir)/server.o

MANPAGES += $(smbdsrcdir)/smbd.8
# End BINARY smbd
#################################
