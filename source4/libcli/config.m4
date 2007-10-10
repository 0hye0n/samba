dnl # LIBCLI subsystem

LIBCLI_RAW_LIBS=
if test x"$with_ads_support" = x"yes"; then
	LIBCLI_RAW_LIBS="KRB5"
fi

SMB_SUBSYSTEM(LIBCLI_RAW,[],
		[libcli/raw/rawfile.o 
		libcli/raw/smb_signing.o
		libcli/raw/clisocket.o 
		libcli/raw/clitransport.o 
		libcli/raw/clisession.o 
		libcli/raw/clitree.o 
		libcli/raw/rawrequest.o 
		libcli/raw/rawreadwrite.o 
		libcli/raw/rawsearch.o 
		libcli/raw/rawsetfileinfo.o 
		libcli/raw/raweas.o 
		libcli/raw/rawtrans.o 
		libcli/raw/clioplock.o 
		libcli/raw/rawnegotiate.o 
		libcli/raw/rawfsinfo.o 
		libcli/raw/rawfileinfo.o 
		libcli/raw/rawnotify.o 
		libcli/raw/rawioctl.o 
		libcli/raw/rawacl.o 
		libcli/raw/rawdate.o],
		[${LIBCLI_RAW_LIBS}])

