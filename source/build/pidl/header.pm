###################################################
# create C header files for an IDL structure
# Copyright tridge@samba.org 2000
# released under the GNU GPL
package IdlHeader;

use Data::Dumper;

my($res);

#####################################################################
# dump a properties list
sub DumpProperties($)
{
    my($props) = shift;
    foreach my $d (@{$props}) {
	if (ref($d) ne "HASH") {
	    $res .= "/* [$d] */ ";
	} else {
	    foreach my $k (keys %{$d}) {
		$res .= "/* [$k($d->{$k})] */ ";
	    }
	}
    }
}

#####################################################################
# dump a structure element
sub DumpElement($)
{
    my($element) = shift;
    (defined $element->{PROPERTIES}) && DumpProperties($element->{PROPERTIES});
    DumpType($element->{TYPE});
    $res .= " ";
    if ($element->{POINTERS}) {
	for (my($i)=0; $i < $element->{POINTERS}; $i++) {
	    $res .= "*";
	}
    }
    $res .= "$element->{NAME}";
    (defined $element->{ARRAY_LEN}) && ($res .= "[$element->{ARRAY_LEN}]");
}

#####################################################################
# dump a struct
sub DumpStruct($)
{
    my($struct) = shift;
    $res .= "struct {\n";
    if (defined $struct->{ELEMENTS}) {
	foreach my $e (@{$struct->{ELEMENTS}}) {
	    DumpElement($e);
	    $res .= ";\n";
	}
    }
    $res .= "}";
}


#####################################################################
# dump a union element
sub DumpUnionElement($)
{
    my($element) = shift;
    $res .= "/* [case($element->{CASE})] */ ";
    DumpElement($element->{DATA});
    $res .= ";\n";
}

#####################################################################
# dump a union
sub DumpUnion($)
{
    my($union) = shift;
    (defined $union->{PROPERTIES}) && DumpProperties($union->{PROPERTIES});
    $res .= "union {\n";
    foreach my $e (@{$union->{DATA}}) {
	DumpUnionElement($e);
    }
    $res .= "}";
}

#####################################################################
# dump a type
sub DumpType($)
{
    my($data) = shift;
    if (ref($data) eq "HASH") {
	($data->{TYPE} eq "STRUCT") &&
	    DumpStruct($data);
	($data->{TYPE} eq "UNION") &&
	    DumpUnion($data);
    } else {
	$res .= "$data";
    }
}

#####################################################################
# dump a typedef
sub DumpTypedef($)
{
    my($typedef) = shift;
    $res .= "typedef ";
    DumpType($typedef->{DATA});
    $res .= " $typedef->{NAME};\n\n";
}

#####################################################################
# dump the interface definitions
sub DumpInterface($)
{
    my($interface) = shift;
    my($data) = $interface->{DATA};
    foreach my $d (@{$data}) {
	($d->{TYPE} eq "TYPEDEF") &&
	    DumpTypedef($d);
    }
}


#####################################################################
# dump a parsed IDL structure back into an IDL file
sub Dump($)
{
    my($idl) = shift;
    $res = "/* Dumped by pidl */\n\n";
    foreach my $x (@{$idl}) {
	($x->{TYPE} eq "INTERFACE") && 
	    DumpInterface($x);
    }
    return $res;
}

1;
