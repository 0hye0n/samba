[SUBSYSTEM::LIBCLI_LDAP]
PUBLIC_DEPENDENCIES = LIBSAMBA-ERRORS LIBEVENTS LIBPACKET 
PRIVATE_DEPENDENCIES = LIBCLI_COMPOSITE samba-socket NDR_SAMR LIBTLS ASN1_UTIL \
					   LDAP_ENCODE LIBNDR LP_RESOLVE gensec

LIBCLI_LDAP_OBJ_FILES = $(addprefix $(libclisrcdir)/ldap/, \
					   ldap.o ldap_client.o ldap_bind.o \
					   ldap_msg.o ldap_ildap.o ldap_controls.o)


PUBLIC_HEADERS += $(libclisrcdir)/ldap/ldap.h $(libclisrcdir)/ldap/ldap_ndr.h

$(call proto_header_template,$(libclisrcdir)/ldap/ldap_proto.h,$(LIBCLI_LDAP_OBJ_FILES:.o=.c))

[SUBSYSTEM::LDAP_ENCODE]
# FIXME PRIVATE_DEPENDENCIES = LIBLDB

LDAP_ENCODE_OBJ_FILES = $(libclisrcdir)/ldap/ldap_ndr.o
