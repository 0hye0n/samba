#!/usr/bin/perl
# Plain text output for selftest
# Copyright (C) 2008 Jelmer Vernooij <jelmer@samba.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
package output::plain;
use Exporter;
@ISA = qw(Exporter);

use FindBin qw($RealBin);
use lib "$RealBin/..";

use strict;

sub new($$$$$$$) {
	my ($class, $summaryfile, $verbose, $immediate, $statistics, $totaltests) = @_;
	my $self = { 
		verbose => $verbose, 
		immediate => $immediate, 
		statistics => $statistics,
		start_time => undef,
		test_output => {},
		suitesfailed => [],
		suites_ok => 0,
		skips => {},
		summaryfile => $summaryfile,
		index => 0,
		totalsuites => $totaltests,
	};
	bless($self, $class);
}

sub report_time($$)
{
	my ($self, $time) = @_;
	unless ($self->{start_time}) {
		$self->{start_time} = $time;
	}
	$self->{last_time} = $time;
}

sub output_msg($$);

sub start_testsuite($$)
{
	my ($self, $name) = @_;

	$self->{index}++;
	$self->{NAME} = $name;
	$self->{START_TIME} = $self->{last_time};

	my $duration = $self->{START_TIME} - $self->{start_time};

	$self->{test_output}->{$name} = "" unless($self->{verbose});

	my $out = "";
	$out .= "[$self->{index}";
	if ($self->{totalsuites}) {
		$out .= "/$self->{totalsuites}";
	}
	$out.= " in ".$duration."s";
	$out .= sprintf(", %d errors", ($#{$self->{suitesfailed}}+1)) if ($#{$self->{suitesfailed}} > -1);
	$out .= "] $name"; 
	if ($self->{immediate}) {
		print "$out\n";
	} else {
		print "$out: ";
	}
}

sub output_msg($$)
{
	my ($self, $output) = @_;

	if ($self->{verbose}) {
		require FileHandle;
		print $output;
		STDOUT->flush();
	} else {
		$self->{test_output}->{$self->{NAME}} .= $output;
	}
}

sub control_msg($$)
{
	my ($self, $output) = @_;

	$self->output_msg($output);
}

sub end_testsuite($$$$$)
{
	my ($self, $name, $result, $unexpected, $reason) = @_;
	my $out = "";

	if ($unexpected) {
		if ($result eq "success" and not defined($reason)) {
			$reason = "Expected negative exit code, got positive exit code";
		} 
		$self->output_msg("ERROR: $reason\n");
		push (@{$self->{suitesfailed}}, $name);
	} else {
		$self->{suites_ok}++;
	}

	if ($unexpected and $self->{immediate} and not $self->{verbose}) {
		$out .= $self->{test_output}->{$name};
	}

	if (not $self->{immediate}) {
		if (not $unexpected) {
			$out .= " ok\n";
		} else {
			$out .= " " . uc($result) . "\n";
		}
	}

	print $out;
}

sub start_test($$$)
{
	my ($self, $parents, $testname) = @_;

	if ($#$parents == -1) {
		$self->start_testsuite($testname);
	}
}

sub end_test($$$$$)
{
	my ($self, $parents, $testname, $result, $unexpected, $reason) = @_;
	
	if ($#$parents == -1) {
		if ($result eq "skip") {
			$self->skip_testsuite($testname, $reason);
		} else {
			$self->end_testsuite($testname, $result, $unexpected, $reason);
		}
		return;
	}

	my $append = "";

	unless ($unexpected) {
		$self->{test_output}->{$self->{NAME}} = "";
		if (not $self->{immediate}) {
			if ($result eq "failure") { print "f"; }
			elsif ($result eq "skip") { print "s"; }
			elsif ($result eq "success") { print "."; }
			else { print "?($result)"; }
		}
		return;
	}

	my $fullname = join(".", @$parents).".$testname";

	$append = "UNEXPECTED($result): $testname ($fullname)\n";

	$self->{test_output}->{$self->{NAME}} .= $append;

	if ($self->{immediate} and not $self->{verbose}) {
		print $self->{test_output}->{$self->{NAME}};
		$self->{test_output}->{$self->{NAME}} = "";
	}

	if (not $self->{immediate}) {
		if ($result eq "error") { print "E"; } 
		elsif ($result eq "failure") { print "F"; }
		elsif ($result eq "success") { print "S"; }
		else { print "?"; }
	}
}

sub summary($)
{
	my ($self) = @_;

	open(SUMMARY, ">$self->{summaryfile}");

	if ($#{$self->{suitesfailed}} > -1) {
		print SUMMARY "= Failed tests =\n";

		foreach (@{$self->{suitesfailed}}) {
			print SUMMARY "== $_ ==\n";
			print SUMMARY $self->{test_output}->{$_}."\n\n";
		}

		print SUMMARY "\n";
	}

	if (not $self->{immediate} and not $self->{verbose}) {
		foreach (@{$self->{suitesfailed}}) {
			print "===============================================================================\n";
			print "FAIL: $_\n";
			print $self->{test_output}->{$_};
			print "\n";
		}
	}

	print SUMMARY "= Skipped tests =\n";
	foreach my $reason (keys %{$self->{skips}}) {
		print SUMMARY "$reason\n";
		foreach my $name (@{$self->{skips}->{$reason}}) {
			print SUMMARY "\t$name\n";
		}
		print SUMMARY "\n";
	}
	close(SUMMARY);

	print "\nA summary with detailed information can be found in:\n  $self->{summaryfile}\n";

	if ($#{$self->{suitesfailed}} == -1) {
		my $ok = $self->{statistics}->{TESTS_EXPECTED_OK} + 
				 $self->{statistics}->{TESTS_EXPECTED_FAIL};
		print "\nALL OK ($ok tests in $self->{suites_ok} testsuites)\n";
	} else {
		print "\nFAILED ($self->{statistics}->{TESTS_UNEXPECTED_FAIL} failures and $self->{statistics}->{TESTS_ERROR} errors in ". ($#{$self->{suitesfailed}}+1) ." testsuites)\n";
	}

}

sub skip_testsuite($$)
{
	my ($self, $name, $reason) = @_;

	push (@{$self->{skips}->{$reason}}, $name);

	if ($self->{totalsuites}) {
		$self->{totalsuites}--;
	}
}

1;
