#!/usr/bin/python

# Unix SMB/CIFS implementation.
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
from samba.provision import setup_secretsdb
import samba.tests
from ldb import Dn

setup_dir = "setup"
def setup_path(file):
    return os.path.join(setup_dir, file)


class ProvisionTestCase(samba.tests.TestCaseInTempDir):
    def test_setup_secretsdb(self):
        path = os.path.join(self.tempdir, "secrets.ldb")
        ldb = setup_secretsdb(path, setup_path, None, None, None)
        try:
            self.assertEquals("LSA Secrets",
                 ldb.searchone(Dn(ldb, "CN=LSA Secrets"), "CN"))
        finally:
            del ldb
            os.unlink(path)


class Disabled:
    def test_setup_templatesdb(self):
        raise NotImplementedError(self.test_setup_templatesdb)

    def test_setup_registry(self):
        raise NotImplementedError(self.test_setup_registry)

    def test_setup_samdb_rootdse(self):
        raise NotImplementedError(self.test_setup_samdb_rootdse)

    def test_setup_samdb_partitions(self):
        raise NotImplementedError(self.test_setup_samdb_partitions)

    def test_create_phpldapadmin_config(self):
        raise NotImplementedError(self.test_create_phpldapadmin_config)

    def test_provision_dns(self):
        raise NotImplementedError(self.test_provision_dns)

    def test_provision_ldapbase(self):
        raise NotImplementedError(self.test_provision_ldapbase)

    def test_provision_guess(self):
        raise NotImplementedError(self.test_provision_guess)

    def test_join_domain(self):
        raise NotImplementedError(self.test_join_domain)

    def test_vampire(self):
        raise NotImplementedError(self.test_vampire)

    def test_erase_partitions(self):
        raise NotImplementedError(self.test_erase_partitions)

