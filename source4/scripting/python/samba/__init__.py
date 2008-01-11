#!/usr/bin/python

# Unix SMB/CIFS implementation.
# Copyright (C) Andrew Tridgell <tridge@samba.org> 2005
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

import os

def _in_source_tree():
    """Check whether the script is being run from the source dir. """
    return os.path.exists("%s/../../../samba4-skip" % os.path.dirname(__file__))


# When running, in-tree, make sure bin/python is in the PYTHONPATH
if _in_source_tree():
    import sys
    srcdir = "%s/../../.." % os.path.dirname(__file__)
    sys.path.append("%s/bin/python" % srcdir)
    default_ldb_modules_dir = "%s/bin/modules/ldb" % srcdir


import ldb
import credentials
import misc

class Ldb(ldb.Ldb):
    """Simple Samba-specific LDB subclass that takes care 
    of setting up the modules dir, credentials pointers, etc.
    
    Please note that this is intended to be for all Samba LDB files, 
    not necessarily the Sam database. For Sam-specific helper 
    functions see samdb.py.
    """
    def __init__(self, url=None, session_info=None, credentials=None, 
                 modules_dir=None, lp=None):
        """Open a Samba Ldb file. 

        :param url: Optional LDB URL to open
        :param session_info: Optional session information
        :param credentials: Optional credentials, defaults to anonymous.
        :param modules_dir: Modules directory, if not the default.
        :param lp: Loadparm object, optional.

        This is different from a regular Ldb file in that the Samba-specific
        modules-dir is used by default and that credentials and session_info 
        can be passed through (required by some modules).
        """
        super(Ldb, self).__init__()

        if modules_dir is not None:
            self.set_modules_dir(modules_dir)
        elif default_ldb_modules_dir is not None:
            self.set_modules_dir(default_ldb_modules_dir)

        if credentials is not None:
            self.set_credentials(self, credentials)

        if session_info is not None:
            self.set_session_info(self, session_info)

        assert misc.ldb_register_samba_handlers(self) == 0

        if lp is not None:
            self.set_loadparm(self, lp)

        def msg(l,text):
            print text
        #self.set_debug(msg)

        if url is not None:
            self.connect(url)


    set_credentials = misc.ldb_set_credentials
    set_session_info = misc.ldb_set_session_info
    set_loadparm = misc.ldb_set_loadparm

    def searchone(self, basedn, attribute, expression=None, 
                  scope=ldb.SCOPE_BASE):
        """Search for one attribute as a string.
        
        :param basedn: BaseDN for the search.
        :param attribute: Name of the attribute
        :param expression: Optional search expression.
        :param scope: Search scope (defaults to base).
        :return: Value of attribute as a string or None if it wasn't found.
        """
        res = self.search(basedn, scope, expression, [attribute])
        if len(res) != 1 or res[0][attribute] is None:
            return None
        values = set(res[0][attribute])
        assert len(values) == 1
        return values.pop()

    def erase(self):
        """Erase this ldb, removing all records."""
        # delete the specials
        for attr in ["@INDEXLIST", "@ATTRIBUTES", "@SUBCLASSES", "@MODULES", 
                     "@OPTIONS", "@PARTITION", "@KLUDGEACL"]:
            try:
                self.delete(attr)
            except ldb.LdbError, (LDB_ERR_NO_SUCH_OBJECT, _):
                # Ignore missing dn errors
                pass

        basedn = ""
        # and the rest
        for msg in self.search(basedn, ldb.SCOPE_SUBTREE, 
                "(&(|(objectclass=*)(distinguishedName=*))(!(distinguishedName=@BASEINFO)))", 
                ["dn"]):
            try:
                self.delete(msg.dn)
            except ldb.LdbError, (LDB_ERR_NO_SUCH_OBJECT, _):
                # Ignor eno such object errors
                pass

        res = self.search(basedn, ldb.SCOPE_SUBTREE, "(&(|(objectclass=*)(distinguishedName=*))(!(distinguisedName=@BASEINFO)))", ["dn"])
        assert len(res) == 0

    def erase_partitions(self):
        """Erase an ldb, removing all records."""
        res = self.search("", ldb.SCOPE_BASE, "(objectClass=*)", 
                         ["namingContexts"])
        assert len(res) == 1
        if not "namingContexts" in res[0]:
            return
        for basedn in res[0]["namingContexts"]:
            previous_remaining = 1
            current_remaining = 0

            k = 0
            while ++k < 10 and (previous_remaining != current_remaining):
                # and the rest
                res2 = self.search(basedn, ldb.SCOPE_SUBTREE, "(|(objectclass=*)(distinguishedName=*))", ["distinguishedName"])
                previous_remaining = current_remaining
                current_remaining = len(res2)
                for msg in res2:
                    self.delete(msg.dn)

    def load_ldif_file_add(self, ldif_path):
        """Load a LDIF file.

        :param ldif_path: Path to LDIF file.
        """
        self.add_ldif(open(ldif_path, 'r').read())

    def add_ldif(self, ldif):
        """Add data based on a LDIF string.

        :param ldif: LDIF text.
        """
        for changetype, msg in self.parse_ldif(ldif):
            assert changetype == ldb.CHANGETYPE_NONE
            self.add(msg)

    def modify_ldif(self, ldif):
        """Modify database based on a LDIF string.

        :param ldif: LDIF text.
        """
        for changetype, msg in self.parse_ldif(ldif):
            self.modify(msg)


def substitute_var(text, values):
    """substitute strings of the form ${NAME} in str, replacing
    with substitutions from subobj.
    
    :param text: Text in which to subsitute.
    :param values: Dictionary with keys and values.
    """

    for (name, value) in values.items():
        assert isinstance(name, str), "%r is not a string" % name
        assert isinstance(value, str), "Value %r for %s is not a string" % (value, name)
        text = text.replace("${%s}" % name, value)

    return text


def valid_netbios_name(name):
    """Check whether a name is valid as a NetBIOS name. """
    # FIXME: There are probably more constraints here. 
    # crh has a paragraph on this in his book (1.4.1.1)
    if len(name) > 13:
        return False
    return True

version = misc.version
