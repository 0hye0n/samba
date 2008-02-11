# Samba Build System					
# - the main program					
#							
#  Copyright (C) Stefan (metze) Metzmacher 2004	
#  Copyright (C) Jelmer Vernooij 2005
#  Released under the GNU GPL				

use smb_build::makefile;
use smb_build::header;
use smb_build::input;
use smb_build::config_mk;
use smb_build::output;
use smb_build::env;
use smb_build::cflags;
use smb_build::summary;
use smb_build::config;
use strict;

my $INPUT = {};
my $mkfile = smb_build::config_mk::run_config_mk($INPUT, $config::config{srcdir}, $config::config{builddir}, "main.mk");

my $subsys_output_type;
$subsys_output_type = ["STATIC_LIBRARY"];

my $library_output_type;
if ($config::config{USESHARED} eq "true") {
	$library_output_type = ["SHARED_LIBRARY", "STATIC_LIBRARY"];
} else {
	$library_output_type = ["STATIC_LIBRARY"];
	push (@$library_output_type, "SHARED_LIBRARY") if 
						($config::config{BLDSHARED} eq "true")
}

my $module_output_type;
if ($config::config{USESHARED} eq "true") {
	$module_output_type = ["SHARED_LIBRARY"];
} else {
	$module_output_type = ["INTEGRATED"];
}

my $DEPEND = smb_build::input::check($INPUT, \%config::enabled,
				     $subsys_output_type,
				     $library_output_type,
				     $module_output_type);
my $OUTPUT = output::create_output($DEPEND, \%config::config);
$config::config{SUBSYSTEM_OUTPUT_TYPE} = $subsys_output_type;
$config::config{LIBRARY_OUTPUT_TYPE} = $library_output_type;
$config::config{MODULE_OUTPUT_TYPE} = $module_output_type;
my $mkenv = new smb_build::makefile(\%config::config, $mkfile);

foreach my $key (values %$OUTPUT) {
	next unless defined $key->{OUTPUT_TYPE};

	$mkenv->Integrated($key) if grep(/INTEGRATED/, @{$key->{OUTPUT_TYPE}});
}

my $shared_libs_used = 0;

foreach my $key (values %$OUTPUT) {
	next unless defined $key->{OUTPUT_TYPE};

	$mkenv->StaticLibrary($key) if grep(/STATIC_LIBRARY/, @{$key->{OUTPUT_TYPE}});
	if (defined($key->{PC_FILE})) {
		push(@{$mkenv->{pc_files}}, "$key->{BASEDIR}/$key->{PC_FILE}");
	} 
	$mkenv->SharedLibrary($key) if ($key->{TYPE} eq "LIBRARY") and
					grep(/SHARED_LIBRARY/, @{$key->{OUTPUT_TYPE}});
	if ($key->{TYPE} eq "LIBRARY" and 
	    ${$key->{OUTPUT_TYPE}}[0] eq "SHARED_LIBRARY") {
		$shared_libs_used = 1;
	}
	$mkenv->SharedModule($key) if ($key->{TYPE} eq "MODULE" or 
								   $key->{TYPE} eq "PYTHON") and
					grep(/SHARED_LIBRARY/, @{$key->{OUTPUT_TYPE}});
	$mkenv->Binary($key) if grep(/BINARY/, @{$key->{OUTPUT_TYPE}});
	$mkenv->PythonFiles($key) if defined($key->{PYTHON_FILES});
	$mkenv->Manpage($key) if defined($key->{MANPAGE});
	$mkenv->Header($key) if defined($key->{PUBLIC_HEADERS});
	$mkenv->ProtoHeader($key) if defined($key->{PRIVATE_PROTO_HEADER}) or 
					 defined($key->{PUBLIC_PROTO_HEADER});
}

$mkenv->write("data.mk");
header::create_smb_build_h($OUTPUT, "include/build.h");

cflags::create_cflags($OUTPUT, $config::config{srcdir},
		    $config::config{builddir}, "extra_cflags.txt");

summary::show($OUTPUT, \%config::config);

if ($shared_libs_used) {
	print <<EOF;
To run binaries without installing, set the following environment variable:
	$config::config{LIB_PATH_VAR}=$config::config{builddir}/bin/shared
EOF
}

1;
