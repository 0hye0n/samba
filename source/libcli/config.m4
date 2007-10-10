dnl # LIBCLI subsystem

SMB_SUBSYSTEM(LIBCLI_RAW,[],
		[libcli/raw/rawfile.o libcli/raw/smb_signing.o  \
		libcli/raw/clisocket.o libcli/raw/clitransport.o \
		libcli/raw/clisession.o libcli/raw/clitree.o \
		libcli/raw/clikrb5.o libcli/raw/clispnego.o libcli/raw/rawrequest.o \
		libcli/raw/rawreadwrite.o libcli/raw/rawsearch.o \
		libcli/raw/rawsetfileinfo.o libcli/raw/raweas.o \
		libcli/raw/rawtrans.o libcli/raw/clioplock.o \
		libcli/raw/rawnegotiate.o libcli/raw/rawfsinfo.o \
		libcli/raw/rawfileinfo.o libcli/raw/rawnotify.o \
		libcli/raw/rawioctl.o libcli/raw/rawacl.o],
		libcli/raw/libcli_raw_public_proto.h)

SMB_SUBSYSTEM(LIBCLI_UTILS,[],
		[libcli/util/asn1.o \
		libcli/util/smberr.o \
		libcli/util/doserr.o libcli/util/errormap.o \
		libcli/util/pwd_cache.o libcli/util/clierror.o libcli/util/cliutil.o \
		libcli/util/nterr.o libcli/util/smbdes.o libcli/util/smbencrypt.o \
		libcli/util/dom_sid.o],
		libcli/util/libcli_utils_public_proto.h)

SMB_SUBSYSTEM(LIBCLI_AUTH,[],
		[libcli/auth/ntlmssp.o libcli/auth/ntlmssp_parse.o \
		libcli/auth/ntlmssp_sign.o libcli/auth/schannel.o \
		libcli/auth/credentials.o libcli/auth/session.o],
		libcli/auth/libcli_auth_public_proto.h)

SMB_SUBSYSTEM(LIBCLI_NMB,[],
		[libcli/unexpected.o libcli/namecache.o libcli/nmblib.o \
		libcli/namequery.o],
		libcli/libcli_nmb_public_proto.h)

SMB_SUBSYSTEM(LIBCLI,[],
		[\$(LIBCLI_RAW_OBJS) \$(LIBCLI_UTILS_OBJS) \$(LIBCLI_AUTH_OBJS) \$(LIBCLI_NMB_OBJS)],
		librpc/libcli_public_proto.h)
