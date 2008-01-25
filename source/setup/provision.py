#!/usr/bin/python
#
# Unix SMB/CIFS implementation.
# provision a Samba4 server
# Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007-2008
# Copyright (C) Andrew Bartlett <abartlet@samba.org> 2008
#
# Based on the original in EJS:
# Copyright (C) Andrew Tridgell 2005
#   
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#   
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#   
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import getopt
import optparse
import os, sys

# Add path to the library for in-tree use
sys.path.append("scripting/python")

import samba

from auth import system_session
import samba.getopt as options
import param
from samba.provision import (provision, 
                             provision_paths_from_lp,
                             FILL_FULL, FILL_NT4SYNC,
                             FILL_DRS)

parser = optparse.OptionParser("provision [options]")
sambaopts = options.SambaOptions(parser)
parser.add_option_group(sambaopts)
parser.add_option_group(options.VersionOptions(parser))
credopts = options.CredentialsOptions(parser)
parser.add_option_group(credopts)
parser.add_option("--setupdir", type="string", metavar="DIR", 
		help="directory with setup files")
parser.add_option("--realm", type="string", metavar="REALM", help="set realm")
parser.add_option("--domain", type="string", metavar="DOMAIN",
				  help="set domain")
parser.add_option("--domain-guid", type="string", metavar="GUID", 
		help="set domainguid (otherwise random)")
parser.add_option("--domain-sid", type="string", metavar="SID", 
		help="set domainsid (otherwise random)")
parser.add_option("--policy-guid", type="string", metavar="GUID",
				  help="set policy guid")
parser.add_option("--host-name", type="string", metavar="HOSTNAME", 
		help="set hostname")
parser.add_option("--host-ip", type="string", metavar="IPADDRESS", 
		help="set ipaddress")
parser.add_option("--host-guid", type="string", metavar="GUID", 
		help="set hostguid (otherwise random)")
parser.add_option("--invocationid", type="string", metavar="GUID", 
		help="set invocationid (otherwise random)")
parser.add_option("--adminpass", type="string", metavar="PASSWORD", 
		help="choose admin password (otherwise random)")
parser.add_option("--krbtgtpass", type="string", metavar="PASSWORD", 
		help="choose krbtgt password (otherwise random)")
parser.add_option("--machinepass", type="string", metavar="PASSWORD", 
		help="choose machine password (otherwise random)")
parser.add_option("--dnspass", type="string", metavar="PASSWORD", 
		help="choose dns password (otherwise random)")
parser.add_option("--root", type="string", metavar="USERNAME", 
		help="choose 'root' unix username")
parser.add_option("--nobody", type="string", metavar="USERNAME", 
		help="choose 'nobody' user")
parser.add_option("--nogroup", type="string", metavar="GROUPNAME", 
		help="choose 'nogroup' group")
parser.add_option("--wheel", type="string", metavar="GROUPNAME", 
		help="choose 'wheel' privileged group")
parser.add_option("--users", type="string", metavar="GROUPNAME", 
		help="choose 'users' group")
parser.add_option("--quiet", help="Be quiet", action="store_true")
parser.add_option("--blank", action="store_true",
		help="do not add users or groups, just the structure")
parser.add_option("--ldap-backend", type="string", metavar="LDAPSERVER", 
		help="LDAP server to use for this provision")
parser.add_option("--ldap-backend-type", type="choice", metavar="LDAP-BACKEND-TYPE", 
		help="LDB mapping module to use for the LDAP backend",
		choices=["fedora-ds", "openldap"])
parser.add_option("--aci", type="string", metavar="ACI", 
		help="An arbitary LDIF fragment, particularly useful to loading a backend ACI value into a target LDAP server. You must provide at least a realm and domain")
parser.add_option("--server-role", type="choice", metavar="ROLE",
		          choices=["domain controller", "member server"],
		help="Set server role to provision for (default standalone)")
parser.add_option("--partitions-only", 
		help="Configure Samba's partitions, but do not modify them (ie, join a BDC)", action="store_true")
parser.add_option("--targetdir", type="string", metavar="DIR", 
		          help="Set target directory")

opts = parser.parse_args()[0]

def message(text):
	"""print a message if quiet is not set."""
	if not opts.quiet:
		print text

if opts.realm is None or opts.domain is None:
	if opts.realm is None:
		print >>sys.stderr, "No realm set"
	if opts.domain is None:
		print >>sys.stderr, "No domain set"
	parser.print_usage()
	sys.exit(1)

# cope with an initially blank smb.conf 
private_dir = None
lp = sambaopts.get_loadparm()
if opts.targetdir is not None:
    if not os.path.exists(opts.targetdir):
        os.mkdir(opts.targetdir)
    private_dir = os.path.join(opts.targetdir, "private")
    if not os.path.exists(private_dir):
        os.mkdir(private_dir)
    lp.set("private dir", os.path.abspath(private_dir))
    lp.set("lock dir", os.path.abspath(opts.targetdir))
lp.set("realm", opts.realm)
lp.set("workgroup", opts.domain)
lp.set("server role", opts.server_role or "domain controller")


if opts.aci is not None:
	print "set ACI: %s" % opts.aci

paths = provision_paths_from_lp(lp, opts.realm.lower(), private_dir)
paths.smbconf = sambaopts.get_loadparm_path()

creds = credopts.get_credentials()

setup_dir = opts.setupdir
if setup_dir is None:
	setup_dir = "setup"

samdb_fill = FILL_FULL
if opts.blank:
    samdb_fill = FILL_NT4SYNC
elif opts.partitions_only:
    samdb_fill = FILL_DRS

provision(lp, setup_dir, message, paths, 
          system_session(), creds, opts.ldap_backend, 
          samdb_fill=samdb_fill, realm=opts.realm,
          domainguid=opts.domain_guid, domainsid=opts.domain_sid,
          policyguid=opts.policy_guid, hostname=opts.host_name,
          hostip=opts.host_ip, hostguid=opts.host_guid, 
          invocationid=opts.invocationid, adminpass=opts.adminpass,
          krbtgtpass=opts.krbtgtpass, machinepass=opts.machinepass,
          dnspass=opts.dnspass, root=opts.root, nobody=opts.nobody,
          nogroup=opts.nogroup, wheel=opts.wheel, users=opts.users,
          aci=opts.aci, serverrole=opts.server_role, 
          ldap_backend=opts.ldap_backend, 
          ldap_backend_type=opts.ldap_backend_type)

message("To reproduce this provision, run with:")
def shell_escape(arg):
    if " " in arg:
        return '"%s"' % arg
    return arg
message(" ".join([shell_escape(arg) for arg in sys.argv]))

message("All OK")
