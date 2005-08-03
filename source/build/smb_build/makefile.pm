###########################################################
### SMB Build System					###
### - create output for Makefile			###
###							###
###  Copyright (C) Stefan (metze) Metzmacher 2004	###
###  Copyright (C) Jelmer Vernooij 2005			###
###  Released under the GNU GPL				###
###########################################################

package makefile;
use config qw(%config);
use strict;

sub _prepare_path_vars()
{
	my $output;

	$output = << '__EOD__';
prefix = @prefix@
exec_prefix = @exec_prefix@
VPATH = @srcdir@
srcdir = @srcdir@
builddir = @builddir@

BASEDIR = @prefix@
BINDIR = @bindir@
SBINDIR = @sbindir@
LIBDIR = @libdir@
CONFIGDIR = @configdir@
localstatedir = @localstatedir@
SWATDIR = @swatdir@
VARDIR = @localstatedir@

# The permissions to give the executables
INSTALLPERMS = 0755

# set these to where to find various files
# These can be overridden by command line switches (see smbd(8))
# or in smb.conf (see smb.conf(5))
LOGFILEBASE = @logfilebase@
CONFIGFILE = $(CONFIGDIR)/smb.conf
LMHOSTSFILE = $(CONFIGDIR)/lmhosts
NCALRPCDIR = @localstatedir@/ncalrpc

# This is where smbpasswd et al go
PRIVATEDIR = @privatedir@
SMB_PASSWD_FILE = $(PRIVATEDIR)/smbpasswd

# the directory where lock files go
LOCKDIR = @lockdir@

# the directory where pid files go
PIDDIR = @piddir@

MANDIR = @mandir@

PATH_FLAGS = -DCONFIGFILE=\"$(CONFIGFILE)\"  -DSBINDIR=\"$(SBINDIR)\" \
	 -DBINDIR=\"$(BINDIR)\" -DLMHOSTSFILE=\"$(LMHOSTSFILE)\" \
	 -DLOCKDIR=\"$(LOCKDIR)\" -DPIDDIR=\"$(PIDDIR)\" -DLIBDIR=\"$(LIBDIR)\" \
	 -DLOGFILEBASE=\"$(LOGFILEBASE)\" -DSHLIBEXT=\"@SHLIBEXT@\" \
	 -DCONFIGDIR=\"$(CONFIGDIR)\" -DNCALRPCDIR=\"$(NCALRPCDIR)\" \
	 -DSWATDIR=\"$(SWATDIR)\" -DSMB_PASSWD_FILE=\"$(SMB_PASSWD_FILE)\" \
	 -DPRIVATE_DIR=\"$(PRIVATEDIR)\"
__EOD__

	return $output;
}

sub _prepare_compiler_linker()
{
	return << "__EOD__";
SHELL=$config{SHELL}
PERL=$config{PERL}
CC=$config{CC}
CFLAGS=-I\$(srcdir)/include -I\$(srcdir) -D_SAMBA_BUILD_ -DHAVE_CONFIG_H -I\$(srcdir)/lib $config{CFLAGS} $config{CPPFLAGS}

LD=$config{LD}
LD_FLAGS=$config{LDFLAGS} -Lbin

STLD=$config{AR}
STLD_FLAGS=-rc

SHLD=$config{CC}
SHLD_FLAGS=$config{LDSHFLAGS} -Lbin

XSLTPROC=$config{XSLTPROC}

LEX=$config{LEX}
YACC=$config{YACC}

__EOD__
}

sub _prepare_default_rule()
{
	return << '__EOD__';
default: all

__EOD__
}

sub _prepare_SUFFIXES()
{
	return << '__EOD__';
.SUFFIXES:
.SUFFIXES: .c .et .y .l .d .o .h .h.gch .a .so .1 .1.xml .3 .3.xml .5 .5.xml .7 .7.xml

__EOD__
}

sub _prepare_IDL()
{
	return << '__EOD__';
idl_full: build/pidl/Parse/Pidl/IDL.pm
	@CPP="$(CPP)" PERL="$(PERL)" script/build_idl.sh FULL @PIDL_ARGS@

idl: build/pidl/Parse/Pidl/IDL.pm
	@CPP="$(CPP)" PERL="$(PERL)" script/build_idl.sh PARTIAL @PIDL_ARGS@

build/pidl/Parse/Pidl/IDL.pm: build/pidl/idl.yp
	-yapp -s -m 'Parse::Pidl::IDL' -o build/pidl/Parse/Pidl/IDL.pm build/pidl/idl.yp 

smb_interfaces: build/pidl/smb_interfaces.pm
	$(PERL) -Ibuild/pidl script/build_smb_interfaces.pl \
		include/smb_interfaces.h

build/pidl/smb_interfaces.pm: build/pidl/smb_interfaces.yp
	-yapp -s -m 'smb_interfaces' -o build/pidl/smb_interfaces.pm build/pidl/smb_interfaces.yp 

pch: proto include/includes.h.gch

pch_clean:
	-rm -f include/includes.h.gch

basics: idl proto_exists HEIMDAL_EXTERNAL

test: @DEFAULT_TEST_TARGET@

test-swrap: all
	./script/tests/selftest.sh @selftest_prefix@/st all SOCKET_WRAPPER

test-noswrap: all
	./script/tests/selftest.sh @selftest_prefix@/st all

quicktest: all
	./script/tests/selftest.sh @selftest_prefix@/st quick SOCKET_WRAPPER

valgrindtest: all
	SMBD_VALGRIND="xterm -n smbd -e valgrind -q --db-attach=yes --num-callers=30" \
	./script/tests/selftest.sh @selftest_prefix@/st quick SOCKET_WRAPPER

__EOD__
}

sub _prepare_man_rule($)
{
	my $suffix = shift;

	return << "__EOD__";
.$suffix.xml.$suffix:
	\$(XSLTPROC) -o \$@ http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl \$<

__EOD__
}

sub _prepare_binaries($)
{
	my $ctx = shift;

	my @bbn_list = ();
	my @sbn_list = ();

	foreach (values %$ctx) {
		next unless defined $_->{OUTPUT_TYPE};
		next unless ($_->{OUTPUT_TYPE} eq "BINARY");

		push (@sbn_list, $_->{OUTPUT}) if ($_->{INSTALLDIR} eq "SBINDIR");
		push(@bbn_list, $_->{OUTPUT}) if ($_->{INSTALLDIR} eq "BINDIR");
	}

	my $bbn = array2oneperline(\@bbn_list);
	my $sbn = array2oneperline(\@sbn_list);
	return << "__EOD__";
BIN_PROGS = $bbn
SBIN_PROGS = $sbn

binaries: \$(BIN_PROGS) \$(SBIN_PROGS)

__EOD__
}

sub _prepare_manpages($)
{
	my $ctx = shift;

	my @mp_list = ();

	foreach (values %$ctx) {
		if (defined($_->{MANPAGE}) and $_->{MANPAGE} ne "") {
			push (@mp_list, $_->{MANPAGE});
		}
	}
	
	my $mp = array2oneperline(\@mp_list);
	return << "__EOD__";
MANPAGES = $mp

manpages: \$(MANPAGES)

__EOD__
}

sub _prepare_dummy_MAKEDIR()
{
	my $ctx = shift;

	return  << '__EOD__';
bin/.dummy:
	@: >> $@ || : > $@

dynconfig.o: dynconfig.c Makefile
	@echo Compiling $*.c
	@$(CC) $(CFLAGS) @PICFLAG@ $(PATH_FLAGS) -c $< -o $@
@BROKEN_CC@	-mv `echo $@ | sed 's%^.*/%%g'` $@

__EOD__
}

sub _prepare_et_rule()
{
	return << '__EOD__';

.et.c: 
	$(MAKE) bin/compile_et
	./bin/compile_et $<
	mv `basename $@` $@

__EOD__
}

sub _prepare_yacc_rule()
{
	return << '__EOD__';
.y.c:
	$(YACC) -d -o $@ $<	
	
__EOD__
}

sub _prepare_lex_rule()
{
	return << '__EOD__';
.l.c:
	$(LEX) -o $@ $<

__EOD__
}

sub _prepare_depend_CC_rule()
{
	return << '__EOD__';

.c.d:
	@echo "Generating dependencies for $<"
	@$(CC) -MM -MG -MT $(<:.c=.o) -MF $@ $(CFLAGS) $<

__EOD__
}

###########################################################
# This function creates a standard make rule which is using $(CC)
#
# $output = _prepare_std_CC_rule($srcext,$destext,$flags,$message,$comment)
#
# $srcext -	sourcefile extension
#
# $destext -	destinationfile extension
#
# $flags -	additional compiler flags
#
# $message -	logmessage which is echoed while running this rule
#
# $comment -	just a comment what this rule should do
#
# $output -		the resulting output buffer
sub _prepare_std_CC_rule($$$$$)
{
	my ($src,$dst,$flags,$message,$comment) = @_;
	my $flagsstr = "";
	my $output;

	$output = << "__EOD__";
# $comment
.$src.$dst:
	\@echo $message \$\*.$src
	\@\$(CC) `script/cflags.sh \$\@` \$(CFLAGS) $flags -c \$< -o \$\@
\@BROKEN_CC\@	-mv `echo \$\@ | sed 's%^.*/%%g'` \$\@

__EOD__

	return $output;
}

sub array2oneperline($)
{
	my $array = shift;
	my $output = "";

	foreach (@$array) {
		next unless defined($_);

		$output .= " \\\n\t\t$_";
	}

	return $output;
}

###########################################################
# This function creates a object file list
#
# $output = _prepare_var_obj_list($var, $var_ctx)
#
# $var_ctx -		the subsystem context
#
# $var_ctx->{NAME} 	-	the <var> name
# $var_ctx->{OBJ_LIST} 	-	the list of objectfiles which sould be linked to this <var>
#
# $output -		the resulting output buffer
sub _prepare_obj_list($$)
{
	my ($var,$ctx) = @_;

	my $tmplist = array2oneperline($ctx->{OBJ_LIST});

	return << "__EOD__";
# $var $ctx->{NAME} OBJ LIST
$var\_$ctx->{NAME}_OBJS =$tmplist

__EOD__
}

sub _prepare_cflags($$)
{
	my ($var,$ctx) = @_;

	my $tmplist = array2oneperline($ctx->{CFLAGS});

	return << "__EOD__";
$var\_$ctx->{NAME}_CFLAGS =$tmplist

__EOD__
}

###########################################################
# This function creates a make rule for linking a library
#
# $output = _prepare_shared_library_rule($library_ctx)
#
# $library_ctx -		the library context
#
# $library_ctx->{NAME} -		the library name
#
# $library_ctx->{DEPEND_LIST} -		the list of rules on which this library depends
#
# $library_ctx->{LIBRARY_NAME} -	the shared library name
# $library_ctx->{LIBRARY_REALNAME} -	the shared library real name
# $library_ctx->{LIBRARY_SONAME} - the shared library soname
# $library_ctx->{LINK_LIST} -	the list of objectfiles and external libraries
#					which sould be linked to this shared library
# $library_ctx->{LINK_FLAGS} -	linker flags used by this shared library
#
# $output -		the resulting output buffer
sub _prepare_shared_library_rule($)
{
	my $ctx = shift;
	my $output;

	my $tmpdepend = array2oneperline($ctx->{DEPEND_LIST});
	my $tmpshlink = array2oneperline($ctx->{LINK_LIST});
	my $tmpshflag = array2oneperline($ctx->{LINK_FLAGS});

	$output = << "__EOD__";
LIBRARY_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
#
LIBRARY_$ctx->{NAME}_SHARED_LINK_LIST =$tmpshlink
LIBRARY_$ctx->{NAME}_SHARED_LINK_FLAGS =$tmpshflag
#

$ctx->{TARGET}: \$(LIBRARY_$ctx->{NAME}_DEPEND_LIST) \$(LIBRARY_$ctx->{NAME}_OBJS) bin/.dummy
	\@echo Linking \$\@
	\@\$(SHLD) \$(SHLD_FLAGS) -o \$\@ \\
		\$(LIBRARY_$ctx->{NAME}_SHARED_LINK_FLAGS) \\
		\$(LIBRARY_$ctx->{NAME}_SHARED_LINK_LIST)

__EOD__

	if (defined($ctx->{LIBRARY_SONAME})) {
	    $output .= << "__EOD__";
# Symlink $ctx->{LIBRARY_SONAME}
bin/$ctx->{LIBRARY_SONAME}: bin/$ctx->{LIBRARY_REALNAME} bin/.dummy
	\@echo Symlink \$\@
	\@ln -sf $ctx->{LIBRARY_REALNAME} \$\@
# Symlink $ctx->{LIBRARY_NAME}
bin/$ctx->{LIBRARY_NAME}: bin/$ctx->{LIBRARY_SONAME} bin/.dummy
	\@echo Symlink \$\@
	\@ln -sf $ctx->{LIBRARY_SONAME} \$\@

__EOD__
	}

$output .= << "__EOD__";
library_$ctx->{NAME}: basics bin/lib$ctx->{LIBRARY_NAME}

__EOD__

	return $output;
}

sub _prepare_mergedobj_rule($)
{
	my $ctx = shift;

	return "" unless $ctx->{TARGET};

	my $output = "";

	my $tmpdepend = array2oneperline($ctx->{DEPEND_LIST});

	$output .= "$ctx->{TYPE}_$ctx->{NAME}_DEPEND_LIST = $tmpdepend\n";

	$output .= "$ctx->{TARGET}: \$($ctx->{TYPE}_$ctx->{NAME}_DEPEND_LIST) \$($ctx->{TYPE}_$ctx->{NAME}_OBJS)\n";

	$output .= "\t\@echo \"Pre-Linking $ctx->{TYPE} $ctx->{NAME}\"\n";
	$output .= "\t@\$(LD) -r \$($ctx->{TYPE}_$ctx->{NAME}_OBJS) -o $ctx->{TARGET}\n";
	$output .= "\n";

	return $output;
}

sub _prepare_objlist_rule($)
{
	my $ctx = shift;
	my $tmpdepend = array2oneperline($ctx->{DEPEND_LIST});

	return "" unless $ctx->{TARGET};

	my $output = "$ctx->{TYPE}_$ctx->{NAME}_DEPEND_LIST = $tmpdepend\n";
	$output .= "$ctx->{TARGET}: ";
	$output .= "\$($ctx->{TYPE}_$ctx->{NAME}_DEPEND_LIST) \$($ctx->{TYPE}_$ctx->{NAME}_OBJS)\n";
	$output .= "\t\@touch $ctx->{TARGET}\n";

	return $output;
}

###########################################################
# This function creates a make rule for linking a library
#
# $output = _prepare_static_library_rule($library_ctx)
#
# $library_ctx -		the library context
#
# $library_ctx->{NAME} -		the library name
#
# $library_ctx->{DEPEND_LIST} -		the list of rules on which this library depends
#
# $library_ctx->{LIBRARY_NAME} -	the static library name
# $library_ctx->{LINK_LIST} -	the list of objectfiles	which sould be linked
#					to this static library
# $library_ctx->{LINK_FLAGS} -	linker flags used by this static library
#
# $output -		the resulting output buffer
sub _prepare_static_library_rule($)
{
	my $ctx = shift;
	my $output;

	my $tmpdepend = array2oneperline($ctx->{DEPEND_LIST});
	my $tmpstlink = array2oneperline($ctx->{LINK_LIST});
	my $tmpstflag = array2oneperline($ctx->{LINK_FLAGS});

	$output = << "__EOD__";
LIBRARY_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
#
LIBRARY_$ctx->{NAME}_STATIC_LINK_LIST =$tmpstlink
#
$ctx->{TARGET}: \$(LIBRARY_$ctx->{NAME}_DEPEND_LIST) \$(LIBRARY_$ctx->{NAME}_OBJS) bin/.dummy
	\@echo Linking \$@
	\@\$(STLD) \$(STLD_FLAGS) \$@ \\
		\$(LIBRARY_$ctx->{NAME}_STATIC_LINK_LIST)

library_$ctx->{NAME}: basics $ctx->{TARGET}

__EOD__

	return $output;
}

###########################################################
# This function creates a make rule for linking a binary
#
# $output = _prepare_binary_rule($binary_ctx)
#
# $binary_ctx -		the binary context
#
# $binary_ctx->{NAME} -		the binary name
# $binary_ctx->{BINARY} -	the binary binary name
#
# $binary_ctx->{DEPEND_LIST} -	the list of rules on which this binary depends
# $binary_ctx->{LINK_LIST} -	the list of objectfiles and external libraries
#				which sould be linked to this binary
# $binary_ctx->{LINK_FLAGS} -	linker flags used by this binary
#
# $output -		the resulting output buffer
sub _prepare_binary_rule($)
{
	my $ctx = shift;

	my $tmpdepend = array2oneperline($ctx->{DEPEND_LIST});
	my $tmplink = array2oneperline($ctx->{LINK_LIST});
	my $tmpflag = array2oneperline($ctx->{LINK_FLAGS});

	my $output = << "__EOD__";
#
BINARY_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
BINARY_$ctx->{NAME}_LINK_LIST =$tmplink
BINARY_$ctx->{NAME}_LINK_FLAGS =$tmpflag
#
bin/$ctx->{BINARY}: bin/.dummy \$(BINARY_$ctx->{NAME}_DEPEND_LIST) \$(BINARY_$ctx->{NAME}_OBJS)
	\@echo Linking \$\@
	\@\$(CC) \$(LD_FLAGS) -o \$\@ \\
		\$\(BINARY_$ctx->{NAME}_LINK_FLAGS) \\
		\$\(BINARY_$ctx->{NAME}_LINK_LIST) \\
		\$\(BINARY_$ctx->{NAME}_LINK_FLAGS)
binary_$ctx->{BINARY}: basics bin/$ctx->{BINARY}

__EOD__

	return $output;
}

sub _prepare_custom_rule($)
{
	my $ctx = shift;
	return "
$ctx->{NAME}: bin/.TARGET_$ctx->{NAME}

bin/.TARGET_$ctx->{NAME}:
	$ctx->{CMD}
	touch bin/.TARGET_$ctx->{NAME}
";
}

sub _prepare_proto_rules()
{
	my $output = "";

	$output .= << '__EOD__';
# Making this target will just make sure that the prototype files
# exist, not necessarily that they are up to date.  Since they're
# removed by 'make clean' this will always be run when you do anything
# afterwards.
proto_exists: include/proto.h

delheaders: pch_clean
	-rm -f $(builddir)/include/proto.h

include/proto.h:
	@cd $(srcdir) && $(SHELL) script/mkproto.sh "$(PERL)" \
	  -h _PROTO_H_ $(builddir)/include/proto.h \
	  $(PROTO_PROTO_OBJS)

# 'make headers' or 'make proto' calls a subshell because we need to
# make sure these commands are executed in sequence even for a
# parallel make.
headers: delheaders proto_exists

proto: idl headers

proto_test:
	@[ -f $(builddir)/include/proto.h ] || $(MAKE) proto

clean: delheaders
	@echo Removing objects
	@-find . -name '*.o' -exec rm -f '{}' \;
	@echo Removing binaries
	@-rm -f bin/*
	@echo Removing dummy targets
	@-rm -f bin/.*_*
	@echo Removing generated files
	@-rm -rf librpc/gen_*
	@echo Removing generated ASN1 files
	@-find heimdal/lib/asn1 -name 'asn1_*.[xc]' -exec rm -f '{}' \;
	@-find heimdal/lib/gssapi -name 'asn1_*.[xc]' -exec rm -f '{}' \;
	@-find heimdal/lib/hdb -name 'asn1_*.[xc]' -exec rm -f '{}' \;

distclean: clean
	-rm -f bin/.dummy
	-rm -f include/config.h include/smb_build.h
	-rm -f Makefile*
	-rm -f config.status
	-rm -f config.log config.cache
	-rm -f samba4-deps.dot
	-rm -f config.pm config.mk
	-rm -f lib/registry/winregistry.pc
__EOD__

	if ($config{developer} eq "yes") {
		$output .= "\t@-rm -f \$(_ALL_OBJS_OBJS:.o=.d)\n";
	}

	$output .= << '__EOD__';

removebackup:
	-rm -f *.bak *~ */*.bak */*~ */*/*.bak */*/*~ */*/*/*.bak */*/*/*~

realdistclean: distclean removebackup
	-rm -f include/config.h.in
	-rm -f include/version.h
	-rm -f configure
	-rm -f $(MANPAGES)
__EOD__

	return $output;
}

sub _prepare_make_target($)
{
	my $ctx = shift;
	my $tmpdepend;
	my $output;

	$tmpdepend = array2oneperline($ctx->{DEPEND_LIST});

	return << "__EOD__";
$ctx->{TARGET}: basics $tmpdepend

__EOD__
}

sub _prepare_target_settings($)
{
	my $CTX = shift;
	my $output = "";

	foreach my $key (values %$CTX) {
		if (defined($key->{OBJ_LIST})) {
			$output .= _prepare_obj_list($key->{TYPE}, $key);
		}

		if (defined($key->{OBJ_LIST})) {
			$output .= _prepare_cflags($key->{TYPE}, $key);
		}
	}

	return $output;
}

sub _prepare_install_rules($)
{
	my $CTX = shift;
	my $output = "";

	$output .= << '__EOD__';

showlayout: 
	@echo "Samba will be installed into:"
	@echo "  basedir: $(BASEDIR)"
	@echo "  bindir:  $(BINDIR)"
	@echo "  sbindir: $(SBINDIR)"
	@echo "  libdir:  $(LIBDIR)"
	@echo "  vardir:  $(VARDIR)"
	@echo "  privatedir:  $(PRIVATEDIR)"
	@echo "  piddir:   $(PIDDIR)"
	@echo "  lockdir:  $(LOCKDIR)"
	@echo "  swatdir:  $(SWATDIR)"
	@echo "  mandir:   $(MANDIR)"

showflags:
	@echo "Samba will be compiled with flags:"
	@echo "  CFLAGS = $(CFLAGS)"
	@echo "  LD_FLAGS = $(LD_FLAGS)"
	@echo "  STLD_FLAGS = $(STLD_FLAGS)"
	@echo "  SHLD_FLAGS = $(SHLD_FLAGS)"

install: showlayout installbin installdat installswat

# DESTDIR is used here to prevent packagers wasting their time
# duplicating the Makefile. Remove it and you will have the privilege
# of packaging each samba release for multiple versions of multiple
# distributions and operating systems, or at least supplying patches
# to all the packaging files required for this, prior to committing
# the removal of DESTDIR. Do not remove it even though you think it
# is not used.

installdirs:
	@$(SHELL) $(srcdir)/script/installdirs.sh $(DESTDIR)$(BASEDIR) $(DESTDIR)$(BINDIR) $(DESTDIR)$(SBINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(VARDIR) $(DESTDIR)$(PRIVATEDIR) $(DESTDIR)$(PIDDIR) $(DESTDIR)$(LOCKDIR) $(DESTDIR)$(PRIVATEDIR)/tls

installbin: all installdirs
	@$(SHELL) $(srcdir)/script/installbin.sh $(INSTALLPERMS) $(DESTDIR)$(BASEDIR) $(DESTDIR)$(SBINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(VARDIR) $(SBIN_PROGS)
	@$(SHELL) $(srcdir)/script/installbin.sh $(INSTALLPERMS) $(DESTDIR)$(BASEDIR) $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(VARDIR) $(BIN_PROGS)

installdat: installdirs
	@$(SHELL) $(srcdir)/script/installdat.sh $(DESTDIR)$(LIBDIR) $(srcdir)

installswat: installdirs
	@$(SHELL) $(srcdir)/script/installswat.sh $(DESTDIR)$(SWATDIR) $(srcdir) $(DESTDIR)$(LIBDIR)

installman: installdirs
	@$(SHELL) $(srcdir)/script/installman.sh $(DESTDIR)$(MANDIR) $(MANPAGES)

uninstall: uninstallbin uninstallman

uninstallbin:
	@$(SHELL) $(srcdir)/script/uninstallbin.sh $(INSTALLPERMS) $(DESTDIR)$(BASEDIR) $(DESTDIR)$(SBINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(VARDIR) $(DESTDIR)$(SBIN_PROGS)
	@$(SHELL) $(srcdir)/script/uninstallbin.sh $(INSTALLPERMS) $(DESTDIR)$(BASEDIR) $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(VARDIR) $(DESTDIR)$(BIN_PROGS)

uninstallman:
	@$(SHELL) $(srcdir)/script/uninstallman.sh $(DESTDIR)$(MANDIR) $(MANPAGES)

# Swig extensions
swig: scripting/swig/_tdb.so scripting/swig/_dcerpc.so

scripting/swig/tdb_wrap.c: scripting/swig/tdb.i
	swig -python scripting/swig/tdb.i

scripting/swig/_tdb.so: scripting/swig/tdb_wrap.o $(LIBRARY_swig_tdb_DEPEND_LIST)
	$(SHLD) $(SHLD_FLAGS) -o scripting/swig/_tdb.so scripting/swig/tdb_wrap.o \
		$(LIBRARY_swig_tdb_SHARED_LINK_LIST) $(LIBRARY_swig_tdb_SHARED_LINK_FLAGS)

SWIG_INCLUDES = librpc/gen_ndr/samr.i librpc/gen_ndr/lsa.i librpc/gen_ndr/spoolss.i

scripting/swig/dcerpc_wrap.c: scripting/swig/dcerpc.i scripting/swig/samba.i scripting/swig/status_codes.i $(SWIG_INCLUDES)
	swig -python scripting/swig/dcerpc.i

scripting/swig/_dcerpc.so: scripting/swig/dcerpc_wrap.o $(LIBRARY_swig_dcerpc_DEPEND_LIST)
	$(SHLD) $(SHLD_FLAGS) -o scripting/swig/_dcerpc.so scripting/swig/dcerpc_wrap.o $(LIBRARY_swig_dcerpc_SHARED_LINK_LIST) $(LIBRARY_swig_dcerpc_SHARED_LINK_FLAGS)

swig_clean:
	-rm -f scripting/swig/_tdb.so scripting/swig/tdb.pyc \
		scripting/swig/tdb.py scripting/swig/tdb_wrap.c \
		scripting/swig/tdb_wrap.o

everything: all

etags:
	etags `find $(srcdir) -name "*.[ch]"`

ctags:
	ctags `find $(srcdir) -name "*.[ch]"`

__EOD__

	return $output;
}

sub _prepare_rule_lists($)
{
	my $depend = shift;
	my $output = "";

	foreach my $key (values %{$depend}) {
		next unless defined $key->{OUTPUT_TYPE};

		($output .= _prepare_mergedobj_rule($key)) if $key->{OUTPUT_TYPE} eq "MERGEDOBJ";
		($output .= _prepare_objlist_rule($key)) if $key->{OUTPUT_TYPE} eq "OBJLIST";
		($output .= _prepare_static_library_rule($key)) if $key->{OUTPUT_TYPE} eq "STATIC_LIBRARY";
		($output .= _prepare_shared_library_rule($key)) if $key->{OUTPUT_TYPE} eq "SHARED_LIBRARY";
		($output .= _prepare_binary_rule($key)) if $key->{OUTPUT_TYPE} eq "BINARY";
		($output .= _prepare_custom_rule($key) ) if $key->{TYPE} eq "TARGET";
	}

	$output .= _prepare_IDL();
	$output .= _prepare_proto_rules();
	$output .= _prepare_install_rules($depend);

	return $output;
}

###########################################################
# This function prepares the output for Makefile
#
# $output = _prepare_makefile_in($OUTPUT)
#
# $OUTPUT -	the global OUTPUT context
#
# $output -		the resulting output buffer
sub _prepare_makefile_in($)
{
	my ($CTX) = @_;
	my $output;

	$output  = "########################################\n";
	$output .= "# Autogenerated by config.smb_build.pl #\n";
	$output .= "########################################\n";
	$output .= "\n";

	$output .= _prepare_path_vars();
	$output .= _prepare_compiler_linker();
	$output .= _prepare_default_rule();
	$output .= _prepare_SUFFIXES();
	$output .= _prepare_dummy_MAKEDIR();
	$output .= _prepare_std_CC_rule("c","o",$config{PICFLAG},"Compiling","Rule for std objectfiles");
	$output .= _prepare_std_CC_rule("h","h.gch",$config{PICFLAG},"Precompiling","Rule for precompiled headerfiles");
	$output .= _prepare_lex_rule();
	$output .= _prepare_yacc_rule();
	$output .= _prepare_et_rule();

	$output .= _prepare_depend_CC_rule();
	
	$output .= _prepare_man_rule("1");
	$output .= _prepare_man_rule("3");
	$output .= _prepare_man_rule("5");
	$output .= _prepare_man_rule("7");
	$output .= _prepare_manpages($CTX);
	$output .= _prepare_binaries($CTX);
	$output .= _prepare_target_settings($CTX);
	$output .= _prepare_rule_lists($CTX);

	my @all = ();
	
	foreach my $part (values %{$CTX}) {
		push (@all, $part->{TARGET}) if defined ($part->{OUTPUT_TYPE}) and $part->{OUTPUT_TYPE} eq "BINARY";	
	}
	
	$output .= _prepare_make_target({ TARGET => "all", DEPEND_LIST => \@all });

	if ($config{developer} eq "yes") {
		$output .= <<__EOD__
#-include \$(_ALL_OBJS_OBJS:.o=.d)
IDL_FILES = \$(wildcard librpc/idl/*.idl)
\$(patsubst librpc/idl/%.idl,librpc/gen_ndr/ndr_%.c,\$(IDL_FILES)) \\
\$(patsubst librpc/idl/%.idl,librpc/gen_ndr/ndr_\%_c.c,\$(IDL_FILES)) \\
\$(patsubst librpc/idl/%.idl,librpc/gen_ndr/ndr_%.h,\$(IDL_FILES)): idl
__EOD__
	}

	return $output;
}

###########################################################
# This function creates Makefile.in from the OUTPUT 
# context
#
# create_makefile_in($OUTPUT)
#
# $OUTPUT	-	the global OUTPUT context
#
# $output -		the resulting output buffer
sub create_makefile_in($$)
{
	my ($CTX, $file) = @_;

	open(MAKEFILE_IN,">$file") || die ("Can't open $file\n");
	print MAKEFILE_IN _prepare_makefile_in($CTX);
	close(MAKEFILE_IN);

	print "config.smb_build.pl: creating $file\n";
	return;	
}

1;
