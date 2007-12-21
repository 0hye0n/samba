#!/usr/bin/perl
# Bootstrap Samba and run a number of tests against it.
# Copyright (C) 2005-2007 Jelmer Vernooij <jelmer@samba.org>
# Published under the GNU GPL, v3 or later.

package Samba4;

use strict;
use Cwd qw(abs_path);
use FindBin qw($RealBin);
use POSIX;

sub new($$$$) {
	my ($classname, $bindir, $ldap, $setupdir) = @_;
	my $self = { 
		vars => {}, 
		ldap => $ldap, 
		bindir => $bindir, 
		setupdir => $setupdir 
	};
	bless $self;
	return $self;
}

sub openldap_start($$$) {
        my ($slapd_conf, $uri, $logs) = @_;
        my $oldpath = $ENV{PATH};
        $ENV{PATH} = "/usr/local/sbin:/usr/sbin:/sbin:$ENV{PATH}";
        system("slapd -d0 -f $slapd_conf -h $uri > $logs 2>&1 &");
        $ENV{PATH} = $oldpath;
}

sub slapd_start($$)
{
	my $count = 0;
	my ($self, $env_vars) = @_;

	my $uri = $env_vars->{LDAP_URI};

	# running slapd in the background means it stays in the same process group, so it can be
	# killed by timelimit
	if ($self->{ldap} eq "fedora-ds") {
	        system("$ENV{FEDORA_DS_PREFIX}/sbin/ns-slapd -D $env_vars->{FEDORA_DS_DIR} -d0 -i $env_vars->{FEDORA_DS_PIDFILE}> $env_vars->{LDAPDIR}/logs 2>&1 &");
	} elsif ($self->{ldap} eq "openldap") {
	        openldap_start($env_vars->{SLAPD_CONF}, $uri, "$env_vars->{LDAPDIR}/logs");
	}
	while (system("$self->{bindir}/ldbsearch -H $uri -s base -b \"\" supportedLDAPVersion > /dev/null") != 0) {
	        $count++;
		if ($count > 40) {
		    $self->slapd_stop($env_vars);
		    return 0;
		}
		sleep(1);
	}
	return 1;
}

sub slapd_stop($$)
{
	my ($self, $envvars) = @_;
	if ($self->{ldap} eq "fedora-ds") {
		system("$envvars->{LDAPDIR}/slapd-samba4/stop-slapd");
	} elsif ($self->{ldap} eq "openldap") {
		open(IN, "<$envvars->{OPENLDAP_PIDFILE}") or 
			die("unable to open slapd pid file: $envvars->{OPENLDAP_PIDFILE}");
		kill 9, <IN>;
		close(IN);
	}
	return 1;
}

sub check_or_start($$$) 
{
	my ($self, $env_vars, $max_time) = @_;
	return 0 if ( -p $env_vars->{SMBD_TEST_FIFO});

	unlink($env_vars->{SMBD_TEST_FIFO});
	POSIX::mkfifo($env_vars->{SMBD_TEST_FIFO}, 0700);
	unlink($env_vars->{SMBD_TEST_LOG});
	
	print "STARTING SMBD... ";
	my $pid = fork();
	if ($pid == 0) {
		open STDIN, $env_vars->{SMBD_TEST_FIFO};
		open STDOUT, ">$env_vars->{SMBD_TEST_LOG}";
		open STDERR, '>&STDOUT';
		
		SocketWrapper::set_default_iface($env_vars->{SOCKET_WRAPPER_DEFAULT_IFACE});

		my $valgrind = "";
		if (defined($ENV{SMBD_VALGRIND})) {
		    $valgrind = $ENV{SMBD_VALGRIND};
		} 

		$ENV{KRB5_CONFIG} = $env_vars->{KRB5_CONFIG}; 

		# Start slapd before smbd, but with the fifo on stdin
		if (defined($self->{ldap})) {
		    $self->slapd_start($env_vars) or 
			die("couldn't start slapd (2nd time)");
		}

		my $optarg = "";
		if (defined($max_time)) {
			$optarg = "--maximum-runtime=$max_time ";
		}
		if (defined($ENV{SMBD_OPTIONS})) {
			$optarg.= " $ENV{SMBD_OPTIONS}";
		}
		my $ret = system("$valgrind $self->{bindir}/smbd $optarg $env_vars->{CONFIGURATION} -M single -i --leak-report-full");
		if ($? == -1) {
			print "Unable to start smbd: $ret: $!\n";
			exit 1;
		}
		unlink($env_vars->{SMBD_TEST_FIFO});
		my $exit = $? >> 8;
		if ( $ret == 0 ) {
			print "smbd exits with status $exit\n";
		} elsif ( $ret & 127 ) {
			print "smbd got signal ".($ret & 127)." and exits with $exit!\n";
		} else {
			$ret = $? >> 8;
			print "smbd failed with status $exit!\n";
		}
		exit $exit;
	}
	print "DONE\n";

	open(DATA, ">$env_vars->{SMBD_TEST_FIFO}");

	return $pid;
}

sub wait_for_start($$)
{
	my ($self, $testenv_vars) = @_;
	# give time for nbt server to register its names
	print "delaying for nbt name registration\n";
	sleep 2;

	# This will return quickly when things are up, but be slow if we 
	# need to wait for (eg) SSL init 
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{SERVER}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{SERVER}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{NETBIOSNAME}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{NETBIOSNAME}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{NETBIOSALIAS}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{NETBIOSALIAS}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{SERVER}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{SERVER}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{NETBIOSNAME}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{NETBIOSNAME}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} $testenv_vars->{NETBIOSALIAS}");
	system("bin/nmblookup $testenv_vars->{CONFIGURATION} -U $testenv_vars->{SERVER_IP} $testenv_vars->{NETBIOSALIAS}");

	print $self->getlog_env($testenv_vars);
}

sub write_ldb_file($$$)
{
	my ($self, $file, $ldif) = @_;

	open(LDIF, "|$self->{bindir}/ldbadd -H $file >/dev/null");
	print LDIF $ldif;
	return close(LDIF);
}

sub add_wins_config($$)
{
	my ($self, $privatedir) = @_;

	return $self->write_ldb_file("$privatedir/wins_config.ldb", "
dn: name=TORTURE_6,CN=PARTNERS
objectClass: wreplPartner
name: TORTURE_6
address: 127.0.0.6
pullInterval: 0
pushChangeCount: 0
type: 0x3
");
}

sub mk_fedora_ds($$$)
{
	my ($self, $ldapdir, $configuration) = @_;

	my $fedora_ds_inf = "$ldapdir/fedorads.inf";
	my $fedora_ds_extra_ldif = "$ldapdir/fedorads-partitions.ldif";

	#Make the subdirectory be as fedora DS would expect
	my $fedora_ds_dir = "$ldapdir/slapd-samba4";

	my $pidfile = "$fedora_ds_dir/logs/slapd-samba4.pid";

	system("$self->{bindir}/ad2oLschema $configuration -H $ldapdir/schema-tmp.ldb --option=convert:target=fedora-ds -I $self->{setupdir}/schema-map-fedora-ds-1.0 -O $ldapdir/99_ad.ldif >&2") == 0 or die("schema conversion for Fedora DS failed");

my $dir = getcwd();
chdir "$ENV{FEDORA_DS_PREFIX}/bin" || die;
	if (system("perl $ENV{FEDORA_DS_PREFIX}/sbin/setup-ds.pl --silent --file=$fedora_ds_inf >&2") != 0) {
            chdir $dir;
            die("perl $ENV{FEDORA_DS_PREFIX}/sbin/setup-ds.pl --silent --file=$fedora_ds_inf FAILED: $?");
        }
        chdir $dir || die;

	return ($fedora_ds_dir, $pidfile);
}

sub mk_openldap($$$)
{
	my ($self, $ldapdir, $configuration) = @_;

	my $slapd_conf = "$ldapdir/slapd.conf";
	my $pidfile = "$ldapdir/slapd.pid";
	my $modconf = "$ldapdir/modules.conf";

	#This uses the backend provision we just did, to read out the schema
	system("$self->{bindir}/ad2oLschema $configuration --option=convert:target=openldap -H $ldapdir/schema-tmp.ldb -I $self->{setupdir}/schema-map-openldap-2.3 -O $ldapdir/backend-schema.schema >&2") == 0 or die("schema conversion for OpenLDAP failed");

	my $oldpath = $ENV{PATH};
	$ENV{PATH} = "$ENV{OPENLDAP_PATH}/usr/local/sbin:/usr/sbin:/sbin:$ENV{PATH}";

	unlink($modconf);
	open(CONF, ">$modconf"); close(CONF);

	if (system("slaptest -u -f $slapd_conf >&2") != 0) {
		open(CONF, ">$modconf"); 
		# enable slapd modules
		print CONF "
moduleload	back_hdb
moduleload	syncprov
";
		close(CONF);
	}

	if (system("slaptest -u -f $slapd_conf >&2") != 0) {
		open(CONF, ">$modconf"); 
		# enable slapd modules
		print CONF "
modulepath	/usr/lib/ldap
moduleload	back_hdb
moduleload	syncprov
";
		close(CONF);
	}

	if (system("slaptest -u -f $slapd_conf >&2") != 0) {
		open(CONF, ">$modconf"); 
		# enable slapd modules (Fedora layout)
		print CONF "
modulepath	/usr/lib/openldap
moduleload	syncprov
";
		close(CONF);
	}

	if (system("slaptest -u -f $slapd_conf >&2") != 0) {
		open(CONF, ">$modconf"); 
		# enable slapd modules (Fedora x86_64 layout)
		print CONF "
modulepath	/usr/lib64/openldap
moduleload	syncprov
";
		close(CONF);
	}

	system("slaptest -u -f $slapd_conf") == 0 or die("slaptest still fails after adding modules");

    
	$ENV{PATH} = $oldpath;

	return ($slapd_conf, $pidfile);
}

sub mk_keyblobs($$)
{
	my ($self, $tlsdir) = @_;

	#TLS and PKINIT crypto blobs
	my $dhfile = "$tlsdir/dhparms.pem";
	my $cafile = "$tlsdir/ca.pem";
	my $certfile = "$tlsdir/cert.pem";
	my $reqkdc = "$tlsdir/req-kdc.der";
	my $kdccertfile = "$tlsdir/kdc.pem";
	my $keyfile = "$tlsdir/key.pem";
	my $adminkeyfile = "$tlsdir/adminkey.pem";
	my $reqadmin = "$tlsdir/req-admin.der";
	my $admincertfile = "$tlsdir/admincert.pem";

	mkdir($tlsdir, 0777);

	#This is specified here to avoid draining entropy on every run
	open(DHFILE, ">$dhfile");
	print DHFILE <<EOF;
-----BEGIN DH PARAMETERS-----
MGYCYQC/eWD2xkb7uELmqLi+ygPMKyVcpHUo2yCluwnbPutEueuxrG/Cys8j8wLO
svCN/jYNyR2NszOmg7ZWcOC/4z/4pWDVPUZr8qrkhj5MRKJc52MncfaDglvEdJrv
YX70obsCAQI=
-----END DH PARAMETERS-----
EOF
	close(DHFILE);

	#Likewise, we pregenerate the key material.  This allows the 
	#other certificates to be pre-generated
	open(KEYFILE, ">$keyfile");
	print KEYFILE <<EOF;
-----BEGIN RSA PRIVATE KEY-----
MIICXQIBAAKBgQDKg6pAwCHUMA1DfHDmWhZfd+F0C+9Jxcqvpw9ii9En3E1uflpc
ol3+S9/6I/uaTmJHZre+DF3dTzb/UOZo0Zem8N+IzzkgoGkFafjXuT3BL5UPY2/H
6H+pPqVIRLOmrWImai359YyoKhFyo37Y6HPeU8QcZ+u2rS9geapIWfeuowIDAQAB
AoGAAqDLzFRR/BF1kpsiUfL4WFvTarCe9duhwj7ORc6fs785qAXuwUYAJ0Uvzmy6
HqoGv3t3RfmeHDmjcpPHsbOKnsOQn2MgmthidQlPBMWtQMff5zdoYNUFiPS0XQBq
szNW4PRjaA9KkLQVTwnzdXGkBSkn/nGxkaVu7OR3vJOBoo0CQQDO4upypesnbe6p
9/xqfZ2uim8IwV1fLlFClV7WlCaER8tsQF4lEi0XSzRdXGUD/dilpY88Nb+xok/X
8Z8OvgAXAkEA+pcLsx1gN7kxnARxv54jdzQjC31uesJgMKQXjJ0h75aUZwTNHmZQ
vPxi6u62YiObrN5oivkixwFNncT9MxTxVQJBAMaWUm2SjlLe10UX4Zdm1MEB6OsC
kVoX37CGKO7YbtBzCfTzJGt5Mwc1DSLA2cYnGJqIfSFShptALlwedot0HikCQAJu
jNKEKnbf+TdGY8Q0SKvTebOW2Aeg80YFkaTvsXCdyXrmdQcifw4WdO9KucJiDhSz
Y9hVapz7ykEJtFtWjLECQQDIlfc63I5ZpXfg4/nN4IJXUW6AmPVOYIA5215itgki
cSlMYli1H9MEXH0pQMGv5Qyd0OYIx2DDg96mZ+aFvqSG
-----END RSA PRIVATE KEY-----
EOF
	close(KEYFILE);

	open(ADMINKEYFILE, ">$adminkeyfile");

	print ADMINKEYFILE <<EOF;
-----BEGIN RSA PRIVATE KEY-----
MIICXQIBAAKBgQD0+OL7TQBj0RejbIH1+g5GeRaWaM9xF43uE5y7jUHEsi5owhZF
5iIoHZeeL6cpDF5y1BZRs0JlA1VqMry1jjKlzFYVEMMFxB6esnXhl0Jpip1JkUMM
XLOP1m/0dqayuHBWozj9f/cdyCJr0wJIX1Z8Pr+EjYRGPn/MF0xdl3JRlwIDAQAB
AoGAP8mjCP628Ebc2eACQzOWjgEvwYCPK4qPmYOf1zJkArzG2t5XAGJ5WGrENRuB
cm3XFh1lpmaADl982UdW3gul4gXUy6w4XjKK4vVfhyHj0kZ/LgaXUK9BAGhroJ2L
osIOUsaC6jdx9EwSRctwdlF3wWJ8NK0g28AkvIk+FlolW4ECQQD7w5ouCDnf58CN
u4nARx4xv5XJXekBvOomkCQAmuOsdOb6b9wn3mm2E3au9fueITjb3soMR31AF6O4
eAY126rXAkEA+RgHzybzZEP8jCuznMqoN2fq/Vrs6+W3M8/G9mzGEMgLLpaf2Jiz
I9tLZ0+OFk9tkRaoCHPfUOCrVWJZ7Y53QQJBAMhoA6rw0WDyUcyApD5yXg6rusf4
ASpo/tqDkqUIpoL464Qe1tjFqtBM3gSXuhs9xsz+o0bzATirmJ+WqxrkKTECQHt2
OLCpKqwAspU7N+w32kaUADoRLisCEdrhWklbwpQgwsIVsCaoEOpt0CLloJRYTANE
yoZeAErTALjyZYZEPcECQQDlUi0N8DFxQ/lOwWyR3Hailft+mPqoPCa8QHlQZnlG
+cfgNl57YHMTZFwgUVFRdJNpjH/WdZ5QxDcIVli0q+Ko
-----END RSA PRIVATE KEY-----
EOF

	#generated with 
	#hxtool issue-certificate --self-signed --issue-ca --ca-private-key=FILE:$KEYFILE \
	#          --subject="CN=CA,$BASEDN" --certificate="FILE:$CAFILE"

	open(CAFILE, ">$cafile");
	print CAFILE <<EOF;
-----BEGIN CERTIFICATE-----
MIIChTCCAe6gAwIBAgIUFZoF6jt0R+hQBdF7cWPy0tT3fGwwCwYJKoZIhvcNAQEFMFIxEzAR
BgoJkiaJk/IsZAEZDANjb20xFzAVBgoJkiaJk/IsZAEZDAdleGFtcGxlMRUwEwYKCZImiZPy
LGQBGQwFc2FtYmExCzAJBgNVBAMMAkNBMCIYDzIwMDcwMTIzMDU1MzA5WhgPMjAwODAxMjQw
NTUzMDlaMFIxEzARBgoJkiaJk/IsZAEZDANjb20xFzAVBgoJkiaJk/IsZAEZDAdleGFtcGxl
MRUwEwYKCZImiZPyLGQBGQwFc2FtYmExCzAJBgNVBAMMAkNBMIGfMA0GCSqGSIb3DQEBAQUA
A4GNADCBiQKBgQDKg6pAwCHUMA1DfHDmWhZfd+F0C+9Jxcqvpw9ii9En3E1uflpcol3+S9/6
I/uaTmJHZre+DF3dTzb/UOZo0Zem8N+IzzkgoGkFafjXuT3BL5UPY2/H6H+pPqVIRLOmrWIm
ai359YyoKhFyo37Y6HPeU8QcZ+u2rS9geapIWfeuowIDAQABo1YwVDAOBgNVHQ8BAf8EBAMC
AqQwEgYDVR0lBAswCQYHKwYBBQIDBTAdBgNVHQ4EFgQUwtm596AMotmzRU7IVdgrUvozyjIw
DwYDVR0TBAgwBgEB/wIBADANBgkqhkiG9w0BAQUFAAOBgQBgzh5uLDmESGYv60iUdEfuk/T9
VCpzb1z3VJVWt3uJoQYbcpR00SKeyMdlfTTLzO6tSPMmlk4hwqfvLkPzGCSObR4DRRYa0BtY
2laBVlg9X59bGpMUvpFQfpvxjvFWNJDL+377ELCVpLNdoR23I9TKXlalj0bY5Ks46CVIrm6W
EA==
-----END CERTIFICATE-----
EOF

	#generated with GNUTLS internally in Samba.  

	open(CERTFILE, ">$certfile");
	print CERTFILE <<EOF;
-----BEGIN CERTIFICATE-----
MIICYTCCAcygAwIBAgIE5M7SRDALBgkqhkiG9w0BAQUwZTEdMBsGA1UEChMUU2Ft
YmEgQWRtaW5pc3RyYXRpb24xNDAyBgNVBAsTK1NhbWJhIC0gdGVtcG9yYXJ5IGF1
dG9nZW5lcmF0ZWQgY2VydGlmaWNhdGUxDjAMBgNVBAMTBVNhbWJhMB4XDTA2MDgw
NDA0MzY1MloXDTA4MDcwNDA0MzY1MlowZTEdMBsGA1UEChMUU2FtYmEgQWRtaW5p
c3RyYXRpb24xNDAyBgNVBAsTK1NhbWJhIC0gdGVtcG9yYXJ5IGF1dG9nZW5lcmF0
ZWQgY2VydGlmaWNhdGUxDjAMBgNVBAMTBVNhbWJhMIGcMAsGCSqGSIb3DQEBAQOB
jAAwgYgCgYDKg6pAwCHUMA1DfHDmWhZfd+F0C+9Jxcqvpw9ii9En3E1uflpcol3+
S9/6I/uaTmJHZre+DF3dTzb/UOZo0Zem8N+IzzkgoGkFafjXuT3BL5UPY2/H6H+p
PqVIRLOmrWImai359YyoKhFyo37Y6HPeU8QcZ+u2rS9geapIWfeuowIDAQABoyUw
IzAMBgNVHRMBAf8EAjAAMBMGA1UdJQQMMAoGCCsGAQUFBwMBMAsGCSqGSIb3DQEB
BQOBgQAmkN6XxvDnoMkGcWLCTwzxGfNNSVcYr7TtL2aJh285Xw9zaxcm/SAZBFyG
LYOChvh6hPU7joMdDwGfbiLrBnMag+BtGlmPLWwp/Kt1wNmrRhduyTQFhN3PP6fz
nBr9vVny2FewB2gHmelaPS//tXdxivSXKz3NFqqXLDJjq7P8wA==
-----END CERTIFICATE-----
EOF
	close(CERTFILE);

	#KDC certificate
	# hxtool request-create --subject="CN=krbtgt,cn=users,$basedn" --key=FILE:$KEYFILE $KDCREQ

	# hxtool issue-certificate --ca-certificate=FILE:$CAFILE,$KEYFILE --type="pkinit-kdc" --pk-init-principal="krbtgt/$RELAM@$REALM" --req="$KDCREQ" --certificate="FILE:$KDCCERTFILE"

	open(KDCCERTFILE, ">$kdccertfile");
	print KDCCERTFILE <<EOF;
-----BEGIN CERTIFICATE-----
MIIDDDCCAnWgAwIBAgIUDEhjaOT1ZjHjHHEn+l5eYO05oK8wCwYJKoZIhvcNAQEFMFIxEzAR
BgoJkiaJk/IsZAEZDANjb20xFzAVBgoJkiaJk/IsZAEZDAdleGFtcGxlMRUwEwYKCZImiZPy
LGQBGQwFc2FtYmExCzAJBgNVBAMMAkNBMCIYDzIwMDcwMTIzMDcwNzA4WhgPMjAwODAxMjQw
NzA3MDhaMGYxEzARBgoJkiaJk/IsZAEZDANjb20xFzAVBgoJkiaJk/IsZAEZDAdleGFtcGxl
MRUwEwYKCZImiZPyLGQBGQwFc2FtYmExDjAMBgNVBAMMBXVzZXJzMQ8wDQYDVQQDDAZrcmJ0
Z3QwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBAMqDqkDAIdQwDUN8cOZaFl934XQL70nF
yq+nD2KL0SfcTW5+WlyiXf5L3/oj+5pOYkdmt74MXd1PNv9Q5mjRl6bw34jPOSCgaQVp+Ne5
PcEvlQ9jb8fof6k+pUhEs6atYiZqLfn1jKgqEXKjftjoc95TxBxn67atL2B5qkhZ966jAgMB
AAGjgcgwgcUwDgYDVR0PAQH/BAQDAgWgMBIGA1UdJQQLMAkGBysGAQUCAwUwVAYDVR0RBE0w
S6BJBgYrBgEFAgKgPzA9oBMbEVNBTUJBLkVYQU1QTEUuQ09NoSYwJKADAgEBoR0wGxsGa3Ji
dGd0GxFTQU1CQS5FWEFNUExFLkNPTTAfBgNVHSMEGDAWgBTC2bn3oAyi2bNFTshV2CtS+jPK
MjAdBgNVHQ4EFgQUwtm596AMotmzRU7IVdgrUvozyjIwCQYDVR0TBAIwADANBgkqhkiG9w0B
AQUFAAOBgQCMSgLkIv9RobE0a95H2ECA+5YABBwKXIt4AyN/HpV7iJdRx7B9PE6vM+nboVKY
E7i7ECUc3bu6NgrLu7CKHelNclHWWMiZzSUwhkXyvG/LE9qtr/onNu9NfLt1OV+dwQwyLdEP
n63FxSmsKg3dfi3ryQI/DIKeisvipwDtLqOn9g==
-----END CERTIFICATE-----
EOF

	#hxtool request-create --subject="CN=Administrator,cn=users,$basedn" --key=FILE:$ADMINKEYFILE $ADMINREQFILE
	#hxtool issue-certificate --ca-certificate=FILE:$CAFILE,$KEYFILE --type="pkinit-client" --pk-init-principal="administrator@$REALM" --req="$ADMINREQFILE" --certificate="FILE:$ADMINCERTFILE"
	
	open(ADMINCERTFILE, ">$admincertfile");
	print ADMINCERTFILE <<EOF;
-----BEGIN CERTIFICATE-----
MIICwjCCAiugAwIBAgIUXyECoq4im33ByZDWZMGhtpvHYWEwCwYJKoZIhvcNAQEFMFIxEzAR
BgoJkiaJk/IsZAEZDANjb20xFzAVBgoJkiaJk/IsZAEZDAdleGFtcGxlMRUwEwYKCZImiZPy
LGQBGQwFc2FtYmExCzAJBgNVBAMMAkNBMCIYDzIwMDcwMTIzMDcyMzE2WhgPMjAwODAxMjQw
NzIzMTZaMCgxDjAMBgNVBAMMBXVzZXJzMRYwFAYDVQQDDA1BZG1pbmlzdHJhdG9yMIGfMA0G
CSqGSIb3DQEBAQUAA4GNADCBiQKBgQD0+OL7TQBj0RejbIH1+g5GeRaWaM9xF43uE5y7jUHE
si5owhZF5iIoHZeeL6cpDF5y1BZRs0JlA1VqMry1jjKlzFYVEMMFxB6esnXhl0Jpip1JkUMM
XLOP1m/0dqayuHBWozj9f/cdyCJr0wJIX1Z8Pr+EjYRGPn/MF0xdl3JRlwIDAQABo4G8MIG5
MA4GA1UdDwEB/wQEAwIFoDASBgNVHSUECzAJBgcrBgEFAgMEMEgGA1UdEQRBMD+gPQYGKwYB
BQICoDMwMaATGxFTQU1CQS5FWEFNUExFLkNPTaEaMBigAwIBAaERMA8bDWFkbWluaXN0cmF0
b3IwHwYDVR0jBBgwFoAUwtm596AMotmzRU7IVdgrUvozyjIwHQYDVR0OBBYEFCDzVsvJ8IDz
wLYH8EONeUa5oVrGMAkGA1UdEwQCMAAwDQYJKoZIhvcNAQEFBQADgYEAbTCnaPTieVZPV3bH
UmAMbnF9+YN1mCbe2xZJ0xzve+Yw1XO82iv/9kZaZkcRkaQt2qcwsBK/aSPOgfqGx+mJ7hXQ
AGWvAJhnWi25PawNaRysCN8WC6+nWKR4d2O2m5rpj3T9kH5WE7QbG0bCu92dGaS29FvWDCP3
q9pRtDOoAZc=
-----END CERTIFICATE-----
EOF
	close(ADMINCERTFILE);
}

sub provision($$$$$$)
{
	my ($self, $prefix, $server_role, $netbiosname, $netbiosalias, $swiface, $password) = @_;

	my $smbd_loglevel = 1;
	my $username = "administrator";
	my $domain = "SAMBADOMAIN";
	my $realm = "SAMBA.EXAMPLE.COM";
	my $dnsname = "samba.example.com";
	my $basedn = "dc=samba,dc=example,dc=com";
	my $unix_name = ($ENV{USER} or $ENV{LOGNAME} or `whoami`);
	my $srcdir="$RealBin/..";
	-d $prefix or mkdir($prefix, 0777) or die("Unable to create $prefix");
	my $prefix_abs = abs_path($prefix);
	my $tmpdir = "$prefix_abs/tmp";
	my $etcdir = "$prefix_abs/etc";
	my $piddir = "$prefix_abs/pid";
	my $conffile = "$etcdir/smb.conf";
	my $krb5_config = "$etcdir/krb5.conf";
	my $privatedir = "$prefix_abs/private";
	my $ncalrpcdir = "$prefix_abs/ncalrpc";
	my $lockdir = "$prefix_abs/lockdir";
	my $winbindd_socket_dir = "$prefix_abs/winbind_socket";
	my $winbindd_priv_pipe_dir = "$piddir/smbd.tmp/winbind_pipe";

	my $configuration = "--configfile=$conffile";
	my $ldapdir = "$privatedir/ldap";

	my $tlsdir = "$privatedir/tls";

	my $ifaceipv4 = "127.0.0.$swiface";
	my $interfaces = "$ifaceipv4/8";

	(system("rm -rf $prefix/*") == 0) or die("Unable to clean up");
	mkdir($_, 0777) foreach ($privatedir, $etcdir, $piddir, $ncalrpcdir, $lockdir, 
		$tmpdir);


	my $localbasedn = $basedn;
	$localbasedn = "DC=$netbiosname" if $server_role eq "member server";

	open(CONFFILE, ">$conffile");
	print CONFFILE "
[global]
	netbios name = $netbiosname
	netbios aliases = $netbiosalias
	workgroup = $domain
	realm = $realm
	private dir = $privatedir
	pid directory = $piddir
	ncalrpc dir = $ncalrpcdir
	lock dir = $lockdir
	setup directory = $self->{setupdir}
	js include = $srcdir/scripting/libjs
	winbindd socket directory = $winbindd_socket_dir
        winbind separator = /
	name resolve order = bcast
	interfaces = $interfaces
	tls dh params file = $tlsdir/dhparms.pem
	panic action = $srcdir/script/gdb_backtrace \%PID% \%PROG%
	wins support = yes
	server role = $server_role
	max xmit = 32K
	server max protocol = SMB2
	notify:inotify = false
	ldb:nosync = true
	system:anonymous = true
#We don't want to pass our self-tests if the PAC code is wrong
	gensec:require_pac = true
	log level = $smbd_loglevel

[tmp]
	path = $tmpdir
	read only = no
	ntvfs handler = posix
	posix:sharedelay = 100000
	posix:eadb = $lockdir/eadb.tdb

[cifs]
	read only = no
	ntvfs handler = cifs
	cifs:server = $netbiosname
	cifs:share = tmp
#There is no username specified here, instead the client is expected
#to log in with kerberos, and smbd will used delegated credentials.

[simple]
	path = $tmpdir
	read only = no
	ntvfs handler = simple

[cifsposix]
	copy = simple
	ntvfs handler = cifsposix   
";
	close(CONFFILE);

	$self->mk_keyblobs($tlsdir);

	open(KRB5CONF, ">$krb5_config");
	print KRB5CONF "
#Generated krb5.conf for $realm

[libdefaults]
 default_realm = $realm
 dns_lookup_realm = false
 dns_lookup_kdc = false
 ticket_lifetime = 24h
 forwardable = yes

[realms]
 $realm = {
  kdc = 127.0.0.1:88
  admin_server = 127.0.0.1:88
  default_domain = $dnsname
 }
 $dnsname = {
  kdc = 127.0.0.1:88
  admin_server = 127.0.0.1:88
  default_domain = $dnsname
 }
 $domain = {
  kdc = 127.0.0.1:88
  admin_server = 127.0.0.1:88
  default_domain = $dnsname
 }

[appdefaults]
	pkinit_anchors = FILE:$tlsdir/ca.pem

[kdc]
	enable-pkinit = true
	pkinit_identity = FILE:$tlsdir/kdc.pem,$tlsdir/key.pem
	pkinit_anchors = FILE:$tlsdir/ca.pem

[domain_realm]
 .$dnsname = $realm
";
	close(KRB5CONF);

#Ensure the config file is valid before we start
	if (system("$self->{bindir}/testparm $configuration -v --suppress-prompt >/dev/null 2>&1") != 0) {
		system("$self->{bindir}/testparm -v --suppress-prompt $configuration >&2");
		die("Failed to create a valid smb.conf configuration!");
	}

	(system("($self->{bindir}/testparm $configuration -v --suppress-prompt --parameter-name=\"netbios name\" --section-name=global 2> /dev/null | grep -i \"^$netbiosname\" ) >/dev/null 2>&1") == 0) or die("Failed to create a valid smb.conf configuration!");

my @provision_options = ("$self->{bindir}/smbscript", "$self->{setupdir}/provision");
	push (@provision_options, split(' ', $configuration));
	push (@provision_options, "--host-name=$netbiosname");
	push (@provision_options, "--host-ip=$ifaceipv4");
	push (@provision_options, "--quiet");
	push (@provision_options, "--domain=$domain");
	push (@provision_options, "--realm=$realm");
	push (@provision_options, "--adminpass=$password");
	push (@provision_options, "--krbtgtpass=krbtgt$password");
	push (@provision_options, "--machinepass=machine$password");
	push (@provision_options, "--root=$unix_name");
	push (@provision_options, "--simple-bind-dn=cn=Manager,$localbasedn");
	push (@provision_options, "--password=$password");
	push (@provision_options, "--server-role=$server_role");

	my $ldap_uri= "$ldapdir/ldapi";
	$ldap_uri =~ s|/|%2F|g;
	$ldap_uri = "ldapi://$ldap_uri";

	my $ret = {
		KRB5_CONFIG => $krb5_config,
		PIDDIR => $piddir,
		SERVER => $netbiosname,
		SERVER_IP => $ifaceipv4,
		NETBIOSNAME => $netbiosname,
		NETBIOSALIAS => $netbiosalias,
		LDAP_URI => $ldap_uri,
		DOMAIN => $domain,
		USERNAME => $username,
		REALM => $realm,
		PASSWORD => $password,
		LDAPDIR => $ldapdir,
		WINBINDD_SOCKET_DIR => $winbindd_socket_dir,
		WINBINDD_PRIV_PIPE_DIR => $winbindd_priv_pipe_dir,
		NCALRPCDIR => $ncalrpcdir,
		LOCKDIR => $lockdir,
		CONFIGURATION => $configuration,
		SOCKET_WRAPPER_DEFAULT_IFACE => $swiface
	};

	if (defined($self->{ldap})) {

                push (@provision_options, "--ldap-backend=$ldap_uri");
	        system("$self->{bindir}/smbscript $self->{setupdir}/provision-backend $configuration --ldap-manager-pass=$password --root=$unix_name --realm=$realm --host-name=$netbiosname --ldap-backend-type=$self->{ldap}>&2") == 0 or die("backend provision failed");

	        if ($self->{ldap} eq "openldap") {
		       ($ret->{SLAPD_CONF}, $ret->{OPENLDAP_PIDFILE}) = $self->mk_openldap($ldapdir, $configuration) or die("Unable to create openldap directories");
	        } elsif ($self->{ldap} eq "fedora-ds") {
		       ($ret->{FEDORA_DS_DIR}, $ret->{FEDORA_DS_PIDFILE}) = $self->mk_fedora_ds($ldapdir, $configuration) or die("Unable to create fedora ds directories");
		       push (@provision_options, "--ldap-module=nsuniqueid");
		       push (@provision_options, "--aci=aci:: KHRhcmdldGF0dHIgPSAiKiIpICh2ZXJzaW9uIDMuMDthY2wgImZ1bGwgYWNjZXNzIHRvIGFsbCBieSBhbGwiO2FsbG93IChhbGwpKHVzZXJkbiA9ICJsZGFwOi8vL2FueW9uZSIpOykK");
                 }

		$self->slapd_start($ret) or 
			die("couldn't start slapd");
	}

	(system(@provision_options) == 0) or die("Unable to provision");

	if (defined($self->{ldap})) {
		$self->slapd_stop($ret) or 
			die("couldn't stop slapd");
        }

	return $ret; 
}

sub provision_member($$$)
{
	my ($self, $prefix, $dcvars) = @_;
	print "PROVISIONING MEMBER...";

	my $ret = $self->provision($prefix,
				   "member server",
				   "localmember3",
				   "localmember",
				   3,
				   "localmemberpass");

	$ret or die("Unable to provision");

	my $cmd = "";
	$cmd .= "SOCKET_WRAPPER_DEFAULT_IFACE=\"$ret->{SOCKET_WRAPPER_DEFAULT_IFACE}\" ";
	$cmd .= "KRB5_CONFIG=\"$ret->{KRB5_CONFIG}\" ";
	$cmd .= "$self->{bindir}/net join $ret->{CONFIGURATION} $dcvars->{DOMAIN} member";
	$cmd .= " -U$dcvars->{USERNAME}\%$dcvars->{PASSWORD}";

	system($cmd) == 0 or die("Join failed\n$cmd");

	$ret->{SMBD_TEST_FIFO} = "$prefix/smbd_test.fifo";
	$ret->{SMBD_TEST_LOG} = "$prefix/smbd_test.log";
	$ret->{SMBD_TEST_LOG_POS} = 0;

	$ret->{DC_SERVER} = $dcvars->{SERVER};
	$ret->{DC_SERVER_IP} = $dcvars->{SERVER_IP};
	$ret->{DC_NETBIOSNAME} = $dcvars->{NETBIOSNAME};
	$ret->{DC_NETBIOSALIAS} = $dcvars->{NETBIOSALIAS};
	$ret->{DC_USERNAME} = $dcvars->{USERNAME};
	$ret->{DC_PASSWORD} = $dcvars->{PASSWORD};

	return $ret;
}

sub provision_dc($$)
{
	my ($self, $prefix) = @_;

	print "PROVISIONING DC...";
	my $ret = $self->provision($prefix,
				   "domain controller",
				   "localdc1",
				   "localdc",
				   1,
				   "localdcpass");

	$self->add_wins_config("$prefix/private") or 
		die("Unable to add wins configuration");

	$ret->{SMBD_TEST_FIFO} = "$prefix/smbd_test.fifo";
	$ret->{SMBD_TEST_LOG} = "$prefix/smbd_test.log";
	$ret->{SMBD_TEST_LOG_POS} = 0;
	return $ret;
}

sub teardown_env($$)
{
	my ($self, $envvars) = @_;
	my $pid;

	close(DATA);

	if (-f "$envvars->{PIDDIR}/smbd.pid" ) {
		open(IN, "<$envvars->{PIDDIR}/smbd.pid") or die("unable to open smbd pid file");
		$pid = <IN>;
		close(IN);

		# Give the process 20 seconds to exit.  gcov needs
		# this time to write out the covarge data
		my $count = 0;
		until (kill(0, $pid) == 0) {
		    # if no process sucessfully signalled, then we are done
		    sleep(1);
		    $count++;
		    last if $count > 20;
		}
		
		# If it is still around, kill it
		if ($count > 20) {
		    print "smbd process $pid took more than $count seconds to exit, killing\n";
		    kill 9, $pid;
		}
	}

	my $failed = $? >> 8;

	$self->slapd_stop($envvars) if ($self->{ldap});

	print $self->getlog_env($envvars);

	return $failed;
}

sub getlog_env($$)
{
	my ($self, $envvars) = @_;
	my $title = "SMBD LOG of: $envvars->{NETBIOSNAME}\n";
	my $out = $title;

	open(LOG, "<$envvars->{SMBD_TEST_LOG}");

	seek(LOG, $envvars->{SMBD_TEST_LOG_POS}, SEEK_SET);
	while (<LOG>) {
		$out .= $_;
	}
	$envvars->{SMBD_TEST_LOG_POS} = tell(LOG);
	close(LOG);

	return "" if $out eq $title;
 
	return $out;
}

sub check_env($$)
{
	my ($self, $envvars) = @_;

	return 1 if (-p $envvars->{SMBD_TEST_FIFO});

	print $self->getlog_env($envvars);

	return 0;
}

sub setup_env($$$)
{
	my ($self, $envname, $path) = @_;

	if ($envname eq "dc") {
		return $self->setup_dc("$path/dc");
	} elsif ($envname eq "member") {
		if (not defined($self->{vars}->{dc})) {
			$self->setup_dc("$path/dc");
		}
		return $self->setup_member("$path/member", $self->{vars}->{dc});
	} else {
		die("Samba4 can't provide environment '$envname'");
	}
}

sub setup_member($$$$)
{
	my ($self, $path, $dc_vars) = @_;

	my $env = $self->provision_member($path, $dc_vars);

	$self->check_or_start($env, ($ENV{SMBD_MAXTIME} or 7500));

	$self->wait_for_start($env);

	return $env;
}

sub setup_dc($$)
{
	my ($self, $path) = @_;

	my $env = $self->provision_dc($path);

	$self->check_or_start($env, 
		($ENV{SMBD_MAXTIME} or 7500));

	$self->wait_for_start($env);

	$self->{vars}->{dc} = $env;

	return $env;
}

sub stop($)
{
	my ($self) = @_;
}

1;
