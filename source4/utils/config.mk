# utils subsystem

#################################
# Start BINARY ntlm_auth
[BINARY::ntlm_auth]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		LIBPOPT \
		POPT_SAMBA \
		POPT_CREDENTIALS \
		gensec \
		LIBCLI_RESOLVE \
		auth \
		ntlm_check \
		MESSAGING \
		LIBEVENTS
# End BINARY ntlm_auth
#################################

ntlm_auth_OBJ_FILES = $(utilssrcdir)/ntlm_auth.o

MANPAGES += $(utilssrcdir)/man/ntlm_auth.1

#################################
# Start BINARY getntacl
[BINARY::getntacl]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		NDR_XATTR \
		WRAP_XATTR \
		LIBSAMBA-ERRORS

getntacl_OBJ_FILES = $(utilssrcdir)/getntacl.o

# End BINARY getntacl
#################################

MANPAGES += $(utilssrcdir)/man/getntacl.1

#################################
# Start BINARY setntacl
[BINARY::setntacl]
# disabled until rewritten
#INSTALLDIR = BINDIR
# End BINARY setntacl
#################################

setntacl_OBJ_FILES = $(utilssrcdir)/setntacl.o

#################################
# Start BINARY setnttoken
[BINARY::setnttoken]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES =
# End BINARY setnttoken
#################################

setnttoken_OBJ_FILES = $(utilssrcdir)/setnttoken.o

#################################
# Start BINARY nmblookup
[BINARY::nmblookup]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		LIBCLI_NBT \
		LIBPOPT \
		POPT_SAMBA \
		LIBNETIF \
		LIBCLI_RESOLVE
# End BINARY nmblookup
#################################

nmblookup_OBJ_FILES = $(utilssrcdir)/nmblookup.o

#################################
# Start BINARY testparm
[BINARY::testparm]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBSAMBA-HOSTCONFIG \
		LIBSAMBA-UTIL \
		LIBPOPT \
		samba-socket \
		POPT_SAMBA \
		LIBCLI_RESOLVE \
		CHARSET
# End BINARY testparm
#################################

testparm_OBJ_FILES = $(utilssrcdir)/testparm.o

################################################
# Start BINARY oLschema2ldif
[BINARY::oLschema2ldif]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBLDB_CMDLINE
# End BINARY oLschema2ldif
################################################


oLschema2ldif_OBJ_FILES = $(addprefix $(utilssrcdir)/, schema_convert.o oLschema2ldif.o)

MANPAGES += $(utilssrcdir)/man/oLschema2ldif.1

################################################
# Start BINARY  ad2oLschema
[BINARY::ad2oLschema]
INSTALLDIR = BINDIR
PRIVATE_DEPENDENCIES = \
		LIBLDB_CMDLINE SAMDB 
# End BINARY ad2oLschema
################################################

ad2oLschema_OBJ_FILES = $(addprefix $(utilssrcdir)/, schema_convert.o ad2oLschema.o)

MANPAGES += $(utilssrcdir)/man/ad2oLschema.1

