###########################################################
### SMB Build System					###
### - create output for Makefile			###
###							###
###  Copyright (C) Stefan (metze) Metzmacher 2004	###
###  Released under the GNU GPL				###
###########################################################

sub _prepare_command_interpreters($)
{
	my $ctx = shift;
	my $output;

	$output = "
SHELL=/bin/sh
PERL=\@PERL\@
";
	return $output;
}

sub _prepare_path_vars($)
{
	my $ctx = shift;
	my $output;

	$output = "
prefix=\@prefix\@
exec_prefix=\@exec_prefix\@
VPATH=\@srcdir\@
srcdir=\@srcdir\@
builddir=\@builddir\@
";
	return $output;
}

sub _prepare_compiler_linker($)
{
	my $ctx = shift;
	my $output;

	$output = "
CC=\@CC\@
CC_FLAGS=-I./include -I./ -I./lib \@CFLAGS\@ \@CPPFLAGS\@

LD=\@CC\@
LD_FLAGS=\@LDFLAGS\@ \@CFLAGS\@

STLD=ar
STLD_FLAGS=-rc

SHLD=\@CC\@
SHLD_FLAGS=\@LDSHFLAGS\@ \@LDFLAGS\@
";
	return $output;
}

sub _prepare_default_rule($)
{
	my $ctx = shift;
	my $output;

	$output = "
default: all
";
	return $output;
}

sub _prepare_SUFFIXES($)
{
	my $ctx = shift;
	my $output;

	$output = "
.SUFFIXES:
.SUFFIXES: .c .o .h .gch .a .so
";
	return $output;
}

sub _prepare_IDL($)
{
	my $ctx = shift;
	my $output;

	$output = "
idl_full: build/pidl/idl.pm
	CPP=\"\@CPP\@\" script/build_idl.sh FULL

idl: build/pidl/idl.pm
	\@CPP=\"\@CPP\@\" script/build_idl.sh

basics: idl proto_exists
";
	return $output;
}

sub _prepare_dummy_MAKEDIR()
{
	my $ctx = shift;
	my $output;

	$output = "
MAKEDIR = || exec false; \\
	  if test -d \"\$\$dir\"; then :; else \\
	  echo mkdir \"\$\$dir\"; \\
	  mkdir -p \"\$\$dir\" >/dev/null 2>&1 || \\
	  test -d \"\$\$dir\" || \\
	  mkdir \"\$\$dir\" || \\
	  exec false; fi || exec false

bin/.dummy:
	\@if (: >> \$\@ || : > \$\@) >/dev/null 2>&1; then :; else \\
	  dir=bin \$(MAKEDIR); fi
	\@: >> \$\@ || : > \$\@
";
	return $output;
}

###########################################################
# This function creates a standard make rule which is using $(CC)
#
# $output = _prepare_std_CC_rule($srcext,$destext,$message,$comment)
#
# $srcext -	sourcefile extension
#
# $destext -	destinationfile extension
#
# $message -	logmessage which is echoed while running this rule
#
# $comment -	just a comment what this rule should do
#
# $output -		the resulting output buffer
sub _prepare_std_CC_rule($$$$)
{
	my $src = shift;
	my $dst = shift;
	my $message = shift;
	my $comment = shift;
	my $output;

	$output = "
###################################
# Start $comment
.$src.$dst:
	\@if (: >> \$\@ || : > \$\@) >/dev/null 2>&1; then rm -f \$\@; else \\
	 dir=`echo \$\@ | sed 's,/[^/]*\$\$,,;s,^\$\$,.,'` \$(MAKEDIR); fi
	\@echo $message \$*.$src
	\@\$(CC) \$(CC_FLAGS) -c \$< -o \$\@
\@BROKEN_CC\@	-mv `echo \$\@ | sed 's%^.*/%%g'` \$\@
#End $comment
###################################
";

	return $output;
}

sub array2oneperline($)
{
	my $array = shift;
	my $i;
	my $output = "";

	foreach my $str (@{$array}) {
		if (!defined($str)) {
			next;
		}

		$output .= " \\\n\t\t";
		$output .= $str;
	}

	return $output;
}

sub array2oneline($)
{
	my $array = shift;
	my $i;
	my $output = "";

	foreach my $str (@{$array}) {
		if (!defined($str)) {
			next;
		}

		$output .= $str;
		$output .= " ";
	}

	return $output;
}

###########################################################
# This function creates a object file
# and library list
#
# $output = _prepare_var_obj_and_lib_list($var, $var_ctx)
#
# $var_ctx -		the subsystem context
#
# $var_ctx->{NAME} 	-	the <var> name
# $var_ctx->{OBJ_LIST} 	-	the list of objectfiles which sould be linked to this <var>
# $var_ctx->{LIB_LIST} 	-	the list of libraries which sould be linked to this <var>
#
# $output -		the resulting output buffer
sub _prepare_var_obj_and_lib_list($$)
{
	my $var = shift;
	my $ctx = shift;
	my $tmpobjlist;
	my $tmpliblist;
	my $output;

	$tmpobjlist = array2oneperline($ctx->{OBJ_LIST});
	$tmpliblist = array2oneperline($ctx->{LIB_LIST});

	$output = "
###################################
# Start $var $ctx->{NAME} OBJ AND LIB LIST
$var\_$ctx->{NAME}_OBJS =$tmpobjlist
$var\_$ctx->{NAME}_LIBS =$tmpliblist
# End $var $ctx->{NAME} OBJ AND LIB LIST
###################################
";

	return $output;
}

###########################################################
# This function creates a object file
# and library list for a subsystem
#
# $output = _prepare_subsystem_obj_and_lib_list($subsystem_ctx)
#
# $subsystem_ctx -		the subsystem context
#
# $subsystem_ctx->{NAME} -	the subsystem name
# $subsystem_ctx->{OBJ_LIST} -	the list of objectfiles which sould be linked to this subsystem
# $subsystem_ctx->{LIB_LIST} -	the list of libraries which sould be linked to this subsystem
#
# $output -		the resulting output buffer
sub _prepare_subsystem_obj_and_lib_list($)
{
	my $ctx = shift;

	return _prepare_var_obj_and_lib_list("SUBSYSTEM",$ctx);
}

###########################################################
# This function creates a object file 
# and library list for a module
#
# $output = _prepare_module_obj_and_lib_list($module_ctx)
#
# $module_ctx -		the module context
#
# $module_ctx->{NAME} -		the module binary name
# $module_ctx->{OBJ_LIST} -	the list of objectfiles which sould be linked to this module
# $module_ctx->{LIB_LIST} -	the list of libraries which sould be linked to this module
#
# $output -		the resulting output buffer
sub _prepare_module_obj_and_lib_list($)
{
	my $ctx = shift;

	return _prepare_var_obj_and_lib_list("MODULE",$ctx);

}

###########################################################
# This function creates a make rule for linking a shared module
#
# $output = _prepare_shared_module_rule($module_ctx)
#
# $module_ctx -		the module context
#
# $module_ctx->{MODULE} -	the module binary name
# $module_ctx->{DEPEND_LIST} -	the list of rules on which this module depends
# $module_ctx->{LINK_LIST} -	the list of objectfiles and external libraries
#				which sould be linked to this module
# $module_ctx->{LINK_FLAGS} -	linker flags used by this module
#
# $output -		the resulting output buffer
sub _prepare_shared_module_rule($)
{
	my $ctx = shift;
	my $tmpdepend;
	my $tmplink;
	my $tmpflag;
	my $output;

	$tmpdepend = array2oneperline($ctx->{DEPEND_LIST});
	$tmplink = array2oneperline($ctx->{LINK_LIST});
	$tmpflag = array2oneperline($ctx->{LINK_FLAGS});

	$output = "
###################################
# Start Module $ctx->{MODULE}
MODULE_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
MODULE_$ctx->{NAME}_LINK_LIST =$tmplink
MODULE_$ctx->{NAME}_LINK_FLAGS =$tmpflag
#
bin/$ctx->{MODULE}: basics \$(MODULE_$ctx->{NAME}_DEPEND_LIST) bin/.dummy
	\@echo Linking \$\@
	\@\$(SHLD) \$(SHLD_FLAGS) -o \$\@ \\
		\$(MODULE_$ctx->{NAME}_LINK_FLAGS) \\
		\$(MODULE_$ctx->{NAME}_LINK_LIST)
# Module $ctx->{MODULE}
###################################
";

	return $output;
}

###########################################################
# This function creates a object file 
# and library list for a library
#
# $output = _prepare_library_obj_and_lib_list($library_ctx)
#
# $library_ctx -		the library context
#
# $library_ctx->{NAME} -	the library binary name
# $library_ctx->{OBJ_LIST} -	the list of objectfiles which sould be linked to this library
# $library_ctx->{LIB_LIST} -	the list of objectfiles which sould be linked to this library
#
# $output -		the resulting output buffer
sub _prepare_library_obj_and_lib_list($)
{
	my $ctx = shift;

	return _prepare_var_obj_and_lib_list("LIBRARY",$ctx);

}

###########################################################
# This function creates a make rule for linking a library
#
# $output = _prepare_library_rule($library_ctx)
#
# $library_ctx -		the library context
#
# $library_ctx->{LIBRARY} -		the library name
# $library_ctx->{STATIC_LIBRARY} -	the static library name
# $library_ctx->{SHARED_LIBRARY} -	the shared library name
# $library_ctx->{DEPEND_LIST} -		the list of rules on which this library depends
# $library_ctx->{STATIC_LINK_LIST} -	the list of objectfiles	which sould be linked
#					to this static library
# $library_ctx->{STATIC_LINK_FLAGS} -	linker flags used by this static library
# $library_ctx->{SHARED_LINK_LIST} -	the list of objectfiles and external libraries
#					which sould be linked to this shared library
# $library_ctx->{SHARED_LINK_FLAGS} -	linker flags used by this shared library
#
# $output -		the resulting output buffer
sub _prepare_library_rule($)
{
	my $ctx = shift;
	my $tmpdepend;
	my $tmpstlink;
	my $tmpstflag;
	my $tmpshlink;
	my $tmpshflag;
	my $output;

	$tmpdepend = array2oneperline($ctx->{DEPEND_LIST});

	$tmpstlink = array2oneperline($ctx->{STATIC_LINK_LIST});
	$tmpstflag = array2oneperline($ctx->{STATIC_LINK_FLAGS});

	$tmpshlink = array2oneperline($ctx->{SHARED_LINK_LIST});
	$tmpshflag = array2oneperline($ctx->{SHARED_LINK_FLAGS});

	$output = "
###################################
# Start Library $ctx->{LIBRARY}
#
LIBRARY_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
#
LIBRARY_$ctx->{NAME}_STATIC_LINK_LIST =$tmpstlink
LIBRARY_$ctx->{NAME}_STATIC_LINK_FLAGS =$tmpstflag
#
LIBRARY_$ctx->{NAME}_SHARED_LINK_LIST =$tmpshlink
LIBRARY_$ctx->{NAME}_SHARED_LINK_FLAGS =$tmpshflag
#
# Static $ctx->{STATIC_LIBRARY}
bin/$ctx->{STATIC_LIBRARY}: basics \$(LIBRARY_$ctx->{NAME}_DEPEND_LIST) bin/.dummy
	\@echo Linking \$\@
	\@\$(STLD) \$(STLD_FLAGS) \$\@ \\
		\$(LIBRARY_$ctx->{NAME}_STATIC_LINK_FLAGS) \\
		\$(LIBRARY_$ctx->{NAME}_STATIC_LINK_LIST)";

	if (defined($ctx->{SHARED_LIBRARY})) {
		$output .= "
# Shared $ctx->{SHARED_LIBRARY}
bin/$ctx->{SHARED_LIBRARY}: basics \$(LIBRARY_$ctx->{NAME}_DEPEND_LIST) bin/.dummy
	\@echo Linking \$\@
	\@\$(SHLD) \$(SHLD_FLAGS) -o \$\@ \\
		\$(LIBRARY_$ctx->{NAME}_SHARED_LINK_FLAGS) \\
		\$(LIBRARY_$ctx->{NAME}_SHARED_LINK_LIST)";
	}
$output .= "
# End Library $ctx->{LIBRARY}
###################################
";

	return $output;
}

###########################################################
# This function creates a object file 
# and library list for a binary
#
# $output = _prepare_binary_obj_and_lib_list($binary_ctx)
#
# $binary_ctx -		the binary context
#
# $binary_ctx->{NAME} -		the binary name
# $binary_ctx->{OBJ_LIST} -	the list of objectfiles which sould be linked to this binary
# $binary_ctx->{LIB_LIST} -	the list of libraries which sould be linked to this binary
#
# $output -		the resulting output buffer
sub _prepare_binary_obj_and_lib_list($)
{
	my $ctx = shift;

	return _prepare_var_obj_and_lib_list("BINARY",$ctx);

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
	my $tmpdepend;
	my $tmplink;
	my $tmpflag;
	my $output;

	$tmpdepend = array2oneperline($ctx->{DEPEND_LIST});
	$tmplink = array2oneperline($ctx->{LINK_LIST});
	$tmpflag = array2oneperline($ctx->{LINK_FLAGS});

	$output = "
###################################
# Start Binary $ctx->{BINARY}
#
BINARY_$ctx->{NAME}_DEPEND_LIST =$tmpdepend
BINARY_$ctx->{NAME}_LINK_LIST =$tmplink
BINARY_$ctx->{NAME}_LINK_FLAGS =$tmpflag
#
bin/$ctx->{BINARY}: basics \$(BINARY_$ctx->{NAME}_DEPEND_LIST) bin/.dummy
	\@echo Linking \$\@
	\@\$(LD) \$(LD_FLAGS) -o \$\@ \\
		\$(BINARY_$ctx->{NAME}_LINK_FLAGS) \\
		\$(BINARY_$ctx->{NAME}_LINK_LIST)
# End Binary $ctx->{BINARY}
###################################
";

	return $output;
}

###########################################################
# This function creates a object file list for make proto
#
# $output = _prepare_proto_obj_list($proto_ctx)
#
# $proto_ctx -		the proto context

# $proto_ctx->{OBJ_LIST} -	the list of objectfiles which sould be scanned by make proto
#
# $output -		the resulting output buffer
sub _prepare_proto_obj_list($)
{
	my $ctx = shift;
	my $tmplist;
	my $output;

	$tmplist = array2oneperline($ctx->{OBJ_LIST});

	$output = "
###################################
# Start PROTO OBJ LIST
PROTO_OBJS =$tmplist
# End PROTO OBJ LIST
###################################
";

	return $output;
}

sub _prepare_proto_rules()
{
	my $output = "";

	$output .= "
# Making this target will just make sure that the prototype files
# exist, not necessarily that they are up to date.  Since they're
# removed by 'make clean' this will always be run when you do anything
# afterwards.
proto_exists: \$(builddir)/include/proto.h \$(builddir)/include/build_env.h

delheaders:
	@/bin/rm -f \$(builddir)/include/proto.h include/build_env.h:

include/proto.h:
	\@cd \$(srcdir) && \$(SHELL) script/mkproto.sh \$(PERL) \\
	  -h _PROTO_H_ \$(builddir)/include/proto.h \\
	  \$(PROTO_OBJS)

include/build_env.h:
	\@echo Building include/build_env.h
	\@cd \$(srcdir) && \$(SHELL) script/build_env.sh \$(srcdir) \$(builddir) \$(CC) > \$(builddir)/include/build_env.h

# 'make headers' or 'make proto' calls a subshell because we need to
# make sure these commands are executed in sequence even for a
# parallel make.
headers: delheaders \$(builddir)/include/proto.h \$(builddir)/include/build_env.h

proto: idl headers 

proto_test:
	\@[ -f \$(builddir)/include/proto.h ] || \$(MAKE) proto

clean: delheaders
	-rm -f *.o */*.o */*/*.o */*/*/*.o bin/*
	-rm -rf librpc/gen_*

distclean: clean
	-rm -f bin/.dummy
	-rm -f include/config.h 
	-rm -f Makefile*
	-rm -f config.status
	-rm -f config.smb_build.*
	-rm -f config.log config.cache

realdistclean: distclean
	-rm -f *.bak *~ */*.bak */*~ */*/*.bak */*/*~ */*/*/*.bak */*/*/*~
	-rm -f include/config.h.in
	-rm -f lib/version.h
	-rm -f configure
";

	return $output;
}

sub _prepare_make_target($)
{
	my $ctx = shift;
	my $tmpdepend;
	my $output;

	$tmpdepend = array2oneperline($ctx->{DEPEND_LIST});

	$output = "
###################################
# Start Target $ctx->{TARGET}
$ctx->{TARGET}: $tmpdepend
# End Target $ctx->{TARGET}
###################################
";

	return $output;
}

sub _prepare_obj_and_lib_lists($)
{
	my $CTX = shift;
	my $output = "";

	foreach my $key (sort keys %{$CTX->{OUTPUT}{SUBSYSTEMS}}) {
		$output .= _prepare_subsystem_obj_and_lib_list(\%{$CTX->{OUTPUT}{SUBSYSTEMS}{$key}});
	}

	foreach my $key (sort keys %{$CTX->{OUTPUT}{SHARED_MODULES}}) {
		$output .= _prepare_module_obj_and_lib_list(\%{$CTX->{OUTPUT}{SHARED_MODULES}{$key}});
	}

	foreach my $key (sort keys %{$CTX->{OUTPUT}{LIBRARIES}}) {
		$output .= _prepare_library_obj_and_lib_list(\%{$CTX->{OUTPUT}{LIBRARIES}{$key}});
	}

	foreach my $key (sort keys %{$CTX->{OUTPUT}{BINARIES}}) {
		$output .= _prepare_binary_obj_and_lib_list(\%{$CTX->{OUTPUT}{BINARIES}{$key}});
	}

	$output .= _prepare_proto_obj_list(\%{$CTX->{OUTPUT}{PROTO}});

	return $output;
}

sub _prepare_depend_lists($)
{
	my $CTX = shift;
	my $output = "";

	return $output;
}

sub _prepare_rule_lists($)
{
	my $CTX = shift;
	my $output = "";

	foreach my $key (sort keys %{$CTX->{OUTPUT}{SHARED_MODULES}}) {
		$output .= _prepare_module_rule(\%{$CTX->{OUTPUT}{SHARED_MODULES}{$key}});
	}

	foreach my $key (sort keys %{$CTX->{OUTPUT}{LIBRARIES}}) {
		$output .= _prepare_library_rule(\%{$CTX->{OUTPUT}{LIBRARIES}{$key}});
	}

	foreach my $key (sort keys %{$CTX->{OUTPUT}{BINARIES}}) {
		$output .= _prepare_binary_rule(\%{$CTX->{OUTPUT}{BINARIES}{$key}});
	}

	my $idl_ctx;
	$output .= _prepare_IDL($idl_ctx);

	$output .= _prepare_proto_rules();

	return $output;
}

###########################################################
# This function prepares the output for Makefile
#
# $output = _prepare_makefile_in($SMB_BUILD_CTX)
#
# $SMB_BUILD_CTX -	the global SMB_BUILD context
#
# $output -		the resulting output buffer
sub _prepare_makefile_in($)
{
	my $CTX = shift;
	my $output;

	$output  = "########################################\n";
	$output .= "# Autogenerated by config.smb_build.pl #\n";
	$output .= "########################################\n";

	my $cmd_ctx;
	$output .= _prepare_command_interpreters($cmd_ctx);

	my $path_ctx;
	$output .= _prepare_path_vars($path_ctx);

	my $compiler_ctx;
	$output .= _prepare_compiler_linker($compiler_ctx);

	my $rules_ctx;
	$output .= _prepare_default_rule($rules_ctx);

	my $suffix_ctx;
	$output .= _prepare_SUFFIXES($suffix_ctx);

	$output .= _prepare_dummy_MAKEDIR();

	$output .= _prepare_std_CC_rule("c","o","Compiling","Rule for std objectfiles");
	$output .= _prepare_std_CC_rule("h","gch","Precompiling","Rule for precompiled headerfiles");

	$output .= _prepare_obj_and_lib_lists($CTX);

	$output .= _prepare_depend_lists($CTX);

	$output .= _prepare_rule_lists($CTX);

	$output .= _prepare_make_target(\%{$CTX->{OUTPUT}{TARGETS}{ALL}});

	return $output;
}

###########################################################
# This function creates Makefile.in from the SMB_BUILD 
# context
#
# create_makefile_in($SMB_BUILD_CTX)
#
# $SMB_BUILD_CTX -	the global SMB_BUILD context
#
# $output -		the resulting output buffer
sub create_makefile_in($)
{
	my $CTX = shift;
	my $output;

	$output = _prepare_makefile_in($CTX);

	open(MAKEFILE_IN,"> Makefile.in") || die ("Can't open Makefile.in\n");

	print MAKEFILE_IN $output;

	close(MAKEFILE_IN);

	print "config.smb_build.pl: creating Makefile.in\n";
	return;	
}
