#!/usr/bin/perl

use File::Basename;

my $file = shift;
my $dirname = dirname($file);
my $basename = basename($file);

my $header = $file; $header =~ s/\.et$/.h/;
my $source = $file; $source =~ s/\.et$/.c/;
print "$source: $file bin/compile_et\n";
print "\t\@cd $dirname && ../../../bin/compile_et $basename\n\n";

print "$header: $source\n";
