###################################################
# Samba4 parser generator for IDL structures
# Copyright tridge@samba.org 2000-2004
# Copyright jelmer@samba.org 2004
# released under the GNU GPL

package needed;

use strict;

# the list of needed functions
my %needed;

sub NeededFunction($)
{
	my $fn = shift;
	$needed{"pull_$fn->{NAME}"} = 1;
	$needed{"push_$fn->{NAME}"} = 1;
	foreach my $e (@{$fn->{DATA}}) {
		$e->{PARENT} = $fn;
		$needed{"pull_$e->{TYPE}"} = 1;
		$needed{"push_$e->{TYPE}"} = 1;
	}
}

sub NeededTypedef($)
{
	my $t = shift;
	if (util::has_property($t, "public")) {
		$needed{"pull_$t->{NAME}"} = 1;
		$needed{"push_$t->{NAME}"} = 1;		
	}

	if (util::has_property($t, "nopull")) {
		$needed{"pull_$t->{NAME}"} = 0;
	}
	if (util::has_property($t, "nopush")) {
		$needed{"push_$t->{NAME}"} = 0;
	}

	if ($t->{DATA}->{TYPE} eq "STRUCT" or $t->{DATA}->{TYPE} eq "UNION") {
		if (util::has_property($t, "gensize")) {
			$needed{"ndr_size_$t->{NAME}"} = 1;
		}

		for my $e (@{$t->{DATA}->{ELEMENTS}}) {
			$e->{PARENT} = $t->{DATA};
			if ($needed{"pull_$t->{NAME}"}) {
				$needed{"pull_$e->{TYPE}"} = 1;
			}
			if ($needed{"push_$t->{NAME}"}) {
				$needed{"push_$e->{TYPE}"} = 1;
			}
		}
	}
}

#####################################################################
# work out what parse functions are needed
sub BuildNeeded($)
{
	my($interface) = shift;
	my($data) = $interface->{DATA};
	foreach my $d (@{$data}) {
		($d->{TYPE} eq "FUNCTION") && 
		    NeededFunction($d);
	}
	foreach my $d (reverse @{$data}) {
		($d->{TYPE} eq "TYPEDEF") &&
		    NeededTypedef($d);
	}
}

sub is_needed($)
{
	my $name = shift;
	return $needed{$name};
}

1;
