#!/usr/bin/python
#
#	Upgrade from Samba3
#	Copyright Jelmer Vernooij 2005-2007
#	Released under the GNU GPL v3 or later
#
import getopt
import optparse
import os, sys
import param
import samba
import samba.getopt as options
from auth import system_session

parser = optparse.OptionParser("upgrade [options] <libdir> <smbconf>")
sambaopts = options.SambaOptions(parser)
parser.add_option_group(sambaopts)
parser.add_option_group(options.VersionOptions(parser))
credopts = options.CredentialsOptions(parser)
parser.add_option_group(credopts)
parser.add_option("--setupdir", type="string", metavar="DIR", 
		help="directory with setup files")
parser.add_option("--realm", type="string", metavar="REALM", help="set realm")
parser.add_option("--quiet", help="Be quiet")
parser.add_option("--blank", 
		help="do not add users or groups, just the structure")
parser.add_option("--targetdir", type="string", metavar="DIR", 
		          help="Set target directory")

opts, args = parser.parse_args()

def message(text):
    """Print a message if quiet is not set."""
    if opts.quiet:
        print text

if len(args) < 1:
    parser.print_usage()
    sys.exit(1)
from samba.samba3 import Samba3
message("Reading Samba3 databases and smb.conf\n")
libdir = args[0]
if not os.path.isdir(libdir):
    print "error: %s is not a directory"
    sys.exit(1)
if len(args) > 1:
    smbconf = args[1]
else:
    smbconf = os.path.join(libdir, "smb.conf")
samba3 = Samba3(libdir, smbconf)

from samba.upgrade import upgrade_provision
from samba.provision import provision_paths_from_lp

message("Provisioning\n")

setup_dir = opts.setupdir
if setup_dir is None:
	setup_dir = "setup"

creds = credopts.get_credentials()
lp = sambaopts.get_loadparm()
if opts.targetdir is not None:
    if not os.path.exists(opts.targetdir):
        os.mkdir(opts.targetdir)
    lp.set("private dir", os.path.abspath(opts.targetdir))
    lp.set("lock dir", os.path.abspath(opts.targetdir))
paths = provision_paths_from_lp(lp, "")
paths.smbconf = sambaopts.get_loadparm_path()
upgrade_provision(samba3, setup_dir, message, credentials=creds, session_info=system_session(), 
                  lp=lp, paths=paths)
