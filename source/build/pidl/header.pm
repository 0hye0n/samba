###################################################
# create C header files for an IDL structure
# Copyright tridge@samba.org 2000
# released under the GNU GPL

package IdlHeader;

use strict;
use needed;

my($res);
my($tab_depth);

sub tabs()
{
	for (my($i)=0; $i < $tab_depth; $i++) {
		$res .= "\t";
	}
}

#####################################################################
# parse a properties list
sub HeaderProperties($)
{
    my($props) = shift;

    return;

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
# parse a structure element
sub HeaderElement($)
{
    my($element) = shift;

    (defined $element->{PROPERTIES}) && HeaderProperties($element->{PROPERTIES});
    $res .= tabs();
    HeaderType($element, $element->{TYPE}, "");
    $res .= " ";
    if ($element->{POINTERS} && 
	$element->{TYPE} ne "string") {
	    my($n) = $element->{POINTERS};
	    for (my($i)=$n; $i > 0; $i--) {
		    $res .= "*";
	    }
    }
    if (defined $element->{ARRAY_LEN} && 
	!util::is_constant($element->{ARRAY_LEN}) &&
	!$element->{POINTERS}) {
	    # conformant arrays are ugly! I choose to implement them with
	    # pointers instead of the [1] method
	    $res .= "*";
    }
    $res .= "$element->{NAME}";
    if (defined $element->{ARRAY_LEN} && util::is_constant($element->{ARRAY_LEN})) {
	    $res .= "[$element->{ARRAY_LEN}]";
    }
    $res .= ";\n";
}

#####################################################################
# parse a struct
sub HeaderStruct($$)
{
    my($struct) = shift;
    my($name) = shift;
    $res .= "\nstruct $name {\n";
    $tab_depth++;
    my $el_count=0;
    if (defined $struct->{ELEMENTS}) {
	foreach my $e (@{$struct->{ELEMENTS}}) {
	    HeaderElement($e);
	    $el_count++;
	}
    }
    if ($el_count == 0) {
	    # some compilers can't handle empty structures
	    $res .= "\tchar _empty_;\n";
    }
    $tab_depth--;
    $res .= "}";
}

#####################################################################
# parse a enum
sub HeaderEnum($$)
{
    my($enum) = shift;
    my($name) = shift;

    util::register_enum($enum, $name);

    $res .= "\nenum $name {\n";
    $tab_depth++;
    my $els = \@{$enum->{ELEMENTS}};
    foreach my $i (0 .. $#{$els}-1) {
	    my $e = ${$els}[$i];
	    tabs();
	    chomp $e;
	    $res .= "$e,\n";
    }

    my $e = ${$els}[$#{$els}];
    tabs();
    chomp $e;
    if ($e !~ /^(.*?)\s*$/) {
	    die "Bad enum $name\n";
    }
    $res .= "$1\n";
    $tab_depth--;
    $res .= "}";
}

#####################################################################
# parse a bitmap
sub HeaderBitmap($$)
{
    my($bitmap) = shift;
    my($name) = shift;

    util::register_bitmap($bitmap, $name);

    $res .= "\n/* bitmap $name */\n";

    my $els = \@{$bitmap->{ELEMENTS}};
    foreach my $i (0 .. $#{$els}) {
	    my $e = ${$els}[$i];
	    chomp $e;
	    $res .= "#define $e\n";
    }

    $res .= "\n";
}

#####################################################################
# parse a union
sub HeaderUnion($$)
{
	my($union) = shift;
	my($name) = shift;
	my %done = ();

	(defined $union->{PROPERTIES}) && HeaderProperties($union->{PROPERTIES});
	$res .= "\nunion $name {\n";
	$tab_depth++;
	foreach my $e (@{$union->{DATA}}) {
		if ($e->{TYPE} eq "UNION_ELEMENT") {
			if (! defined $done{$e->{DATA}->{NAME}}) {
				HeaderElement($e->{DATA});
			}
			$done{$e->{DATA}->{NAME}} = 1;
		}
	}
	$tab_depth--;
	$res .= "}";
}

#####################################################################
# parse a type
sub HeaderType($$$)
{
	my $e = shift;
	my($data) = shift;
	my($name) = shift;
	if (ref($data) eq "HASH") {
		($data->{TYPE} eq "ENUM") &&
		    HeaderEnum($data, $name);
		($data->{TYPE} eq "BITMAP") &&
		    HeaderBitmap($data, $name);
		($data->{TYPE} eq "STRUCT") &&
		    HeaderStruct($data, $name);
		($data->{TYPE} eq "UNION") &&
		    HeaderUnion($data, $name);
		return;
	}
	if ($data =~ "string") {
		$res .= "const char *";
	} elsif (util::is_enum($e->{TYPE})) {
		$res .= "enum $data";
	} elsif (util::is_bitmap($e->{TYPE})) {
		my $bitmap = util::get_bitmap($e->{TYPE});
		$res .= util::bitmap_type_decl($bitmap);
	} elsif (util::is_scalar_type($data)) {
		$res .= "$data";
	} elsif (util::has_property($e, "switch_is")) {
		$res .= "union $data";
	} else {
		$res .= "struct $data";
	}
}

#####################################################################
# parse a declare
sub HeaderDeclare($)
{
	my($declare) = shift;

	if ($declare->{DATA}->{TYPE} eq "ENUM") {
		util::enum_bitmap($declare, $declare->{NAME});
	} elsif ($declare->{DATA}->{TYPE} eq "BITMAP") {
		util::register_bitmap($declare, $declare->{NAME});
	}
}

#####################################################################
# parse a typedef
sub HeaderTypedef($)
{
    my($typedef) = shift;
    HeaderType($typedef, $typedef->{DATA}, $typedef->{NAME});
    $res .= ";\n" unless ($typedef->{DATA}->{TYPE} eq "BITMAP");
}

#####################################################################
# prototype a typedef
sub HeaderTypedefProto($)
{
    my($d) = shift;

    if (needed::is_needed("ndr_size_$d->{NAME}")) {
	    $res .= "size_t ndr_size_$d->{NAME}(const struct $d->{NAME} *r, int flags);\n";
    }

    if (!util::has_property($d, "public")) {
	    return;
    }

    if ($d->{DATA}{TYPE} eq "STRUCT") {
	    $res .= "NTSTATUS ndr_push_$d->{NAME}(struct ndr_push *ndr, int ndr_flags, struct $d->{NAME} *r);\n";
	    $res .= "NTSTATUS ndr_pull_$d->{NAME}(struct ndr_pull *ndr, int ndr_flags, struct $d->{NAME} *r);\n";
	    if (!util::has_property($d, "noprint")) {
		    $res .= "void ndr_print_$d->{NAME}(struct ndr_print *ndr, const char *name, struct $d->{NAME} *r);\n";
	    }

    }
    if ($d->{DATA}{TYPE} eq "UNION") {
	    $res .= "NTSTATUS ndr_push_$d->{NAME}(struct ndr_push *ndr, int ndr_flags, int level, union $d->{NAME} *r);\n";
	    $res .= "NTSTATUS ndr_pull_$d->{NAME}(struct ndr_pull *ndr, int ndr_flags, int level, union $d->{NAME} *r);\n";
	    if (!util::has_property($d, "noprint")) {
		    $res .= "void ndr_print_$d->{NAME}(struct ndr_print *ndr, const char *name, int level, union $d->{NAME} *r);\n";
	    }
    }

    if ($d->{DATA}{TYPE} eq "ENUM") {
	    $res .= "NTSTATUS ndr_push_$d->{NAME}(struct ndr_push *ndr, enum $d->{NAME} r);\n";
	    $res .= "NTSTATUS ndr_pull_$d->{NAME}(struct ndr_pull *ndr, enum $d->{NAME} *r);\n";
	    if (!util::has_property($d, "noprint")) {
		    $res .= "void ndr_print_$d->{NAME}(struct ndr_print *ndr, const char *name, enum $d->{NAME} r);\n";
	    }
    }

    if ($d->{DATA}{TYPE} eq "BITMAP") {
    	    my $type_decl = util::bitmap_type_decl($d->{DATA});
	    $res .= "NTSTATUS ndr_push_$d->{NAME}(struct ndr_push *ndr, $type_decl r);\n";
	    $res .= "NTSTATUS ndr_pull_$d->{NAME}(struct ndr_pull *ndr, $type_decl *r);\n";
	    if (!util::has_property($d, "noprint")) {
		    $res .= "void ndr_print_$d->{NAME}(struct ndr_print *ndr, const char *name, $type_decl r);\n";
	    }
    }
}

#####################################################################
# parse a const
sub HeaderConst($)
{
    my($const) = shift;
    if (!defined($const->{ARRAY_LEN})) {
    	$res .= "#define $const->{NAME}\t( $const->{VALUE} )\n";
    } else {
    	$res .= "#define $const->{NAME}\t $const->{VALUE}\n";
    }
}

#####################################################################
# parse a function
sub HeaderFunctionInOut($$)
{
    my($fn) = shift;
    my($prop) = shift;

    foreach my $e (@{$fn->{DATA}}) {
	    if (util::has_property($e, $prop)) {
		    HeaderElement($e);
	    }
    }
}

#####################################################################
# determine if we need an "in" or "out" section
sub HeaderFunctionInOut_needed($$)
{
    my($fn) = shift;
    my($prop) = shift;

    if ($prop eq "out" && $fn->{RETURN_TYPE} && $fn->{RETURN_TYPE} ne "void") {
	    return 1;
    }

    foreach my $e (@{$fn->{DATA}}) {
	    if (util::has_property($e, $prop)) {
		    return 1;
	    }
    }

    return undef;
}


#####################################################################
# parse a function
sub HeaderFunction($)
{
    my($fn) = shift;

    $res .= "\nstruct $fn->{NAME} {\n";
    $tab_depth++;
    my $needed = 0;

    if (HeaderFunctionInOut_needed($fn, "in")) {
	    tabs();
	    $res .= "struct {\n";
	    $tab_depth++;
	    HeaderFunctionInOut($fn, "in");
	    $tab_depth--;
	    tabs();
	    $res .= "} in;\n\n";
	    $needed++;
    }

    if (HeaderFunctionInOut_needed($fn, "out")) {
	    tabs();
	    $res .= "struct {\n";
	    $tab_depth++;
	    HeaderFunctionInOut($fn, "out");
	    if ($fn->{RETURN_TYPE} && $fn->{RETURN_TYPE} ne "void") {
		    tabs();
		    $res .= "$fn->{RETURN_TYPE} result;\n";
	    }
	    $tab_depth--;
	    tabs();
	    $res .= "} out;\n\n";
	    $needed++;
    }

    if (! $needed) {
	    # sigh - some compilers don't like empty structures
	    tabs();
	    $res .= "int _dummy_element;\n";
    }

    $tab_depth--;
    $res .= "};\n\n";
}

#####################################################################
# output prototypes for a IDL function
sub HeaderFnProto($$)
{
	my $interface = shift;
    my $fn = shift;
    my $name = $fn->{NAME};
	
    $res .= "void ndr_print_$name(struct ndr_print *ndr, const char *name, int flags, struct $name *r);\n";

	if (util::has_property($interface, "object")) {
		$res .= "NTSTATUS dcom_$interface->{NAME}_$name (struct dcom_interface_p *d, TALLOC_CTX *mem_ctx, struct $name *r);\n";
	} else {
	    $res .= "NTSTATUS dcerpc_$name(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, struct $name *r);\n";
    	$res .= "struct rpc_request *dcerpc_$name\_send(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, struct $name *r);\n";
	}
    $res .= "\n";
}


#####################################################################
# generate vtable structure for DCOM interface
sub HeaderVTable($)
{
	my $interface = shift;
	$res .= "struct dcom_$interface->{NAME}_vtable {\n";
 	if (defined($interface->{BASE})) {
		$res .= "\tstruct dcom_$interface->{BASE}\_vtable base;\n";
	}

	my $data = $interface->{DATA};
	foreach my $d (@{$data}) {
		$res .= "\tNTSTATUS (*$d->{NAME}) (struct dcom_interface_p *d, TALLOC_CTX *mem_ctx, struct $d->{NAME} *r);\n" if ($d->{TYPE} eq "FUNCTION");
	}
	$res .= "};\n\n";
}


#####################################################################
# parse the interface definitions
sub HeaderInterface($)
{
    my($interface) = shift;
    my($data) = $interface->{DATA};

    my $count = 0;

    $res .= "#ifndef _HEADER_NDR_$interface->{NAME}\n";
    $res .= "#define _HEADER_NDR_$interface->{NAME}\n\n";

    if (defined $interface->{PROPERTIES}->{depends}) {
	    my @d = split / /, $interface->{PROPERTIES}->{depends};
	    foreach my $i (@d) {
		    $res .= "#include \"librpc/gen_ndr/ndr_$i\.h\"\n";
	    }
    }

    if (defined $interface->{PROPERTIES}->{uuid}) {
	    my $name = uc $interface->{NAME};
	    $res .= "#define DCERPC_$name\_UUID " . 
		util::make_str($interface->{PROPERTIES}->{uuid}) . "\n";

		if(!defined $interface->{PROPERTIES}->{version}) { $interface->{PROPERTIES}->{version} = "0.0"; }
	    $res .= "#define DCERPC_$name\_VERSION $interface->{PROPERTIES}->{version}\n";

	    $res .= "#define DCERPC_$name\_NAME \"$interface->{NAME}\"\n";

		if(!defined $interface->{PROPERTIES}->{helpstring}) { $interface->{PROPERTIES}->{helpstring} = "NULL"; }
		$res .= "#define DCERPC_$name\_HELPSTRING $interface->{PROPERTIES}->{helpstring}\n";

	    $res .= "\nextern const struct dcerpc_interface_table dcerpc_table_$interface->{NAME};\n";
	    $res .= "NTSTATUS dcerpc_server_$interface->{NAME}_init(void);\n\n";
    }

    foreach my $d (@{$data}) {
	    if ($d->{TYPE} eq "FUNCTION") {
		    my $u_name = uc $d->{NAME};
			$res .= "#define DCERPC_$u_name (";
		
			if (defined($interface->{BASE})) {
				$res .= "DCERPC_" . uc $interface->{BASE} . "_CALL_COUNT + ";
			}
			
		    $res .= sprintf("0x%02x", $count) . ")\n";
		    $count++;
	    }
    }

	$res .= "\n#define DCERPC_" . uc $interface->{NAME} . "_CALL_COUNT (";
	
	if (defined($interface->{BASE})) {
		$res .= "DCERPC_" . uc $interface->{BASE} . "_CALL_COUNT + ";
	}
	
	$res .= "$count)\n\n";

    foreach my $d (@{$data}) {
	($d->{TYPE} eq "CONST") &&
	    HeaderConst($d);
	($d->{TYPE} eq "DECLARE") &&
	    HeaderDeclare($d);
	($d->{TYPE} eq "TYPEDEF") &&
	    HeaderTypedef($d);
	($d->{TYPE} eq "TYPEDEF") &&
	    HeaderTypedefProto($d);
	($d->{TYPE} eq "FUNCTION") &&
	    HeaderFunction($d);
	($d->{TYPE} eq "FUNCTION") &&
	    HeaderFnProto($interface, $d);
    }
	
	(util::has_property($interface, "object")) &&
		HeaderVTable($interface);

    $res .= "#endif /* _HEADER_NDR_$interface->{NAME} */\n";
}

#####################################################################
# parse a parsed IDL into a C header
sub Parse($)
{
    my($idl) = shift;
    $tab_depth = 0;

    $res = "/* header auto-generated by pidl */\n\n";
    foreach my $x (@{$idl}) {
	    if ($x->{TYPE} eq "INTERFACE") {
		    needed::BuildNeeded($x);
		    HeaderInterface($x);
	    }
    }
    return $res;
}

1;
