####################################################################
#
#    This file was generated using Parse::Yapp version 1.05.
#
#        Don't edit this file, use source file instead.
#
#             ANY CHANGE MADE HERE WILL BE LOST !
#
####################################################################
package idl;
use vars qw ( @ISA );
use strict;

@ISA= qw ( Parse::Yapp::Driver );
#Included Parse/Yapp/Driver.pm file----------------------------------------
{
#
# Module Parse::Yapp::Driver
#
# This module is part of the Parse::Yapp package available on your
# nearest CPAN
#
# Any use of this module in a standalone parser make the included
# text under the same copyright as the Parse::Yapp module itself.
#
# This notice should remain unchanged.
#
# (c) Copyright 1998-2001 Francois Desarmenien, all rights reserved.
# (see the pod text in Parse::Yapp module for use and distribution rights)
#

package Parse::Yapp::Driver;

require 5.004;

use strict;

use vars qw ( $VERSION $COMPATIBLE $FILENAME );

$VERSION = '1.05';
$COMPATIBLE = '0.07';
$FILENAME=__FILE__;

use Carp;

#Known parameters, all starting with YY (leading YY will be discarded)
my(%params)=(YYLEX => 'CODE', 'YYERROR' => 'CODE', YYVERSION => '',
			 YYRULES => 'ARRAY', YYSTATES => 'ARRAY', YYDEBUG => '');
#Mandatory parameters
my(@params)=('LEX','RULES','STATES');

sub new {
    my($class)=shift;
	my($errst,$nberr,$token,$value,$check,$dotpos);
    my($self)={ ERROR => \&_Error,
				ERRST => \$errst,
                NBERR => \$nberr,
				TOKEN => \$token,
				VALUE => \$value,
				DOTPOS => \$dotpos,
				STACK => [],
				DEBUG => 0,
				CHECK => \$check };

	_CheckParams( [], \%params, \@_, $self );

		exists($$self{VERSION})
	and	$$self{VERSION} < $COMPATIBLE
	and	croak "Yapp driver version $VERSION ".
			  "incompatible with version $$self{VERSION}:\n".
			  "Please recompile parser module.";

        ref($class)
    and $class=ref($class);

    bless($self,$class);
}

sub YYParse {
    my($self)=shift;
    my($retval);

	_CheckParams( \@params, \%params, \@_, $self );

	if($$self{DEBUG}) {
		_DBLoad();
		$retval = eval '$self->_DBParse()';#Do not create stab entry on compile
        $@ and die $@;
	}
	else {
		$retval = $self->_Parse();
	}
    $retval
}

sub YYData {
	my($self)=shift;

		exists($$self{USER})
	or	$$self{USER}={};

	$$self{USER};
	
}

sub YYErrok {
	my($self)=shift;

	${$$self{ERRST}}=0;
    undef;
}

sub YYNberr {
	my($self)=shift;

	${$$self{NBERR}};
}

sub YYRecovering {
	my($self)=shift;

	${$$self{ERRST}} != 0;
}

sub YYAbort {
	my($self)=shift;

	${$$self{CHECK}}='ABORT';
    undef;
}

sub YYAccept {
	my($self)=shift;

	${$$self{CHECK}}='ACCEPT';
    undef;
}

sub YYError {
	my($self)=shift;

	${$$self{CHECK}}='ERROR';
    undef;
}

sub YYSemval {
	my($self)=shift;
	my($index)= $_[0] - ${$$self{DOTPOS}} - 1;

		$index < 0
	and	-$index <= @{$$self{STACK}}
	and	return $$self{STACK}[$index][1];

	undef;	#Invalid index
}

sub YYCurtok {
	my($self)=shift;

        @_
    and ${$$self{TOKEN}}=$_[0];
    ${$$self{TOKEN}};
}

sub YYCurval {
	my($self)=shift;

        @_
    and ${$$self{VALUE}}=$_[0];
    ${$$self{VALUE}};
}

sub YYExpect {
    my($self)=shift;

    keys %{$self->{STATES}[$self->{STACK}[-1][0]]{ACTIONS}}
}

sub YYLexer {
    my($self)=shift;

	$$self{LEX};
}


#################
# Private stuff #
#################


sub _CheckParams {
	my($mandatory,$checklist,$inarray,$outhash)=@_;
	my($prm,$value);
	my($prmlst)={};

	while(($prm,$value)=splice(@$inarray,0,2)) {
        $prm=uc($prm);
			exists($$checklist{$prm})
		or	croak("Unknow parameter '$prm'");
			ref($value) eq $$checklist{$prm}
		or	croak("Invalid value for parameter '$prm'");
        $prm=unpack('@2A*',$prm);
		$$outhash{$prm}=$value;
	}
	for (@$mandatory) {
			exists($$outhash{$_})
		or	croak("Missing mandatory parameter '".lc($_)."'");
	}
}

sub _Error {
	print "Parse error.\n";
}

sub _DBLoad {
	{
		no strict 'refs';

			exists(${__PACKAGE__.'::'}{_DBParse})#Already loaded ?
		and	return;
	}
	my($fname)=__FILE__;
	my(@drv);
	open(DRV,"<$fname") or die "Report this as a BUG: Cannot open $fname";
	while(<DRV>) {
                	/^\s*sub\s+_Parse\s*{\s*$/ .. /^\s*}\s*#\s*_Parse\s*$/
        	and     do {
                	s/^#DBG>//;
                	push(@drv,$_);
        	}
	}
	close(DRV);

	$drv[0]=~s/_P/_DBP/;
	eval join('',@drv);
}

#Note that for loading debugging version of the driver,
#this file will be parsed from 'sub _Parse' up to '}#_Parse' inclusive.
#So, DO NOT remove comment at end of sub !!!
sub _Parse {
    my($self)=shift;

	my($rules,$states,$lex,$error)
     = @$self{ 'RULES', 'STATES', 'LEX', 'ERROR' };
	my($errstatus,$nberror,$token,$value,$stack,$check,$dotpos)
     = @$self{ 'ERRST', 'NBERR', 'TOKEN', 'VALUE', 'STACK', 'CHECK', 'DOTPOS' };

#DBG>	my($debug)=$$self{DEBUG};
#DBG>	my($dbgerror)=0;

#DBG>	my($ShowCurToken) = sub {
#DBG>		my($tok)='>';
#DBG>		for (split('',$$token)) {
#DBG>			$tok.=		(ord($_) < 32 or ord($_) > 126)
#DBG>					?	sprintf('<%02X>',ord($_))
#DBG>					:	$_;
#DBG>		}
#DBG>		$tok.='<';
#DBG>	};

	$$errstatus=0;
	$$nberror=0;
	($$token,$$value)=(undef,undef);
	@$stack=( [ 0, undef ] );
	$$check='';

    while(1) {
        my($actions,$act,$stateno);

        $stateno=$$stack[-1][0];
        $actions=$$states[$stateno];

#DBG>	print STDERR ('-' x 40),"\n";
#DBG>		$debug & 0x2
#DBG>	and	print STDERR "In state $stateno:\n";
#DBG>		$debug & 0x08
#DBG>	and	print STDERR "Stack:[".
#DBG>					 join(',',map { $$_[0] } @$stack).
#DBG>					 "]\n";


        if  (exists($$actions{ACTIONS})) {

				defined($$token)
            or	do {
				($$token,$$value)=&$lex($self);
#DBG>				$debug & 0x01
#DBG>			and	print STDERR "Need token. Got ".&$ShowCurToken."\n";
			};

            $act=   exists($$actions{ACTIONS}{$$token})
                    ?   $$actions{ACTIONS}{$$token}
                    :   exists($$actions{DEFAULT})
                        ?   $$actions{DEFAULT}
                        :   undef;
        }
        else {
            $act=$$actions{DEFAULT};
#DBG>			$debug & 0x01
#DBG>		and	print STDERR "Don't need token.\n";
        }

            defined($act)
        and do {

                $act > 0
            and do {        #shift

#DBG>				$debug & 0x04
#DBG>			and	print STDERR "Shift and go to state $act.\n";

					$$errstatus
				and	do {
					--$$errstatus;

#DBG>					$debug & 0x10
#DBG>				and	$dbgerror
#DBG>				and	$$errstatus == 0
#DBG>				and	do {
#DBG>					print STDERR "**End of Error recovery.\n";
#DBG>					$dbgerror=0;
#DBG>				};
				};


                push(@$stack,[ $act, $$value ]);

					$$token ne ''	#Don't eat the eof
				and	$$token=$$value=undef;
                next;
            };

            #reduce
            my($lhs,$len,$code,@sempar,$semval);
            ($lhs,$len,$code)=@{$$rules[-$act]};

#DBG>			$debug & 0x04
#DBG>		and	$act
#DBG>		and	print STDERR "Reduce using rule ".-$act." ($lhs,$len): ";

                $act
            or  $self->YYAccept();

            $$dotpos=$len;

                unpack('A1',$lhs) eq '@'    #In line rule
            and do {
                    $lhs =~ /^\@[0-9]+\-([0-9]+)$/
                or  die "In line rule name '$lhs' ill formed: ".
                        "report it as a BUG.\n";
                $$dotpos = $1;
            };

            @sempar =       $$dotpos
                        ?   map { $$_[1] } @$stack[ -$$dotpos .. -1 ]
                        :   ();

            $semval = $code ? &$code( $self, @sempar )
                            : @sempar ? $sempar[0] : undef;

            splice(@$stack,-$len,$len);

                $$check eq 'ACCEPT'
            and do {

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Accept.\n";

				return($semval);
			};

                $$check eq 'ABORT'
            and	do {

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Abort.\n";

				return(undef);

			};

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Back to state $$stack[-1][0], then ";

                $$check eq 'ERROR'
            or  do {
#DBG>				$debug & 0x04
#DBG>			and	print STDERR 
#DBG>				    "go to state $$states[$$stack[-1][0]]{GOTOS}{$lhs}.\n";

#DBG>				$debug & 0x10
#DBG>			and	$dbgerror
#DBG>			and	$$errstatus == 0
#DBG>			and	do {
#DBG>				print STDERR "**End of Error recovery.\n";
#DBG>				$dbgerror=0;
#DBG>			};

			    push(@$stack,
                     [ $$states[$$stack[-1][0]]{GOTOS}{$lhs}, $semval ]);
                $$check='';
                next;
            };

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Forced Error recovery.\n";

            $$check='';

        };

        #Error
            $$errstatus
        or   do {

            $$errstatus = 1;
            &$error($self);
                $$errstatus # if 0, then YYErrok has been called
            or  next;       # so continue parsing

#DBG>			$debug & 0x10
#DBG>		and	do {
#DBG>			print STDERR "**Entering Error recovery.\n";
#DBG>			++$dbgerror;
#DBG>		};

            ++$$nberror;

        };

			$$errstatus == 3	#The next token is not valid: discard it
		and	do {
				$$token eq ''	# End of input: no hope
			and	do {
#DBG>				$debug & 0x10
#DBG>			and	print STDERR "**At eof: aborting.\n";
				return(undef);
			};

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Dicard invalid token ".&$ShowCurToken.".\n";

			$$token=$$value=undef;
		};

        $$errstatus=3;

		while(	  @$stack
			  and (		not exists($$states[$$stack[-1][0]]{ACTIONS})
			        or  not exists($$states[$$stack[-1][0]]{ACTIONS}{error})
					or	$$states[$$stack[-1][0]]{ACTIONS}{error} <= 0)) {

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Pop state $$stack[-1][0].\n";

			pop(@$stack);
		}

			@$stack
		or	do {

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**No state left on stack: aborting.\n";

			return(undef);
		};

		#shift the error token

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Shift \$error token and go to state ".
#DBG>						 $$states[$$stack[-1][0]]{ACTIONS}{error}.
#DBG>						 ".\n";

		push(@$stack, [ $$states[$$stack[-1][0]]{ACTIONS}{error}, undef ]);

    }

    #never reached
	croak("Error in driver logic. Please, report it as a BUG");

}#_Parse
#DO NOT remove comment

1;

}
#End of include--------------------------------------------------




sub new {
        my($class)=shift;
        ref($class)
    and $class=ref($class);

    my($self)=$class->SUPER::new( yyversion => '1.05',
                                  yystates =>
[
	{#State 0
		DEFAULT => -1,
		GOTOS => {
			'idl' => 1
		}
	},
	{#State 1
		ACTIONS => {
			'' => 2
		},
		DEFAULT => -49,
		GOTOS => {
			'interface' => 3,
			'coclass' => 4,
			'property_list' => 5
		}
	},
	{#State 2
		DEFAULT => 0
	},
	{#State 3
		DEFAULT => -2
	},
	{#State 4
		DEFAULT => -3
	},
	{#State 5
		ACTIONS => {
			"coclass" => 6,
			"interface" => 8,
			"[" => 7
		}
	},
	{#State 6
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 10
		}
	},
	{#State 7
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 11,
			'properties' => 13,
			'property' => 12
		}
	},
	{#State 8
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 14
		}
	},
	{#State 9
		DEFAULT => -73
	},
	{#State 10
		ACTIONS => {
			"{" => 15
		}
	},
	{#State 11
		ACTIONS => {
			"(" => 16
		},
		DEFAULT => -53
	},
	{#State 12
		DEFAULT => -51
	},
	{#State 13
		ACTIONS => {
			"," => 17,
			"]" => 18
		}
	},
	{#State 14
		ACTIONS => {
			":" => 19
		},
		DEFAULT => -8,
		GOTOS => {
			'base_interface' => 20
		}
	},
	{#State 15
		DEFAULT => -5,
		GOTOS => {
			'interfaces' => 21
		}
	},
	{#State 16
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'listtext' => 26,
			'anytext' => 25,
			'text' => 24,
			'constant' => 27
		}
	},
	{#State 17
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 11,
			'property' => 29
		}
	},
	{#State 18
		DEFAULT => -50
	},
	{#State 19
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 30
		}
	},
	{#State 20
		ACTIONS => {
			"{" => 31
		}
	},
	{#State 21
		ACTIONS => {
			"}" => 32
		},
		DEFAULT => -49,
		GOTOS => {
			'interface' => 33,
			'property_list' => 34
		}
	},
	{#State 22
		DEFAULT => -75
	},
	{#State 23
		DEFAULT => -60
	},
	{#State 24
		DEFAULT => -62
	},
	{#State 25
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"|" => 40,
			"(" => 41,
			"*" => 42,
			"." => 43,
			">" => 44
		},
		DEFAULT => -55
	},
	{#State 26
		ACTIONS => {
			"," => 45,
			")" => 46
		}
	},
	{#State 27
		DEFAULT => -61
	},
	{#State 28
		DEFAULT => -74
	},
	{#State 29
		DEFAULT => -52
	},
	{#State 30
		DEFAULT => -9
	},
	{#State 31
		ACTIONS => {
			"typedef" => 47,
			"const" => 53
		},
		DEFAULT => -49,
		GOTOS => {
			'const' => 52,
			'function' => 48,
			'typedef' => 54,
			'definitions' => 49,
			'definition' => 51,
			'property_list' => 50
		}
	},
	{#State 32
		ACTIONS => {
			";" => 56
		},
		DEFAULT => -76,
		GOTOS => {
			'optional_semicolon' => 55
		}
	},
	{#State 33
		DEFAULT => -6
	},
	{#State 34
		ACTIONS => {
			"[" => 7,
			"interface" => 8
		}
	},
	{#State 35
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 57,
			'constant' => 27
		}
	},
	{#State 36
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 58,
			'constant' => 27
		}
	},
	{#State 37
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 59,
			'constant' => 27,
			'commalisttext' => 60
		}
	},
	{#State 38
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 61,
			'constant' => 27
		}
	},
	{#State 39
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 62,
			'constant' => 27
		}
	},
	{#State 40
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 63,
			'constant' => 27
		}
	},
	{#State 41
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 59,
			'constant' => 27,
			'commalisttext' => 64
		}
	},
	{#State 42
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 65,
			'constant' => 27
		}
	},
	{#State 43
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 66,
			'constant' => 27
		}
	},
	{#State 44
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 67,
			'constant' => 27
		}
	},
	{#State 45
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 68,
			'constant' => 27
		}
	},
	{#State 46
		DEFAULT => -54
	},
	{#State 47
		DEFAULT => -49,
		GOTOS => {
			'property_list' => 69
		}
	},
	{#State 48
		DEFAULT => -12
	},
	{#State 49
		ACTIONS => {
			"}" => 70,
			"typedef" => 47,
			"const" => 53
		},
		DEFAULT => -49,
		GOTOS => {
			'const' => 52,
			'function' => 48,
			'typedef' => 54,
			'definition' => 71,
			'property_list' => 50
		}
	},
	{#State 50
		ACTIONS => {
			'IDENTIFIER' => 9,
			"union" => 72,
			"enum" => 73,
			"[" => 7,
			'void' => 74,
			"struct" => 80
		},
		GOTOS => {
			'identifier' => 75,
			'struct' => 76,
			'enum' => 77,
			'type' => 78,
			'union' => 79
		}
	},
	{#State 51
		DEFAULT => -10
	},
	{#State 52
		DEFAULT => -13
	},
	{#State 53
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 81
		}
	},
	{#State 54
		DEFAULT => -14
	},
	{#State 55
		DEFAULT => -4
	},
	{#State 56
		DEFAULT => -77
	},
	{#State 57
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -63
	},
	{#State 58
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -70
	},
	{#State 59
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		},
		DEFAULT => -57
	},
	{#State 60
		ACTIONS => {
			"}" => 82,
			"," => 83
		}
	},
	{#State 61
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -68
	},
	{#State 62
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -69
	},
	{#State 63
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -67
	},
	{#State 64
		ACTIONS => {
			"," => 83,
			")" => 84
		}
	},
	{#State 65
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -65
	},
	{#State 66
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -64
	},
	{#State 67
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -66
	},
	{#State 68
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		},
		DEFAULT => -56
	},
	{#State 69
		ACTIONS => {
			'IDENTIFIER' => 9,
			"union" => 72,
			"enum" => 73,
			"[" => 7,
			'void' => 74,
			"struct" => 80
		},
		GOTOS => {
			'identifier' => 75,
			'struct' => 76,
			'enum' => 77,
			'type' => 85,
			'union' => 79
		}
	},
	{#State 70
		ACTIONS => {
			";" => 56
		},
		DEFAULT => -76,
		GOTOS => {
			'optional_semicolon' => 86
		}
	},
	{#State 71
		DEFAULT => -11
	},
	{#State 72
		ACTIONS => {
			"{" => 87
		}
	},
	{#State 73
		ACTIONS => {
			"{" => 88
		}
	},
	{#State 74
		DEFAULT => -23
	},
	{#State 75
		DEFAULT => -22
	},
	{#State 76
		DEFAULT => -19
	},
	{#State 77
		DEFAULT => -21
	},
	{#State 78
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 89
		}
	},
	{#State 79
		DEFAULT => -20
	},
	{#State 80
		ACTIONS => {
			"{" => 90
		}
	},
	{#State 81
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 91
		}
	},
	{#State 82
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 92,
			'constant' => 27
		}
	},
	{#State 83
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 93,
			'constant' => 27
		}
	},
	{#State 84
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 94,
			'constant' => 27
		}
	},
	{#State 85
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 95
		}
	},
	{#State 86
		DEFAULT => -7
	},
	{#State 87
		ACTIONS => {
			"[" => 98
		},
		GOTOS => {
			'union_elements' => 96,
			'union_element' => 97
		}
	},
	{#State 88
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 99,
			'enum_element' => 100,
			'enum_elements' => 101
		}
	},
	{#State 89
		ACTIONS => {
			"(" => 102
		}
	},
	{#State 90
		DEFAULT => -40,
		GOTOS => {
			'element_list1' => 103
		}
	},
	{#State 91
		ACTIONS => {
			"[" => 106,
			"=" => 105
		},
		GOTOS => {
			'array_len' => 104
		}
	},
	{#State 92
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"*" => 42,
			"{" => 37,
			"&" => 38,
			"/" => 39,
			"|" => 40,
			"(" => 41,
			"." => 43,
			">" => 44
		},
		DEFAULT => -72
	},
	{#State 93
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		},
		DEFAULT => -58
	},
	{#State 94
		ACTIONS => {
			"{" => 37
		},
		DEFAULT => -71
	},
	{#State 95
		ACTIONS => {
			"[" => 106
		},
		DEFAULT => -46,
		GOTOS => {
			'array_len' => 107
		}
	},
	{#State 96
		ACTIONS => {
			"}" => 108,
			"[" => 98
		},
		GOTOS => {
			'union_element' => 109
		}
	},
	{#State 97
		DEFAULT => -31
	},
	{#State 98
		ACTIONS => {
			"case" => 110,
			"default" => 111
		}
	},
	{#State 99
		ACTIONS => {
			"=" => 112
		},
		DEFAULT => -27
	},
	{#State 100
		DEFAULT => -25
	},
	{#State 101
		ACTIONS => {
			"}" => 113,
			"," => 114
		}
	},
	{#State 102
		ACTIONS => {
			"," => -42,
			"void" => 117,
			")" => -42
		},
		DEFAULT => -49,
		GOTOS => {
			'base_element' => 115,
			'element_list2' => 118,
			'property_list' => 116
		}
	},
	{#State 103
		ACTIONS => {
			"}" => 119
		},
		DEFAULT => -49,
		GOTOS => {
			'base_element' => 120,
			'property_list' => 116
		}
	},
	{#State 104
		ACTIONS => {
			"=" => 121
		}
	},
	{#State 105
		ACTIONS => {
			'IDENTIFIER' => 9,
			'CONSTANT' => 28,
			'TEXT' => 22
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 122,
			'constant' => 27
		}
	},
	{#State 106
		ACTIONS => {
			'IDENTIFIER' => 9,
			'CONSTANT' => 28,
			'TEXT' => 22,
			"]" => 124
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 123,
			'constant' => 27
		}
	},
	{#State 107
		ACTIONS => {
			";" => 125
		}
	},
	{#State 108
		DEFAULT => -30
	},
	{#State 109
		DEFAULT => -32
	},
	{#State 110
		ACTIONS => {
			"(" => 126
		}
	},
	{#State 111
		ACTIONS => {
			"]" => 127
		}
	},
	{#State 112
		ACTIONS => {
			'CONSTANT' => 28,
			'TEXT' => 22,
			'IDENTIFIER' => 9
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 128,
			'constant' => 27
		}
	},
	{#State 113
		DEFAULT => -24
	},
	{#State 114
		ACTIONS => {
			'IDENTIFIER' => 9
		},
		GOTOS => {
			'identifier' => 99,
			'enum_element' => 129
		}
	},
	{#State 115
		DEFAULT => -44
	},
	{#State 116
		ACTIONS => {
			'IDENTIFIER' => 9,
			"union" => 72,
			"enum" => 73,
			"[" => 7,
			'void' => 74,
			"struct" => 80
		},
		GOTOS => {
			'identifier' => 75,
			'struct' => 76,
			'enum' => 77,
			'type' => 130,
			'union' => 79
		}
	},
	{#State 117
		DEFAULT => -43
	},
	{#State 118
		ACTIONS => {
			"," => 131,
			")" => 132
		}
	},
	{#State 119
		DEFAULT => -29
	},
	{#State 120
		ACTIONS => {
			";" => 133
		}
	},
	{#State 121
		ACTIONS => {
			'IDENTIFIER' => 9,
			'CONSTANT' => 28,
			'TEXT' => 22
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 134,
			'constant' => 27
		}
	},
	{#State 122
		ACTIONS => {
			"-" => 35,
			";" => 135,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		}
	},
	{#State 123
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			"]" => 136,
			">" => 44
		}
	},
	{#State 124
		DEFAULT => -47
	},
	{#State 125
		DEFAULT => -18
	},
	{#State 126
		ACTIONS => {
			'IDENTIFIER' => 9,
			'CONSTANT' => 28,
			'TEXT' => 22
		},
		DEFAULT => -59,
		GOTOS => {
			'identifier' => 23,
			'text' => 24,
			'anytext' => 137,
			'constant' => 27
		}
	},
	{#State 127
		ACTIONS => {
			";" => 138
		},
		DEFAULT => -49,
		GOTOS => {
			'base_element' => 139,
			'property_list' => 116
		}
	},
	{#State 128
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		},
		DEFAULT => -28
	},
	{#State 129
		DEFAULT => -26
	},
	{#State 130
		DEFAULT => -38,
		GOTOS => {
			'pointers' => 140
		}
	},
	{#State 131
		DEFAULT => -49,
		GOTOS => {
			'base_element' => 141,
			'property_list' => 116
		}
	},
	{#State 132
		ACTIONS => {
			";" => 142
		}
	},
	{#State 133
		DEFAULT => -41
	},
	{#State 134
		ACTIONS => {
			"-" => 35,
			";" => 143,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			">" => 44
		}
	},
	{#State 135
		DEFAULT => -15
	},
	{#State 136
		DEFAULT => -48
	},
	{#State 137
		ACTIONS => {
			"-" => 35,
			"+" => 36,
			"&" => 38,
			"{" => 37,
			"/" => 39,
			"(" => 41,
			"|" => 40,
			"*" => 42,
			"." => 43,
			")" => 144,
			">" => 44
		}
	},
	{#State 138
		DEFAULT => -36
	},
	{#State 139
		ACTIONS => {
			";" => 145
		}
	},
	{#State 140
		ACTIONS => {
			'IDENTIFIER' => 9,
			"*" => 147
		},
		GOTOS => {
			'identifier' => 146
		}
	},
	{#State 141
		DEFAULT => -45
	},
	{#State 142
		DEFAULT => -17
	},
	{#State 143
		DEFAULT => -16
	},
	{#State 144
		ACTIONS => {
			"]" => 148
		}
	},
	{#State 145
		DEFAULT => -35
	},
	{#State 146
		ACTIONS => {
			"[" => 106
		},
		DEFAULT => -46,
		GOTOS => {
			'array_len' => 149
		}
	},
	{#State 147
		DEFAULT => -39
	},
	{#State 148
		ACTIONS => {
			";" => 150
		},
		DEFAULT => -49,
		GOTOS => {
			'base_element' => 151,
			'property_list' => 116
		}
	},
	{#State 149
		DEFAULT => -37
	},
	{#State 150
		DEFAULT => -34
	},
	{#State 151
		ACTIONS => {
			";" => 152
		}
	},
	{#State 152
		DEFAULT => -33
	}
],
                                  yyrules  =>
[
	[#Rule 0
		 '$start', 2, undef
	],
	[#Rule 1
		 'idl', 0, undef
	],
	[#Rule 2
		 'idl', 2,
sub
#line 19 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 3
		 'idl', 2,
sub
#line 20 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 4
		 'coclass', 7,
sub
#line 24 "build/pidl/idl.yp"
{$_[3] => {
               "TYPE" => "COCLASS", 
			   "PROPERTIES" => $_[1],
		       "NAME" => $_[3],
		       "DATA" => $_[5],
          }}
	],
	[#Rule 5
		 'interfaces', 0, undef
	],
	[#Rule 6
		 'interfaces', 2,
sub
#line 34 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 7
		 'interface', 8,
sub
#line 38 "build/pidl/idl.yp"
{$_[3] => {
               "TYPE" => "INTERFACE", 
			   "PROPERTIES" => $_[1],
		       "NAME" => $_[3],
			   "BASE" => $_[4],
		       "DATA" => $_[6],
          }}
	],
	[#Rule 8
		 'base_interface', 0, undef
	],
	[#Rule 9
		 'base_interface', 2,
sub
#line 49 "build/pidl/idl.yp"
{ $_[2] }
	],
	[#Rule 10
		 'definitions', 1,
sub
#line 53 "build/pidl/idl.yp"
{ [ $_[1] ] }
	],
	[#Rule 11
		 'definitions', 2,
sub
#line 54 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 12
		 'definition', 1, undef
	],
	[#Rule 13
		 'definition', 1, undef
	],
	[#Rule 14
		 'definition', 1, undef
	],
	[#Rule 15
		 'const', 6,
sub
#line 62 "build/pidl/idl.yp"
{{
                     "TYPE"  => "CONST", 
		     "DTYPE"  => $_[2],
		     "NAME"  => $_[3],
		     "VALUE" => $_[5]
        }}
	],
	[#Rule 16
		 'const', 7,
sub
#line 69 "build/pidl/idl.yp"
{{
                     "TYPE"  => "CONST", 
		     "DTYPE"  => $_[2],
		     "NAME"  => $_[3],
		     "ARRAY_LEN" => $_[4],
		     "VALUE" => $_[6],
        }}
	],
	[#Rule 17
		 'function', 7,
sub
#line 80 "build/pidl/idl.yp"
{{
		"TYPE" => "FUNCTION",
		"NAME" => $_[3],
		"RETURN_TYPE" => $_[2],
		"PROPERTIES" => $_[1],
		"DATA" => $_[5]
	 }}
	],
	[#Rule 18
		 'typedef', 6,
sub
#line 90 "build/pidl/idl.yp"
{{
	             "TYPE" => "TYPEDEF", 
                     "PROPERTIES" => $_[2],
		     "NAME" => $_[4],
		     "DATA" => $_[3],
		     "ARRAY_LEN" => $_[5]
        }}
	],
	[#Rule 19
		 'type', 1, undef
	],
	[#Rule 20
		 'type', 1, undef
	],
	[#Rule 21
		 'type', 1, undef
	],
	[#Rule 22
		 'type', 1, undef
	],
	[#Rule 23
		 'type', 1,
sub
#line 100 "build/pidl/idl.yp"
{ "void" }
	],
	[#Rule 24
		 'enum', 4,
sub
#line 105 "build/pidl/idl.yp"
{{
                     "TYPE" => "ENUM", 
		     "ELEMENTS" => $_[3]
        }}
	],
	[#Rule 25
		 'enum_elements', 1,
sub
#line 112 "build/pidl/idl.yp"
{ [ $_[1] ] }
	],
	[#Rule 26
		 'enum_elements', 3,
sub
#line 113 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[3]); $_[1] }
	],
	[#Rule 27
		 'enum_element', 1, undef
	],
	[#Rule 28
		 'enum_element', 3,
sub
#line 117 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 29
		 'struct', 4,
sub
#line 121 "build/pidl/idl.yp"
{{
                     "TYPE" => "STRUCT", 
		     "ELEMENTS" => $_[3]
        }}
	],
	[#Rule 30
		 'union', 4,
sub
#line 128 "build/pidl/idl.yp"
{{
		"TYPE" => "UNION",
		"DATA" => $_[3]
	 }}
	],
	[#Rule 31
		 'union_elements', 1,
sub
#line 135 "build/pidl/idl.yp"
{ [ $_[1] ] }
	],
	[#Rule 32
		 'union_elements', 2,
sub
#line 136 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 33
		 'union_element', 8,
sub
#line 141 "build/pidl/idl.yp"
{{
		"TYPE" => "UNION_ELEMENT",
		"CASE" => $_[4],
		"DATA" => $_[7]
	 }}
	],
	[#Rule 34
		 'union_element', 7,
sub
#line 147 "build/pidl/idl.yp"
{{
		"TYPE" => "EMPTY",
		"CASE" => $_[4],
	 }}
	],
	[#Rule 35
		 'union_element', 5,
sub
#line 152 "build/pidl/idl.yp"
{{
		"TYPE" => "UNION_ELEMENT",
		"CASE" => "default",
		"DATA" => $_[4]
	 }}
	],
	[#Rule 36
		 'union_element', 4,
sub
#line 158 "build/pidl/idl.yp"
{{
		"TYPE" => "EMPTY",
		"CASE" => "default",
	 }}
	],
	[#Rule 37
		 'base_element', 5,
sub
#line 165 "build/pidl/idl.yp"
{{
			   "NAME" => $_[4],
			   "TYPE" => $_[2],
			   "PROPERTIES" => $_[1],
			   "POINTERS" => $_[3],
			   "ARRAY_LEN" => $_[5]
              }}
	],
	[#Rule 38
		 'pointers', 0,
sub
#line 177 "build/pidl/idl.yp"
{ 0 }
	],
	[#Rule 39
		 'pointers', 2,
sub
#line 178 "build/pidl/idl.yp"
{ $_[1]+1 }
	],
	[#Rule 40
		 'element_list1', 0, undef
	],
	[#Rule 41
		 'element_list1', 3,
sub
#line 185 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[2]); $_[1] }
	],
	[#Rule 42
		 'element_list2', 0, undef
	],
	[#Rule 43
		 'element_list2', 1, undef
	],
	[#Rule 44
		 'element_list2', 1,
sub
#line 191 "build/pidl/idl.yp"
{ [ $_[1] ] }
	],
	[#Rule 45
		 'element_list2', 3,
sub
#line 192 "build/pidl/idl.yp"
{ push(@{$_[1]}, $_[3]); $_[1] }
	],
	[#Rule 46
		 'array_len', 0, undef
	],
	[#Rule 47
		 'array_len', 2,
sub
#line 197 "build/pidl/idl.yp"
{ "*" }
	],
	[#Rule 48
		 'array_len', 3,
sub
#line 198 "build/pidl/idl.yp"
{ "$_[2]" }
	],
	[#Rule 49
		 'property_list', 0, undef
	],
	[#Rule 50
		 'property_list', 4,
sub
#line 204 "build/pidl/idl.yp"
{ util::FlattenHash([$_[1],$_[3]]); }
	],
	[#Rule 51
		 'properties', 1,
sub
#line 207 "build/pidl/idl.yp"
{ $_[1] }
	],
	[#Rule 52
		 'properties', 3,
sub
#line 208 "build/pidl/idl.yp"
{ util::FlattenHash([$_[1], $_[3]]); }
	],
	[#Rule 53
		 'property', 1,
sub
#line 211 "build/pidl/idl.yp"
{{ "$_[1]" => "1"     }}
	],
	[#Rule 54
		 'property', 4,
sub
#line 212 "build/pidl/idl.yp"
{{ "$_[1]" => "$_[3]" }}
	],
	[#Rule 55
		 'listtext', 1, undef
	],
	[#Rule 56
		 'listtext', 3,
sub
#line 217 "build/pidl/idl.yp"
{ "$_[1] $_[3]" }
	],
	[#Rule 57
		 'commalisttext', 1, undef
	],
	[#Rule 58
		 'commalisttext', 3,
sub
#line 222 "build/pidl/idl.yp"
{ "$_[1],$_[3]" }
	],
	[#Rule 59
		 'anytext', 0,
sub
#line 226 "build/pidl/idl.yp"
{ "" }
	],
	[#Rule 60
		 'anytext', 1, undef
	],
	[#Rule 61
		 'anytext', 1, undef
	],
	[#Rule 62
		 'anytext', 1, undef
	],
	[#Rule 63
		 'anytext', 3,
sub
#line 228 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 64
		 'anytext', 3,
sub
#line 229 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 65
		 'anytext', 3,
sub
#line 230 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 66
		 'anytext', 3,
sub
#line 231 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 67
		 'anytext', 3,
sub
#line 232 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 68
		 'anytext', 3,
sub
#line 233 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 69
		 'anytext', 3,
sub
#line 234 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 70
		 'anytext', 3,
sub
#line 235 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]" }
	],
	[#Rule 71
		 'anytext', 5,
sub
#line 236 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]$_[4]$_[5]" }
	],
	[#Rule 72
		 'anytext', 5,
sub
#line 237 "build/pidl/idl.yp"
{ "$_[1]$_[2]$_[3]$_[4]$_[5]" }
	],
	[#Rule 73
		 'identifier', 1, undef
	],
	[#Rule 74
		 'constant', 1, undef
	],
	[#Rule 75
		 'text', 1,
sub
#line 246 "build/pidl/idl.yp"
{ "\"$_[1]\"" }
	],
	[#Rule 76
		 'optional_semicolon', 0, undef
	],
	[#Rule 77
		 'optional_semicolon', 1, undef
	]
],
                                  @_);
    bless($self,$class);
}

#line 257 "build/pidl/idl.yp"


use util;

sub _Error {
        if (exists $_[0]->YYData->{ERRMSG}) {
		print $_[0]->YYData->{ERRMSG};
		delete $_[0]->YYData->{ERRMSG};
		return;
	};
	my $line = $_[0]->YYData->{LINE};
	my $last_token = $_[0]->YYData->{LAST_TOKEN};
	my $file = $_[0]->YYData->{INPUT_FILENAME};
	
	print "$file:$line: Syntax error near '$last_token'\n";
}

sub _Lexer($)
{
	my($parser)=shift;

        $parser->YYData->{INPUT}
        or  return('',undef);

again:
	$parser->YYData->{INPUT} =~ s/^[ \t]*//;

	for ($parser->YYData->{INPUT}) {
		if (/^\#/) {
			if (s/^\# (\d+) \"(.*?)\"( \d+|)//) {
				$parser->YYData->{LINE} = $1-1;
				$parser->YYData->{INPUT_FILENAME} = $2;
				goto again;
			}
			if (s/^\#line (\d+) \"(.*?)\"( \d+|)//) {
				$parser->YYData->{LINE} = $1-1;
				$parser->YYData->{INPUT_FILENAME} = $2;
				goto again;
			}
			if (s/^(\#.*)$//m) {
				goto again;
			}
		}
		if (s/^(\n)//) {
			$parser->YYData->{LINE}++;
			goto again;
		}
		if (s/^\"(.*?)\"//) {
			$parser->YYData->{LAST_TOKEN} = $1;
			return('TEXT',$1); 
		}
		if (s/^(\d+)(\W|$)/$2/) {
			$parser->YYData->{LAST_TOKEN} = $1;
			return('CONSTANT',$1); 
		}
		if (s/^([\w_]+)//) {
			$parser->YYData->{LAST_TOKEN} = $1;
			if ($1 =~ 
			    /^(coclass|interface|const|typedef|union
			      |struct|enum|void|case|default)$/x) {
				return $1;
			}
			return('IDENTIFIER',$1);
		}
		if (s/^(.)//s) {
			$parser->YYData->{LAST_TOKEN} = $1;
			return($1,$1);
		}
	}
}

sub parse_idl($$)
{
	my $self = shift;
	my $filename = shift;

	my $saved_delim = $/;
	undef $/;
	my $cpp = $ENV{CPP};
	if (! defined $cpp) {
		$cpp = "cpp"
	}
	my $data = `$cpp -xc $filename`;
	$/ = $saved_delim;

    $self->YYData->{INPUT} = $data;
    $self->YYData->{LINE} = 0;
    $self->YYData->{LAST_TOKEN} = "NONE";

	my $idl = $self->YYParse( yylex => \&_Lexer, yyerror => \&_Error );

	foreach my $x (@{$idl}) {
		# Add [in] ORPCTHIS *this, [out] ORPCTHAT *that
		# for 'object' interfaces
		if (defined($x->{PROPERTIES}->{object})) {
			foreach my $e (@{$x->{DATA}}) {
				if($e->{TYPE} eq "FUNCTION") {
					$e->{PROPERTIES}->{object} = 1;
					unshift(@{$e->{DATA}}, 
                        { 'NAME' => 'ORPCthis',
                          'POINTERS' => 0,
                          'PROPERTIES' => { 'in' => '1' },
                          'TYPE' => 'ORPCTHIS'
                        });
					unshift(@{$e->{DATA}},
                        { 'NAME' => 'ORPCthat',
                          'POINTERS' => 0,
                          'PROPERTIES' => { 'out' => '1' },
						  'TYPE' => 'ORPCTHAT'
                        });
				}
			}
		}
		
		# Do the inheritance
		if (defined($x->{BASE}) and $x->{BASE} ne "") {
			my $parent = util::get_interface($idl, $x->{BASE});

			if(not defined($parent)) { 
				die("No such parent interface " . $x->{BASE});
			}
			
			@{$x->{INHERITED_DATA}} = (@{$parent->{INHERITED_DATA}}, @{$x->{DATA}});
		} else {
			$x->{INHERITED_DATA} = $x->{DATA};
		}
	}

	return $idl;
}

1;
