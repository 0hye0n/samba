# SMB Build System
# - the output generating functions
#
#  Copyright (C) Stefan (metze) Metzmacher 2004
#  Copyright (C) Jelmer Vernooij 2004
#  Released under the GNU GPL

package output;
use strict;

sub add_dir($$)
{
	my ($dir,$files) = @_;
	my @ret = ();

	$dir =~ s/^\.\///g;
	
	foreach (@$files) {
		$_ = "$dir/$_";
		s/([^\/\.]+)\/\.\.\///g;
		push (@ret, $_);
	}
	
	return @ret;
}

sub generate_mergedobj($)
{
	my $subsys = shift;

	$subsys->{OUTPUT} = $subsys->{TARGET} = "bin/subsystems/$subsys->{TYPE}_$subsys->{NAME}.o";
}

sub generate_objlist($)
{
	my $subsys = shift;

	$subsys->{TARGET} = "bin/.$subsys->{TYPE}_$subsys->{NAME}";
	$subsys->{OUTPUT} = "\$($subsys->{TYPE}_$subsys->{NAME}_OBJ_LIST)";
}

sub generate_shared_library($)
{
	my $lib = shift;
	my $link_name;
	my $lib_name;

	@{$lib->{DEPEND_LIST}} = ();
	@{$lib->{LINK_LIST}} = ("\$($lib->{TYPE}_$lib->{NAME}\_OBJ_LIST)");

	$link_name = lc($lib->{NAME});
	$lib_name = $link_name;

	if ($lib->{TYPE} eq "LIBRARY") {
		$link_name = $lib->{NAME};
		$link_name =~ s/^LIB//;
		$link_name = lc($link_name);
		$lib_name = "lib$link_name";
	}

	if (defined($lib->{LIBRARY_REALNAME})) {
		$lib->{BASEDIR} =~ s/^\.\///g;
		$lib->{LIBRARY_REALNAME} = "$lib->{LIBRARY_REALNAME}";
		$lib->{DEBUGDIR} = $lib->{RELEASEDIR} = $lib->{BASEDIR};
	} else {
		if ($lib->{TYPE} eq "MODULE") {
			$lib->{DEBUGDIR} = "bin/modules/$lib->{SUBSYSTEM}";
			$lib->{RELEASEDIR} = "bin/install/modules/$lib->{SUBSYSTEM}";
			$lib->{LIBRARY_REALNAME} = $link_name;
			$lib->{LIBRARY_REALNAME} =~ s/^$lib->{SUBSYSTEM}_//g;
			$lib->{LIBRARY_REALNAME}.= ".\$(SHLIBEXT)";
		} else {
			$lib->{DEBUGDIR} = "bin";
			$lib->{RELEASEDIR} = "bin/install";
			$lib->{LIBRARY_REALNAME} = "$lib_name.\$(SHLIBEXT)";
		}
	}

	if (defined($lib->{VERSION})) {
		$lib->{LIBRARY_SONAME} = $lib->{LIBRARY_REALNAME}.".$lib->{SO_VERSION}";
		$lib->{LIBRARY_REALNAME} = $lib->{LIBRARY_REALNAME}.".$lib->{VERSION}";
	} 
	
	$lib->{TARGET} = "$lib->{DEBUGDIR}/$lib->{LIBRARY_REALNAME}";
	$lib->{OUTPUT} = $lib->{TARGET};
}

sub generate_static_library($)
{
	my $lib = shift;
	my $link_name;

	@{$lib->{DEPEND_LIST}} = ();

	$link_name = $lib->{NAME};
	$link_name =~ s/^LIB//;

	$lib->{LIBRARY_NAME} = "lib".lc($link_name).".a";
	@{$lib->{LINK_LIST}} = ("\$($lib->{TYPE}_$lib->{NAME}\_OBJ_LIST)");
	@{$lib->{LINK_FLAGS}} = ();

	$lib->{TARGET} = "bin/$lib->{LIBRARY_NAME}";
	$lib->{OUTPUT} = "-l".lc($link_name);
}

sub generate_binary($)
{
	my $bin = shift;

	@{$bin->{DEPEND_LIST}} = ();
	@{$bin->{LINK_LIST}} = ("\$($bin->{TYPE}_$bin->{NAME}\_OBJ_LIST)");
	@{$bin->{LINK_FLAGS}} = ();

	$bin->{RELEASEDIR} = "bin/install";
	$bin->{DEBUGDIR} = "bin/";
	$bin->{TARGET} = $bin->{OUTPUT} = "$bin->{DEBUGDIR}/$bin->{NAME}";
	$bin->{BINARY} = $bin->{NAME};
}

sub create_output($$)
{
	my ($depend, $config) = @_;
	my $part;

	foreach $part (values %{$depend}) {
		next if not defined($part->{OUTPUT_TYPE});

		# Combine object lists
		push(@{$part->{OBJ_LIST}}, add_dir($part->{BASEDIR}, $part->{INIT_OBJ_FILES})) if defined($part->{INIT_OBJ_FILES});
		push(@{$part->{OBJ_LIST}}, add_dir($part->{BASEDIR}, $part->{ADD_OBJ_FILES})) if defined($part->{ADD_OBJ_FILES});
		push(@{$part->{OBJ_LIST}}, add_dir($part->{BASEDIR}, $part->{OBJ_FILES})) if defined($part->{OBJ_FILES});

		if ((not defined($part->{OBJ_LIST}) or 
			scalar(@{$part->{OBJ_LIST}}) == 0) and 
			$part->{OUTPUT_TYPE} eq "MERGEDOBJ") {
			$part->{OUTPUT_TYPE} = "OBJLIST";
		}

		generate_binary($part) if $part->{OUTPUT_TYPE} eq "BINARY";
		generate_mergedobj($part) if $part->{OUTPUT_TYPE} eq "MERGEDOBJ";
		generate_objlist($part) if $part->{OUTPUT_TYPE} eq "OBJLIST";
		generate_shared_library($part) if $part->{OUTPUT_TYPE} eq "SHARED_LIBRARY";
		generate_static_library($part) if $part->{OUTPUT_TYPE} eq "STATIC_LIBRARY";

	}

	foreach $part (values %{$depend}) {
		next if not defined($part->{OUTPUT_TYPE});

		# Always import the CFLAGS and CPPFLAGS of the unique dependencies
		foreach my $elem (values %{$part->{UNIQUE_DEPENDENCIES}}) {
			next if $elem == $part;

			push(@{$part->{PUBLIC_CFLAGS}}, @{$elem->{CPPFLAGS}}) if defined(@{$elem->{CPPFLAGS}});
			push(@{$part->{PUBLIC_CFLAGS}}, $elem->{CFLAGS}) if defined($elem->{CFLAGS});
			push(@{$part->{LINK_LIST}}, $elem->{OUTPUT}) if defined($elem->{OUTPUT});
			push(@{$part->{LINK_FLAGS}}, @{$elem->{LIBS}}) if defined($elem->{LIBS});
			push(@{$part->{LINK_FLAGS}},@{$elem->{LDFLAGS}}) if defined($elem->{LDFLAGS});
		    	push(@{$part->{DEPEND_LIST}}, $elem->{TARGET}) if defined($elem->{TARGET});
		}
	}

	foreach $part (values %{$depend}) {
		$part->{CFLAGS} .= " " . join(' ', @{$part->{PUBLIC_CFLAGS}}) if defined($part->{PUBLIC_CFLAGS});
		$part->{CFLAGS} .= " " . join(' ', @{$part->{CPPFLAGS}}) if defined($part->{CPPFLAGS});
		if (($part->{STANDARD_VISIBILITY} ne "default") and 
			($config->{visibility_attribute} eq "yes")) {
			$part->{CFLAGS} .=  " -fvisibility=$part->{STANDARD_VISIBILITY}";
		}
	}

	return $depend;
}

1;
