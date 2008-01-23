#!/usr/bin/python

# Samba-specific bits for optparse
# Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007
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

import optparse
from credentials import Credentials

class SambaOptions(optparse.OptionGroup):
    def __init__(self, parser):
        optparse.OptionGroup.__init__(self, parser, "Samba Common Options")
        self.add_option("-s", "--configfile", action="callback",
                        type=str, metavar="FILE", help="Configuration file", 
                        callback=self._load_configfile)
        self._configfile = None

    def get_loadparm_path(self):
        return self._configfile

    def _load_configfile(self, option, opt_str, arg, parser):
        self._configfile = arg

    def get_loadparm(self):
        import param
        lp = param.LoadParm()
        if self._configfile is None:
            lp.load_default()
        else:
            lp.load(self._configfile)
        return lp

class VersionOptions(optparse.OptionGroup):
    def __init__(self, parser):
        optparse.OptionGroup.__init__(self, parser, "Version Options")


class CredentialsOptions(optparse.OptionGroup):
    def __init__(self, parser):
        optparse.OptionGroup.__init__(self, parser, "Credentials Options")
        self.add_option("--simple-bind-dn", metavar="DN", action="callback",
                        callback=self._set_simple_bind_dn, type=str,
                        help="DN to use for a simple bind")
        self.add_option("--password", metavar="PASSWORD", action="callback",
                        help="Password", type=str, callback=self._set_password)
        self.add_option("-U", "--username", metavar="USERNAME", 
                        action="callback", type=str,
                        help="Username", callback=self._parse_username)
        self.add_option("-W", "--workgroup", metavar="WORKGROUP", 
                        action="callback", type=str,
                        help="Workgroup", callback=self._parse_workgroup)
        self.creds = Credentials()

    def _parse_username(self, option, opt_str, arg, parser):
        self.creds.parse_string(arg)

    def _parse_workgroup(self, option, opt_str, arg, parser):
        self.creds.set_domain(arg)

    def _set_password(self, option, opt_str, arg, parser):
        self.creds.set_password(arg)

    def _set_simple_bind_dn(self, option, opt_str, arg, parser):
        self.creds.set_bind_dn(arg)

    def get_credentials(self):
        return self.creds
