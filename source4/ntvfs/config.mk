# NTVFS Server subsystem
include posix/config.mk
include unixuid/config.mk

################################################
# Start MODULE ntvfs_cifs
[MODULE::ntvfs_cifs]
INIT_FUNCTION = ntvfs_cifs_init 
SUBSYSTEM = NTVFS
INIT_OBJ_FILES = \
		cifs/vfs_cifs.o
REQUIRED_SUBSYSTEMS = \
		LIBCLI
# End MODULE ntvfs_cifs
################################################

################################################
# Start MODULE ntvfs_simple
[MODULE::ntvfs_simple]
INIT_FUNCTION = ntvfs_simple_init 
SUBSYSTEM = NTVFS
INIT_OBJ_FILES = \
		simple/vfs_simple.o
ADD_OBJ_FILES = \
		simple/svfs_util.o
# End MODULE ntvfs_cifs
################################################

################################################
# Start MODULE ntvfs_print
[MODULE::ntvfs_print]
INIT_FUNCTION = ntvfs_print_init 
SUBSYSTEM = NTVFS
INIT_OBJ_FILES = \
		print/vfs_print.o
# End MODULE ntvfs_print
################################################

################################################
# Start MODULE ntvfs_ipc
[MODULE::ntvfs_ipc]
SUBSYSTEM = NTVFS
INIT_FUNCTION = ntvfs_ipc_init 
INIT_OBJ_FILES = \
		ipc/vfs_ipc.o \
		ipc/ipc_rap.o \
		ipc/rap_server.o
# End MODULE ntvfs_ipc
################################################



################################################
# Start MODULE ntvfs_nbench
[MODULE::ntvfs_nbench]
SUBSYSTEM = NTVFS
INIT_FUNCTION = ntvfs_nbench_init 
INIT_OBJ_FILES = \
		nbench/vfs_nbench.o
# End MODULE ntvfs_nbench
################################################

################################################
# Start SUBSYSTEM ntvfs_common
[SUBSYSTEM::ntvfs_common]
ADD_OBJ_FILES = \
		common/brlock.o \
		common/opendb.o \
		common/sidmap.o
# End SUBSYSTEM ntvfs_common
################################################


################################################
# Start SUBSYSTEM NTVFS
[SUBSYSTEM::NTVFS]
INIT_OBJ_FILES = \
		ntvfs_base.o
ADD_OBJ_FILES = \
		ntvfs_generic.o \
		ntvfs_interface.o \
		ntvfs_util.o
#
# End SUBSYSTEM NTVFS
################################################
