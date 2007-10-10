# Environment class
#
# Samba Build Environment
#
# (C) 2005 Jelmer Vernooij <jelmer@samba.org>
#
# Published under the GNU GPL

package smb_build::env;
use smb_build::input;

use strict;

sub new($$)
{ 
	my ($name, $config) = @_;
	my $self = { };
	bless $self, $name;

	$self->{items} = {};
	$self->{info} = {};
	
	$self->_set_config($config);

	return $self;
}

sub _set_config($$)
{
	my ($self, $config) = @_;

	$self->{config} = $config;

	if (not defined($self->{config}->{srcdir})) {
		$self->{config}->{srcdir} = '.';
	}

	if (not defined($self->{config}->{builddir})) {
		$self->{config}->{builddir}  = '.';
	}

	if ($self->{config}->{prefix} eq "NONE") {
		$self->{config}->{prefix} = $self->{config}->{ac_default_prefix};
	}

	if ($self->{config}->{exec_prefix} eq "NONE") {
		$self->{config}->{exec_prefix} = $self->{config}->{prefix};
	}
	
	if ($self->{config}->{developer} eq "yes") {
		$self->{developer} = 1;
	} else {
		$self->{developer} = 0;
	}
}

sub PkgConfig($$$$$$$)
{
	my ($self,$path,$name,$libs,$cflags,$version,$desc) = @_;

	print __FILE__.": creating $path\n";

	open(OUT, ">$path") or die("Can't open $path: $!");

	print OUT <<"__EOF__";
prefix=$self->{config}->{prefix}
exec_prefix=$self->{config}->{exec_prefix}
libdir=$self->{config}->{libdir}
includedir=$self->{config}->{includedir}

__EOF__

	print OUT "Name: $name\n";
	if (defined($desc)) {
		print OUT "Description: $desc\n";
	}
	print OUT "Version: $version\n";
	print OUT "Libs: -L\${libdir} $libs\n";
	print OUT "Cflags: -I\${includedir} $cflags\n";

	close(OUT);
}

sub Import($$)
{
	my ($self,$items) = @_;

	foreach (keys %$items) {
		if (defined($self->{items})) {
			print "Warning: Importing $_ twice!\n";
		}
		$self->{items}->{$_} = $items->{$_};
	}
}

sub GetInfo($$)
{
	my ($self,$name) = @_;

	unless (defined($self->{info}->{$name})) 
	{
		$self->{info}->{$name} = $self->{items}->Build($self);
	}

	return $self->{info}->{$name};
}

1;
