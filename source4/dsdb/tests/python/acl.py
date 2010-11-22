#!/usr/bin/env python
# -*- coding: utf-8 -*-
# This is unit with tests for LDAP access checks

import optparse
import sys
import base64
import re

sys.path.append("bin/python")
import samba
samba.ensure_external_module("testtools", "testtools")
samba.ensure_external_module("subunit", "subunit/python")

import samba.getopt as options

from ldb import (
    SCOPE_BASE, SCOPE_SUBTREE, LdbError, ERR_NO_SUCH_OBJECT,
    ERR_UNWILLING_TO_PERFORM, ERR_INSUFFICIENT_ACCESS_RIGHTS)
from ldb import ERR_CONSTRAINT_VIOLATION
from ldb import ERR_OPERATIONS_ERROR
from ldb import Message, MessageElement, Dn
from ldb import FLAG_MOD_REPLACE, FLAG_MOD_DELETE
from samba.ndr import ndr_pack, ndr_unpack
from samba.dcerpc import security

from samba.auth import system_session
from samba import gensec
from samba.samdb import SamDB
from samba.credentials import Credentials
import samba.tests
from subunit.run import SubunitTestRunner
import unittest

parser = optparse.OptionParser("acl.py [options] <host>")
sambaopts = options.SambaOptions(parser)
parser.add_option_group(sambaopts)
parser.add_option_group(options.VersionOptions(parser))

# use command line creds if available
credopts = options.CredentialsOptions(parser)
parser.add_option_group(credopts)
opts, args = parser.parse_args()

if len(args) < 1:
    parser.print_usage()
    sys.exit(1)

host = args[0]

lp = sambaopts.get_loadparm()
creds = credopts.get_credentials(lp)
creds.set_gensec_features(creds.get_gensec_features() | gensec.FEATURE_SEAL)

#
# Tests start here
#

class AclTests(samba.tests.TestCase):

    def delete_force(self, ldb, dn):
        try:
            ldb.delete(dn)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_NO_SUCH_OBJECT)

    def find_domain_sid(self, ldb):
        res = ldb.search(base=self.base_dn, expression="(objectClass=*)", scope=SCOPE_BASE)
        return ndr_unpack(security.dom_sid,res[0]["objectSid"][0])

    def setUp(self):
        super(AclTests, self).setUp()
        self.ldb_admin = ldb
        self.base_dn = ldb.domain_dn()
        self.domain_sid = self.find_domain_sid(self.ldb_admin)
        self.user_pass = "samba123@"
        self.configuration_dn = self.ldb_admin.get_config_basedn().get_linearized()
        print "baseDN: %s" % self.base_dn

    def get_user_dn(self, name):
        return "CN=%s,CN=Users,%s" % (name, self.base_dn)

    def modify_desc(self, object_dn, desc):
        """ Modify security descriptor using either SDDL string
            or security.descriptor object
        """
        assert(isinstance(desc, str) or isinstance(desc, security.descriptor))
        mod = """
dn: """ + object_dn + """
changetype: modify
replace: nTSecurityDescriptor
"""
        if isinstance(desc, str):
            mod += "nTSecurityDescriptor: %s" % desc
        elif isinstance(desc, security.descriptor):
            mod += "nTSecurityDescriptor:: %s" % base64.b64encode(ndr_pack(desc))
        self.ldb_admin.modify_ldif(mod)
    
    def create_ou(self, _ldb, ou_dn, desc=None):
        ldif = """
dn: """ + ou_dn + """
ou: """ + ou_dn.split(",")[0][3:] + """
objectClass: organizationalUnit
url: www.example.com
"""
        if desc:
            assert(isinstance(desc, str) or isinstance(desc, security.descriptor))
            if isinstance(desc, str):
                ldif += "nTSecurityDescriptor: %s" % desc
            elif isinstance(desc, security.descriptor):
                ldif += "nTSecurityDescriptor:: %s" % base64.b64encode(ndr_pack(desc))
        _ldb.add_ldif(ldif)

    def create_active_user(self, _ldb, user_dn):
        ldif = """
dn: """ + user_dn + """
sAMAccountName: """ + user_dn.split(",")[0][3:] + """
objectClass: user
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
url: www.example.com
"""
        _ldb.add_ldif(ldif)

    def create_test_user(self, _ldb, user_dn, desc=None):
        ldif = """
dn: """ + user_dn + """
sAMAccountName: """ + user_dn.split(",")[0][3:] + """
objectClass: user
userPassword: """ + self.user_pass + """
url: www.example.com
"""
        if desc:
            assert(isinstance(desc, str) or isinstance(desc, security.descriptor))
            if isinstance(desc, str):
                ldif += "nTSecurityDescriptor: %s" % desc
            elif isinstance(desc, security.descriptor):
                ldif += "nTSecurityDescriptor:: %s" % base64.b64encode(ndr_pack(desc))
        _ldb.add_ldif(ldif)

    def create_group(self, _ldb, group_dn, desc=None):
        ldif = """
dn: """ + group_dn + """
objectClass: group
sAMAccountName: """ + group_dn.split(",")[0][3:] + """
groupType: 4
url: www.example.com
"""
        if desc:
            assert(isinstance(desc, str) or isinstance(desc, security.descriptor))
            if isinstance(desc, str):
                ldif += "nTSecurityDescriptor: %s" % desc
            elif isinstance(desc, security.descriptor):
                ldif += "nTSecurityDescriptor:: %s" % base64.b64encode(ndr_pack(desc))
        _ldb.add_ldif(ldif)

    def create_security_group(self, _ldb, group_dn, desc=None):
        ldif = """
dn: """ + group_dn + """
objectClass: group
sAMAccountName: """ + group_dn.split(",")[0][3:] + """
groupType: -2147483646
url: www.example.com
"""
        if desc:
            assert(isinstance(desc, str) or isinstance(desc, security.descriptor))
            if isinstance(desc, str):
                ldif += "nTSecurityDescriptor: %s" % desc
            elif isinstance(desc, security.descriptor):
                ldif += "nTSecurityDescriptor:: %s" % base64.b64encode(ndr_pack(desc))
        _ldb.add_ldif(ldif)

    def read_desc(self, object_dn):
        res = self.ldb_admin.search(object_dn, SCOPE_BASE, None, ["nTSecurityDescriptor"])
        desc = res[0]["nTSecurityDescriptor"][0]
        return ndr_unpack(security.descriptor, desc)

    def get_ldb_connection(self, target_username, target_password):
        creds_tmp = Credentials()
        creds_tmp.set_username(target_username)
        creds_tmp.set_password(target_password)
        creds_tmp.set_domain(creds.get_domain())
        creds_tmp.set_realm(creds.get_realm())
        creds_tmp.set_workstation(creds.get_workstation())
        creds_tmp.set_gensec_features(creds_tmp.get_gensec_features()
                                      | gensec.FEATURE_SEAL)
        ldb_target = SamDB(url=host, credentials=creds_tmp, lp=lp)
        return ldb_target

    def get_object_sid(self, object_dn):
        res = self.ldb_admin.search(object_dn)
        return ndr_unpack(security.dom_sid, res[0]["objectSid"][0])

    def dacl_add_ace(self, object_dn, ace):
        desc = self.read_desc(object_dn)
        desc_sddl = desc.as_sddl(self.domain_sid)
        if ace in desc_sddl:
            return
        if desc_sddl.find("(") >= 0:
            desc_sddl = desc_sddl[:desc_sddl.index("(")] + ace + desc_sddl[desc_sddl.index("("):]
        else:
            desc_sddl = desc_sddl + ace
        self.modify_desc(object_dn, desc_sddl)

    def get_desc_sddl(self, object_dn):
        """ Return object nTSecutiryDescriptor in SDDL format
        """
        desc = self.read_desc(object_dn)
        return desc.as_sddl(self.domain_sid)

    # Test if we have any additional groups for users than default ones
    def assert_user_no_group_member(self, username):
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" % self.get_user_dn(username))
        try:
            self.assertEqual(res[0]["memberOf"][0], "")
        except KeyError:
            pass
        else:
            self.fail()
    
    def create_enable_user(self, username):
        self.create_active_user(self.ldb_admin, self.get_user_dn(username))
        self.ldb_admin.enable_account("(sAMAccountName=" + username + ")")

    def set_dsheuristics(self, dsheuristics):
        m = Message()
        m.dn = Dn(self.ldb_admin, "CN=Directory Service, CN=Windows NT, CN=Services, "
                  + self.configuration_dn)
        if dsheuristics is not None:
            m["dSHeuristics"] = MessageElement(dsheuristics, FLAG_MOD_REPLACE,
                                               "dSHeuristics")
        else:
            m["dSHeuristics"] = MessageElement([], FLAG_MOD_DELETE, "dsHeuristics")
        self.ldb_admin.modify(m)

#tests on ldap add operations
class AclAddTests(AclTests):

    def setUp(self):
        super(AclAddTests, self).setUp()
        # Domain admin that will be creator of OU parent-child structure
        self.usr_admin_owner = "acl_add_user1"
        # Second domain admin that will not be creator of OU parent-child structure
        self.usr_admin_not_owner = "acl_add_user2"
        # Regular user
        self.regular_user = "acl_add_user3"
        self.create_enable_user(self.usr_admin_owner)
        self.create_enable_user(self.usr_admin_not_owner)
        self.create_enable_user(self.regular_user)

        # add admins to the Domain Admins group
        self.ldb_admin.add_remove_group_members("Domain Admins", self.usr_admin_owner,
                       add_members_operation=True)
        self.ldb_admin.add_remove_group_members("Domain Admins", self.usr_admin_not_owner,
                       add_members_operation=True)

        self.ldb_owner = self.get_ldb_connection(self.usr_admin_owner, self.user_pass)
        self.ldb_notowner = self.get_ldb_connection(self.usr_admin_not_owner, self.user_pass)
        self.ldb_user = self.get_ldb_connection(self.regular_user, self.user_pass)

    def tearDown(self):
        super(AclAddTests, self).tearDown()
        self.delete_force(self.ldb_admin, "CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_add_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, self.get_user_dn(self.usr_admin_owner))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.usr_admin_not_owner))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.regular_user))

    # Make sure top OU is deleted (and so everything under it)
    def assert_top_ou_deleted(self):
        res = self.ldb_admin.search(self.base_dn,
            expression="(distinguishedName=%s,%s)" % (
                "OU=test_add_ou1", self.base_dn))
        self.assertEqual(res, [])

    def test_add_u1(self):
        """Testing OU with the rights of Doman Admin not creator of the OU """
        self.assert_top_ou_deleted()
        # Change descriptor for top level OU
        self.create_ou(self.ldb_owner, "OU=test_add_ou1," + self.base_dn)
        self.create_ou(self.ldb_owner, "OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        user_sid = self.get_object_sid(self.get_user_dn(self.usr_admin_not_owner))
        mod = "(D;CI;WPCC;;;%s)" % str(user_sid)
        self.dacl_add_ace("OU=test_add_ou1," + self.base_dn, mod)
        # Test user and group creation with another domain admin's credentials
        self.create_test_user(self.ldb_notowner, "CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.create_group(self.ldb_notowner, "CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        # Make sure we HAVE created the two objects -- user and group
        # !!! We should not be able to do that, but however beacuse of ACE ordering our inherited Deny ACE
        # !!! comes after explicit (A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA) that comes from somewhere
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s,%s)" % ("CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertTrue(len(res) > 0)
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s,%s)" % ("CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertTrue(len(res) > 0)

    def test_add_u2(self):
        """Testing OU with the regular user that has no rights granted over the OU """
        self.assert_top_ou_deleted()
        # Create a parent-child OU structure with domain admin credentials
        self.create_ou(self.ldb_owner, "OU=test_add_ou1," + self.base_dn)
        self.create_ou(self.ldb_owner, "OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        # Test user and group creation with regular user credentials
        try:
            self.create_test_user(self.ldb_user, "CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
            self.create_group(self.ldb_user, "CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()
        # Make sure we HAVEN'T created any of two objects -- user or group
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s,%s)" % ("CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s,%s)" % ("CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertEqual(res, [])

    def test_add_u3(self):
        """Testing OU with the rights of regular user granted the right 'Create User child objects' """
        self.assert_top_ou_deleted()
        # Change descriptor for top level OU
        self.create_ou(self.ldb_owner, "OU=test_add_ou1," + self.base_dn)
        user_sid = self.get_object_sid(self.get_user_dn(self.regular_user))
        mod = "(OA;CI;CC;bf967aba-0de6-11d0-a285-00aa003049e2;;%s)" % str(user_sid)
        self.dacl_add_ace("OU=test_add_ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_owner, "OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        # Test user and group creation with granted user only to one of the objects
        self.create_test_user(self.ldb_user, "CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        try:
            self.create_group(self.ldb_user, "CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()
        # Make sure we HAVE created the one of two objects -- user
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s,%s)" %
                ("CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1",
                    self.base_dn))
        self.assertNotEqual(len(res), 0)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s,%s)" %
                ("CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1",
                    self.base_dn) )
        self.assertEqual(res, [])

    def test_add_u4(self):
        """ 4 Testing OU with the rights of Doman Admin creator of the OU"""
        self.assert_top_ou_deleted()
        self.create_ou(self.ldb_owner, "OU=test_add_ou1," + self.base_dn)
        self.create_ou(self.ldb_owner, "OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.create_test_user(self.ldb_owner, "CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        self.create_group(self.ldb_owner, "CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1," + self.base_dn)
        # Make sure we have successfully created the two objects -- user and group
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s,%s)" % ("CN=test_add_user1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertTrue(len(res) > 0)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s,%s)" % ("CN=test_add_group1,OU=test_add_ou2,OU=test_add_ou1", self.base_dn))
        self.assertTrue(len(res) > 0)

#tests on ldap modify operations
class AclModifyTests(AclTests):

    def setUp(self):
        super(AclModifyTests, self).setUp()
        self.user_with_wp = "acl_mod_user1"
        self.user_with_sm = "acl_mod_user2"
        self.user_with_group_sm = "acl_mod_user3"
        self.create_enable_user(self.user_with_wp)
        self.create_enable_user(self.user_with_sm)
        self.create_enable_user(self.user_with_group_sm)
        self.ldb_user = self.get_ldb_connection(self.user_with_wp, self.user_pass)
        self.ldb_user2 = self.get_ldb_connection(self.user_with_sm, self.user_pass)
        self.ldb_user3 = self.get_ldb_connection(self.user_with_group_sm, self.user_pass)
        self.user_sid = self.get_object_sid( self.get_user_dn(self.user_with_wp))
        self.create_group(self.ldb_admin, "CN=test_modify_group2,CN=Users," + self.base_dn)
        self.create_group(self.ldb_admin, "CN=test_modify_group3,CN=Users," + self.base_dn)
        self.create_test_user(self.ldb_admin, self.get_user_dn("test_modify_user2"))

    def tearDown(self):
        super(AclModifyTests, self).tearDown()
        self.delete_force(self.ldb_admin, self.get_user_dn("test_modify_user1"))
        self.delete_force(self.ldb_admin, "CN=test_modify_group1,CN=Users," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_modify_group2,CN=Users," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_modify_group3,CN=Users," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, self.get_user_dn(self.user_with_wp))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.user_with_sm))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.user_with_group_sm))
        self.delete_force(self.ldb_admin, self.get_user_dn("test_modify_user2"))

    def test_modify_u1(self):
        """5 Modify one attribute if you have DS_WRITE_PROPERTY for it"""
        mod = "(OA;;WP;bf967953-0de6-11d0-a285-00aa003049e2;;%s)" % str(self.user_sid)
        # First test object -- User
        print "Testing modify on User object"
        self.create_test_user(self.ldb_admin, self.get_user_dn("test_modify_user1"))
        self.dacl_add_ace(self.get_user_dn("test_modify_user1"), mod)
        ldif = """
dn: """ + self.get_user_dn("test_modify_user1") + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % self.get_user_dn("test_modify_user1"))
        self.assertEqual(res[0]["displayName"][0], "test_changed")
        # Second test object -- Group
        print "Testing modify on Group object"
        self.create_group(self.ldb_admin, "CN=test_modify_group1,CN=Users," + self.base_dn)
        self.dacl_add_ace("CN=test_modify_group1,CN=Users," + self.base_dn, mod)
        ldif = """
dn: CN=test_modify_group1,CN=Users,""" + self.base_dn + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" % str("CN=test_modify_group1,CN=Users," + self.base_dn))
        self.assertEqual(res[0]["displayName"][0], "test_changed")
        # Third test object -- Organizational Unit
        print "Testing modify on OU object"
        #self.delete_force(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        self.dacl_add_ace("OU=test_modify_ou1," + self.base_dn, mod)
        ldif = """
dn: OU=test_modify_ou1,""" + self.base_dn + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" % str("OU=test_modify_ou1," + self.base_dn))
        self.assertEqual(res[0]["displayName"][0], "test_changed")

    def test_modify_u2(self):
        """6 Modify two attributes as you have DS_WRITE_PROPERTY granted only for one of them"""
        mod = "(OA;;WP;bf967953-0de6-11d0-a285-00aa003049e2;;%s)" % str(self.user_sid)
        # First test object -- User
        print "Testing modify on User object"
        #self.delete_force(self.ldb_admin, self.get_user_dn("test_modify_user1"))
        self.create_test_user(self.ldb_admin, self.get_user_dn("test_modify_user1"))
        self.dacl_add_ace(self.get_user_dn("test_modify_user1"), mod)
        # Modify on attribute you have rights for
        ldif = """
dn: """ + self.get_user_dn("test_modify_user1") + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" %
                self.get_user_dn("test_modify_user1"))
        self.assertEqual(res[0]["displayName"][0], "test_changed")
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: """ + self.get_user_dn("test_modify_user1") + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()
        # Second test object -- Group
        print "Testing modify on Group object"
        self.create_group(self.ldb_admin, "CN=test_modify_group1,CN=Users," + self.base_dn)
        self.dacl_add_ace("CN=test_modify_group1,CN=Users," + self.base_dn, mod)
        ldif = """
dn: CN=test_modify_group1,CN=Users,""" + self.base_dn + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" %
                str("CN=test_modify_group1,CN=Users," + self.base_dn))
        self.assertEqual(res[0]["displayName"][0], "test_changed")
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: CN=test_modify_group1,CN=Users,""" + self.base_dn + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()
        # Second test object -- Organizational Unit
        print "Testing modify on OU object"
        self.create_ou(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        self.dacl_add_ace("OU=test_modify_ou1," + self.base_dn, mod)
        ldif = """
dn: OU=test_modify_ou1,""" + self.base_dn + """
changetype: modify
replace: displayName
displayName: test_changed"""
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % str("OU=test_modify_ou1,"
                    + self.base_dn))
        self.assertEqual(res[0]["displayName"][0], "test_changed")
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: OU=test_modify_ou1,""" + self.base_dn + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()

    def test_modify_u3(self):
        """7 Modify one attribute as you have no what so ever rights granted"""
        # First test object -- User
        print "Testing modify on User object"
        self.create_test_user(self.ldb_admin, self.get_user_dn("test_modify_user1"))
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: """ + self.get_user_dn("test_modify_user1") + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()

        # Second test object -- Group
        print "Testing modify on Group object"
        self.create_group(self.ldb_admin, "CN=test_modify_group1,CN=Users," + self.base_dn)
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: CN=test_modify_group1,CN=Users,""" + self.base_dn + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()

        # Second test object -- Organizational Unit
        print "Testing modify on OU object"
        #self.delete_force(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=test_modify_ou1," + self.base_dn)
        # Modify on attribute you do not have rights for granted
        ldif = """
dn: OU=test_modify_ou1,""" + self.base_dn + """
changetype: modify
replace: url
url: www.samba.org"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()


    def test_modify_u4(self):
        """11 Grant WP to PRINCIPAL_SELF and test modify"""
        ldif = """
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
add: adminDescription
adminDescription: blah blah blah"""
        try:
            self.ldb_user.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()

        mod = "(OA;;WP;bf967919-0de6-11d0-a285-00aa003049e2;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        # Modify on attribute you have rights for
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" \
                                    % self.get_user_dn(self.user_with_wp), attrs=["adminDescription"] )
        self.assertEqual(res[0]["adminDescription"][0], "blah blah blah")

    def test_modify_u5(self):
        """12 test self membership"""
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
add: Member
Member: """ +  self.get_user_dn(self.user_with_sm)
#the user has no rights granted, this should fail
        try:
            self.ldb_user2.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This 'modify' operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()

#grant self-membership, should be able to add himself
        user_sid = self.get_object_sid(self.get_user_dn(self.user_with_sm))
        mod = "(OA;;SW;bf9679c0-0de6-11d0-a285-00aa003049e2;;%s)" % str(user_sid)
        self.dacl_add_ace("CN=test_modify_group2,CN=Users," + self.base_dn, mod)
        self.ldb_user2.modify_ldif(ldif)
        res = self.ldb_admin.search( self.base_dn, expression="(distinguishedName=%s)" \
                                    % ("CN=test_modify_group2,CN=Users," + self.base_dn), attrs=["Member"])
        self.assertEqual(res[0]["Member"][0], self.get_user_dn(self.user_with_sm))
#but not other users
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
add: Member
Member: CN=test_modify_user2,CN=Users,""" + self.base_dn
        try:
            self.ldb_user2.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_modify_u6(self):
        """13 test self membership"""
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
add: Member
Member: """ +  self.get_user_dn(self.user_with_sm) + """
Member: CN=test_modify_user2,CN=Users,""" + self.base_dn

#grant self-membership, should be able to add himself  but not others at the same time
        user_sid = self.get_object_sid(self.get_user_dn(self.user_with_sm))
        mod = "(OA;;SW;bf9679c0-0de6-11d0-a285-00aa003049e2;;%s)" % str(user_sid)
        self.dacl_add_ace("CN=test_modify_group2,CN=Users," + self.base_dn, mod)
        try:
            self.ldb_user2.modify_ldif(ldif)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_modify_u7(self):
        """13 User with WP modifying Member"""
#a second user is given write property permission
        user_sid = self.get_object_sid(self.get_user_dn(self.user_with_wp))
        mod = "(A;;WP;;;%s)" % str(user_sid)
        self.dacl_add_ace("CN=test_modify_group2,CN=Users," + self.base_dn, mod)
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
add: Member
Member: """ +  self.get_user_dn(self.user_with_wp)
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search( self.base_dn, expression="(distinguishedName=%s)" \
                                    % ("CN=test_modify_group2,CN=Users," + self.base_dn), attrs=["Member"])
        self.assertEqual(res[0]["Member"][0], self.get_user_dn(self.user_with_wp))
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
delete: Member"""
        self.ldb_user.modify_ldif(ldif)
        ldif = """
dn: CN=test_modify_group2,CN=Users,""" + self.base_dn + """
changetype: modify
add: Member
Member: CN=test_modify_user2,CN=Users,""" + self.base_dn
        self.ldb_user.modify_ldif(ldif)
        res = self.ldb_admin.search( self.base_dn, expression="(distinguishedName=%s)" \
                                    % ("CN=test_modify_group2,CN=Users," + self.base_dn), attrs=["Member"])
        self.assertEqual(res[0]["Member"][0], "CN=test_modify_user2,CN=Users," + self.base_dn)

#enable these when we have search implemented
class AclSearchTests(AclTests):

    def setUp(self):
        super(AclSearchTests, self).setUp()
        self.u1 = "search_u1"
        self.u2 = "search_u2"
        self.u3 = "search_u3"
        self.group1 = "group1"
        self.creds_tmp = Credentials()
        self.creds_tmp.set_username("")
        self.creds_tmp.set_password("")
        self.creds_tmp.set_domain(creds.get_domain())
        self.creds_tmp.set_realm(creds.get_realm())
        self.creds_tmp.set_workstation(creds.get_workstation())
        self.anonymous = SamDB(url=host, credentials=self.creds_tmp, lp=lp);
        res = self.ldb_admin.search("CN=Directory Service, CN=Windows NT, CN=Services, "
                 + self.configuration_dn, scope=SCOPE_BASE, attrs=["dSHeuristics"])
        if "dSHeuristics" in res[0]:
            self.dsheuristics = res[0]["dSHeuristics"][0]
        else:
            self.dsheuristics = None
        self.create_enable_user(self.u1)
        self.create_enable_user(self.u2)
        self.create_enable_user(self.u3)
        self.create_security_group(self.ldb_admin, self.get_user_dn(self.group1))
        self.ldb_admin.add_remove_group_members(self.group1, self.u2,
                                                add_members_operation=True)
        self.ldb_user = self.get_ldb_connection(self.u1, self.user_pass)
        self.ldb_user2 = self.get_ldb_connection(self.u2, self.user_pass)
        self.ldb_user3 = self.get_ldb_connection(self.u3, self.user_pass)
        self.full_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                          Dn(self.ldb_admin,  "OU=ou1," + self.base_dn),
                          Dn(self.ldb_admin,  "OU=ou3,OU=ou2,OU=ou1," + self.base_dn),
                          Dn(self.ldb_admin,  "OU=ou4,OU=ou2,OU=ou1," + self.base_dn),
                          Dn(self.ldb_admin,  "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn),
                          Dn(self.ldb_admin,  "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn)]
        self.user_sid = self.get_object_sid(self.get_user_dn(self.u1))
        self.group_sid = self.get_object_sid(self.get_user_dn(self.group1))

    def create_clean_ou(self, object_dn):
        """ Base repeating setup for unittests to follow """
        res = self.ldb_admin.search(base=self.base_dn, scope=SCOPE_SUBTREE, \
                expression="distinguishedName=%s" % object_dn)
        # Make sure top testing OU has been deleted before starting the test
        self.assertEqual(res, [])
        self.create_ou(self.ldb_admin, object_dn)
        desc_sddl = self.get_desc_sddl(object_dn)
        # Make sure there are inheritable ACEs initially
        self.assertTrue("CI" in desc_sddl or "OI" in desc_sddl)
        # Find and remove all inherit ACEs
        res = re.findall("\(.*?\)", desc_sddl)
        res = [x for x in res if ("CI" in x) or ("OI" in x)]
        for x in res:
            desc_sddl = desc_sddl.replace(x, "")
        # Add flag 'protected' in both DACL and SACL so no inherit ACEs
        # can propagate from above
        # remove SACL, we are not interested
        desc_sddl = desc_sddl.replace(":AI", ":AIP")
        self.modify_desc(object_dn, desc_sddl)
        # Verify all inheritable ACEs are gone
        desc_sddl = self.get_desc_sddl(object_dn)
        self.assertFalse("CI" in desc_sddl)
        self.assertFalse("OI" in desc_sddl)

    def tearDown(self):
        super(AclSearchTests, self).tearDown()
        self.delete_force(self.ldb_admin, "OU=test_search_ou2,OU=test_search_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_search_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou4,OU=ou2,OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, self.get_user_dn("search_u1"))
        self.delete_force(self.ldb_admin, self.get_user_dn("search_u2"))
        self.delete_force(self.ldb_admin, self.get_user_dn("search_u3"))
        self.delete_force(self.ldb_admin, self.get_user_dn("group1"))

    def test_search_anonymous1(self):
        """Verify access of rootDSE with the correct request"""
        res = self.anonymous.search("", expression="(objectClass=*)", scope=SCOPE_BASE)
        self.assertEquals(len(res), 1)
        #verify some of the attributes
        #dont care about values
        self.assertTrue("ldapServiceName" in res[0])
        self.assertTrue("namingContexts" in res[0])
        self.assertTrue("isSynchronized" in res[0])
        self.assertTrue("dsServiceName" in res[0])
        self.assertTrue("supportedSASLMechanisms" in res[0])
        self.assertTrue("isGlobalCatalogReady" in res[0])
        self.assertTrue("domainControllerFunctionality" in res[0])
        self.assertTrue("serverName" in res[0])

    def test_search_anonymous2(self):
        """Make sure we cannot access anything else"""
        try:
            res = self.anonymous.search("", expression="(objectClass=*)", scope=SCOPE_SUBTREE)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_OPERATIONS_ERROR)
        else:
            self.fail()
        try:
            res = self.anonymous.search(self.base_dn, expression="(objectClass=*)", scope=SCOPE_SUBTREE)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_OPERATIONS_ERROR)
        else:
            self.fail()
        try:
            res = self.anonymous.search("CN=Configuration," + self.base_dn, expression="(objectClass=*)",
                                        scope=SCOPE_SUBTREE)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_OPERATIONS_ERROR)
        else:
            self.fail()

    def test_search_anonymous3(self):
        """Set dsHeuristics and repeat"""
        self.set_dsheuristics("0000002")
        self.create_ou(self.ldb_admin, "OU=test_search_ou1," + self.base_dn)
        mod = "(A;CI;LC;;;AN)"
        self.dacl_add_ace("OU=test_search_ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_admin, "OU=test_search_ou2,OU=test_search_ou1," + self.base_dn)
        res = self.anonymous.search("OU=test_search_ou2,OU=test_search_ou1," + self.base_dn,
                                    expression="(objectClass=*)", scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 1)
        self.assertTrue("dn" in res[0])
        self.assertTrue(res[0]["dn"] == Dn(self.ldb_admin,
                                           "OU=test_search_ou2,OU=test_search_ou1," + self.base_dn))
        res = self.anonymous.search("CN=Configuration," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 1)
        self.assertTrue("dn" in res[0])
        self.assertTrue(res[0]["dn"] == Dn(self.ldb_admin, self.configuration_dn))
        self.set_dsheuristics(self.dsheuristics)

    def test_search1(self):
        """Make sure users can see us if given LC to user and group"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        mod = "(A;;LC;;;%s)(A;;LC;;;%s)" % (str(self.user_sid), str(self.group_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        self.create_ou(self.ldb_admin, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        self.create_ou(self.ldb_admin, "OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        self.create_ou(self.ldb_admin, "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        self.create_ou(self.ldb_admin, "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)

        #regular users must see only ou1 and ou2
        res = self.ldb_user3.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 2)
        ok_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou1," + self.base_dn)]

        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

        #these users should see all ous
        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 6)
        res_list = [ x["dn"] for x in res if x["dn"] in self.full_list ]
        self.assertEquals(sorted(res_list), sorted(self.full_list))

        res = self.ldb_user2.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 6)
        res_list = [ x["dn"] for x in res if x["dn"] in self.full_list ]
        self.assertEquals(sorted(res_list), sorted(self.full_list))

    def test_search2(self):
        """Make sure users can't see us if access is explicitly denied"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=ou4,OU=ou2,OU=ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn)
        self.create_ou(self.ldb_admin, "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn)
        mod = "(D;;LC;;;%s)(D;;LC;;;%s)" % (str(self.user_sid), str(self.group_sid)) 
        self.dacl_add_ace("OU=ou2,OU=ou1," + self.base_dn, mod)
        res = self.ldb_user3.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        #this user should see all ous
        res_list = [ x["dn"] for x in res if x["dn"] in self.full_list ]
        self.assertEquals(sorted(res_list), sorted(self.full_list))

        #these users should see ou1, 2, 5 and 6 but not 3 and 4
        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        ok_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn)]
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

        res = self.ldb_user2.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 4)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

    def test_search3(self):
        """Make sure users can't see ous if access is explicitly denied - 2"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        mod = "(A;CI;LC;;;%s)(A;CI;LC;;;%s)" % (str(self.user_sid), str(self.group_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_admin, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_admin, "OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_admin, "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_admin, "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")

        print "Testing correct behavior on nonaccessible search base"
        try:
             self.ldb_user3.search("OU=ou3,OU=ou2,OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                   scope=SCOPE_BASE)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_NO_SUCH_OBJECT)
        else:
            self.fail()

        mod = "(D;;LC;;;%s)(D;;LC;;;%s)" % (str(self.user_sid), str(self.group_sid))
        self.dacl_add_ace("OU=ou2,OU=ou1," + self.base_dn, mod)

        ok_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou1," + self.base_dn)]

        res = self.ldb_user3.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

        ok_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn)]

        #should not see ou3 and ou4, but should see ou5 and ou6
        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 4)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

        res = self.ldb_user2.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 4)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

    def test_search4(self):
        """There is no difference in visibility if the user is also creator"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        mod = "(A;CI;CC;;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_user, "OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_user, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_user, "OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_user, "OU=ou5,OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")
        self.create_ou(self.ldb_user, "OU=ou6,OU=ou4,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")

        ok_list = [Dn(self.ldb_admin,  "OU=ou2,OU=ou1," + self.base_dn),
                   Dn(self.ldb_admin,  "OU=ou1," + self.base_dn)]
        res = self.ldb_user3.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 2)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 2)
        res_list = [ x["dn"] for x in res if x["dn"] in ok_list ]
        self.assertEquals(sorted(res_list), sorted(ok_list))

    def test_search5(self):
        """Make sure users can see only attributes they are allowed to see"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        mod = "(A;CI;LC;;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        # assert user can only see dn
        res = self.ldb_user.search("OU=ou2,OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        ok_list = ['dn']
        self.assertEquals(len(res), 1)
        res_list = res[0].keys()
        self.assertEquals(res_list, ok_list)

        res = self.ldb_user.search("OU=ou2,OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_BASE, attrs=["ou"])

        self.assertEquals(len(res), 1)
        res_list = res[0].keys()
        self.assertEquals(res_list, ok_list)

        #give read property on ou and assert user can only see dn and ou
        mod = "(OA;;RP;bf9679f0-0de6-11d0-a285-00aa003049e2;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.dacl_add_ace("OU=ou2,OU=ou1," + self.base_dn, mod)
        res = self.ldb_user.search("OU=ou2,OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)
        ok_list = ['dn', 'ou']
        self.assertEquals(len(res), 1)
        res_list = res[0].keys()
        self.assertEquals(sorted(res_list), sorted(ok_list))

        #give read property on Public Information and assert user can see ou and other members
        mod = "(OA;;RP;e48d0154-bcf8-11d1-8702-00c04fb96050;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.dacl_add_ace("OU=ou2,OU=ou1," + self.base_dn, mod)
        res = self.ldb_user.search("OU=ou2,OU=ou1," + self.base_dn, expression="(objectClass=*)",
                                    scope=SCOPE_SUBTREE)

        ok_list = ['dn', 'objectClass', 'ou', 'distinguishedName', 'name', 'objectGUID', 'objectCategory']
        res_list = res[0].keys()
        self.assertEquals(sorted(res_list), sorted(ok_list))

    def test_search6(self):
        """If an attribute that cannot be read is used in a filter, it is as if the attribute does not exist"""
        self.create_clean_ou("OU=ou1," + self.base_dn)
        mod = "(A;CI;LCCC;;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou1," + self.base_dn, mod)
        self.create_ou(self.ldb_admin, "OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)" + mod)
        self.create_ou(self.ldb_user, "OU=ou3,OU=ou2,OU=ou1," + self.base_dn,
                       "D:(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;DA)")

        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(ou=ou3)",
                                    scope=SCOPE_SUBTREE)
        #nothing should be returned as ou is not accessible
        self.assertEquals(res, [])

        #give read property on ou and assert user can only see dn and ou
        mod = "(OA;;RP;bf9679f0-0de6-11d0-a285-00aa003049e2;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou3,OU=ou2,OU=ou1," + self.base_dn, mod)
        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(ou=ou3)",
                                    scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 1)
        ok_list = ['dn', 'ou']
        res_list = res[0].keys()
        self.assertEquals(sorted(res_list), sorted(ok_list))

        #give read property on Public Information and assert user can see ou and other members
        mod = "(OA;;RP;e48d0154-bcf8-11d1-8702-00c04fb96050;;%s)" % (str(self.user_sid))
        self.dacl_add_ace("OU=ou2,OU=ou1," + self.base_dn, mod)
        res = self.ldb_user.search("OU=ou1," + self.base_dn, expression="(ou=ou2)",
                                   scope=SCOPE_SUBTREE)
        self.assertEquals(len(res), 1)
        ok_list = ['dn', 'objectClass', 'ou', 'distinguishedName', 'name', 'objectGUID', 'objectCategory']
        res_list = res[0].keys()
        self.assertEquals(sorted(res_list), sorted(ok_list))

#tests on ldap delete operations
class AclDeleteTests(AclTests):

    def setUp(self):
        super(AclDeleteTests, self).setUp()
        self.regular_user = "acl_delete_user1"
            # Create regular user
        self.create_enable_user(self.regular_user)
        self.ldb_user = self.get_ldb_connection(self.regular_user, self.user_pass)

    def tearDown(self):
        super(AclDeleteTests, self).tearDown()
        self.delete_force(self.ldb_admin, self.get_user_dn("test_delete_user1"))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.regular_user))

    def test_delete_u1(self):
        """User is prohibited by default to delete another User object"""
        # Create user that we try to delete
        self.create_test_user(self.ldb_admin, self.get_user_dn("test_delete_user1"))
        # Here delete User object should ALWAYS through exception
        try:
            self.ldb_user.delete(self.get_user_dn("test_delete_user1"))
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_delete_u2(self):
        """User's group has RIGHT_DELETE to another User object"""
        user_dn = self.get_user_dn("test_delete_user1")
        # Create user that we try to delete
        self.create_test_user(self.ldb_admin, user_dn)
        mod = "(A;;SD;;;AU)"
        self.dacl_add_ace(user_dn, mod)
        # Try to delete User object
        self.ldb_user.delete(user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])

    def test_delete_u3(self):
        """User indentified by SID has RIGHT_DELETE to another User object"""
        user_dn = self.get_user_dn("test_delete_user1")
        # Create user that we try to delete
        self.create_test_user(self.ldb_admin, user_dn)
        mod = "(A;;SD;;;%s)" % self.get_object_sid(self.get_user_dn(self.regular_user))
        self.dacl_add_ace(user_dn, mod)
        # Try to delete User object
        self.ldb_user.delete(user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])

#tests on ldap rename operations
class AclRenameTests(AclTests):

    def setUp(self):
        super(AclRenameTests, self).setUp()
        self.regular_user = "acl_rename_user1"

        # Create regular user
        self.create_enable_user(self.regular_user)
        self.ldb_user = self.get_ldb_connection(self.regular_user, self.user_pass)

    def tearDown(self):
        super(AclRenameTests, self).tearDown()
        # Rename OU3
        self.delete_force(self.ldb_admin, "CN=test_rename_user1,OU=test_rename_ou3,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user2,OU=test_rename_ou3,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user5,OU=test_rename_ou3,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_rename_ou3,OU=test_rename_ou2," + self.base_dn)
        # Rename OU2
        self.delete_force(self.ldb_admin, "CN=test_rename_user1,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user2,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user5,OU=test_rename_ou2," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_rename_ou2," + self.base_dn)
        # Rename OU1
        self.delete_force(self.ldb_admin, "CN=test_rename_user1,OU=test_rename_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user2,OU=test_rename_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "CN=test_rename_user5,OU=test_rename_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_rename_ou3,OU=test_rename_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "OU=test_rename_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, self.get_user_dn(self.regular_user))

    def test_rename_u1(self):
        """Regular user fails to rename 'User object' within single OU"""
        # Create OU structure
        self.create_ou(self.ldb_admin, "OU=test_rename_ou1," + self.base_dn)
        self.create_test_user(self.ldb_admin, "CN=test_rename_user1,OU=test_rename_ou1," + self.base_dn)
        try:
            self.ldb_user.rename("CN=test_rename_user1,OU=test_rename_ou1," + self.base_dn, \
                    "CN=test_rename_user5,OU=test_rename_ou1," + self.base_dn)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_rename_u2(self):
        """Grant WRITE_PROPERTY to AU so regular user can rename 'User object' within single OU"""
        ou_dn = "OU=test_rename_ou1," + self.base_dn
        user_dn = "CN=test_rename_user1," + ou_dn
        rename_user_dn = "CN=test_rename_user5," + ou_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou_dn)
        self.create_test_user(self.ldb_admin, user_dn)
        mod = "(A;;WP;;;AU)"
        self.dacl_add_ace(user_dn, mod)
        # Rename 'User object' having WP to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u3(self):
        """Test rename with rights granted to 'User object' SID"""
        ou_dn = "OU=test_rename_ou1," + self.base_dn
        user_dn = "CN=test_rename_user1," + ou_dn
        rename_user_dn = "CN=test_rename_user5," + ou_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou_dn)
        self.create_test_user(self.ldb_admin, user_dn)
        sid = self.get_object_sid(self.get_user_dn(self.regular_user))
        mod = "(A;;WP;;;%s)" % str(sid)
        self.dacl_add_ace(user_dn, mod)
        # Rename 'User object' having WP to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u4(self):
        """Rename 'User object' cross OU with WP, SD and CC right granted on reg. user to AU"""
        ou1_dn = "OU=test_rename_ou1," + self.base_dn
        ou2_dn = "OU=test_rename_ou2," + self.base_dn
        user_dn = "CN=test_rename_user2," + ou1_dn
        rename_user_dn = "CN=test_rename_user5," + ou2_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou1_dn)
        self.create_ou(self.ldb_admin, ou2_dn)
        self.create_test_user(self.ldb_admin, user_dn)
        mod = "(A;;WPSD;;;AU)"
        self.dacl_add_ace(user_dn, mod)
        mod = "(A;;CC;;;AU)"
        self.dacl_add_ace(ou2_dn, mod)
        # Rename 'User object' having SD and CC to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u5(self):
        """Test rename with rights granted to 'User object' SID"""
        ou1_dn = "OU=test_rename_ou1," + self.base_dn
        ou2_dn = "OU=test_rename_ou2," + self.base_dn
        user_dn = "CN=test_rename_user2," + ou1_dn
        rename_user_dn = "CN=test_rename_user5," + ou2_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou1_dn)
        self.create_ou(self.ldb_admin, ou2_dn)
        self.create_test_user(self.ldb_admin, user_dn)
        sid = self.get_object_sid(self.get_user_dn(self.regular_user))
        mod = "(A;;WPSD;;;%s)" % str(sid)
        self.dacl_add_ace(user_dn, mod)
        mod = "(A;;CC;;;%s)" % str(sid)
        self.dacl_add_ace(ou2_dn, mod)
        # Rename 'User object' having SD and CC to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u6(self):
        """Rename 'User object' cross OU with WP, DC and CC right granted on OU & user to AU"""
        ou1_dn = "OU=test_rename_ou1," + self.base_dn
        ou2_dn = "OU=test_rename_ou2," + self.base_dn
        user_dn = "CN=test_rename_user2," + ou1_dn
        rename_user_dn = "CN=test_rename_user2," + ou2_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou1_dn)
        self.create_ou(self.ldb_admin, ou2_dn)
        #mod = "(A;CI;DCWP;;;AU)"
        mod = "(A;;DC;;;AU)"
        self.dacl_add_ace(ou1_dn, mod)
        mod = "(A;;CC;;;AU)"
        self.dacl_add_ace(ou2_dn, mod)
        self.create_test_user(self.ldb_admin, user_dn)
        mod = "(A;;WP;;;AU)"
        self.dacl_add_ace(user_dn, mod)
        # Rename 'User object' having SD and CC to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u7(self):
        """Rename 'User object' cross OU (second level) with WP, DC and CC right granted on OU to AU"""
        ou1_dn = "OU=test_rename_ou1," + self.base_dn
        ou2_dn = "OU=test_rename_ou2," + self.base_dn
        ou3_dn = "OU=test_rename_ou3," + ou2_dn
        user_dn = "CN=test_rename_user2," + ou1_dn
        rename_user_dn = "CN=test_rename_user5," + ou3_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou1_dn)
        self.create_ou(self.ldb_admin, ou2_dn)
        self.create_ou(self.ldb_admin, ou3_dn)
        mod = "(A;CI;WPDC;;;AU)"
        self.dacl_add_ace(ou1_dn, mod)
        mod = "(A;;CC;;;AU)"
        self.dacl_add_ace(ou3_dn, mod)
        self.create_test_user(self.ldb_admin, user_dn)
        # Rename 'User object' having SD and CC to AU
        self.ldb_user.rename(user_dn, rename_user_dn)
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % user_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn,
                expression="(distinguishedName=%s)" % rename_user_dn)
        self.assertNotEqual(res, [])

    def test_rename_u8(self):
        """Test rename on an object with and without modify access on the RDN attribute"""
        ou1_dn = "OU=test_rename_ou1," + self.base_dn
        ou2_dn = "OU=test_rename_ou2," + ou1_dn
        ou3_dn = "OU=test_rename_ou3," + ou1_dn
        # Create OU structure
        self.create_ou(self.ldb_admin, ou1_dn)
        self.create_ou(self.ldb_admin, ou2_dn)
        sid = self.get_object_sid(self.get_user_dn(self.regular_user))
        mod = "(OA;;WP;bf967a0e-0de6-11d0-a285-00aa003049e2;;%s)" % str(sid)
        self.dacl_add_ace(ou2_dn, mod)
        mod = "(OD;;WP;bf9679f0-0de6-11d0-a285-00aa003049e2;;%s)" % str(sid)
        self.dacl_add_ace(ou2_dn, mod)
        try:
            self.ldb_user.rename(ou2_dn, ou3_dn)
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            # This rename operation should always throw ERR_INSUFFICIENT_ACCESS_RIGHTS
            self.fail()
        sid = self.get_object_sid(self.get_user_dn(self.regular_user))
        mod = "(A;;WP;bf9679f0-0de6-11d0-a285-00aa003049e2;;%s)" % str(sid)
        self.dacl_add_ace(ou2_dn, mod)
        self.ldb_user.rename(ou2_dn, ou3_dn)
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" % ou2_dn)
        self.assertEqual(res, [])
        res = self.ldb_admin.search(self.base_dn, expression="(distinguishedName=%s)" % ou3_dn)
        self.assertNotEqual(res, [])

#tests on Control Access Rights
class AclCARTests(AclTests):

    def setUp(self):
        super(AclCARTests, self).setUp()
        self.user_with_wp = "acl_car_user1"
        self.user_with_pc = "acl_car_user2"
        self.create_enable_user(self.user_with_wp)
        self.create_enable_user(self.user_with_pc)
        self.ldb_user = self.get_ldb_connection(self.user_with_wp, self.user_pass)
        self.ldb_user2 = self.get_ldb_connection(self.user_with_pc, self.user_pass)

        res = self.ldb_admin.search("CN=Directory Service, CN=Windows NT, CN=Services, "
                 + self.configuration_dn, scope=SCOPE_BASE, attrs=["dSHeuristics"])
        if "dSHeuristics" in res[0]:
            self.dsheuristics = res[0]["dSHeuristics"][0]
        else:
            self.dsheuristics = None

        self.minPwdAge = self.ldb_admin.get_minPwdAge()

        # Set the "dSHeuristics" to have the tests run against Windows Server
        self.set_dsheuristics("000000001")
# Set minPwdAge to 0
        self.ldb_admin.set_minPwdAge("0")

    def tearDown(self):
        super(AclCARTests, self).tearDown()
        #restore original values
        self.set_dsheuristics(self.dsheuristics)
        self.ldb_admin.set_minPwdAge(self.minPwdAge)
        self.delete_force(self.ldb_admin, self.get_user_dn(self.user_with_wp))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.user_with_pc))

    def test_change_password1(self):
        """Try a password change operation without any CARs given"""
        #users have change password by default - remove for negative testing
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;WD)", "")
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;PS)", "")
        self.modify_desc(self.get_user_dn(self.user_with_wp), sddl)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: unicodePwd
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
add: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS2\"".encode('utf-16-le')) + """
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_CONSTRAINT_VIOLATION)
        else:
            # for some reason we get constraint violation instead of insufficient access error
            self.fail()

    def test_change_password2(self):
        """Make sure WP has no influence"""
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;WD)", "")
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;PS)", "")
        self.modify_desc(self.get_user_dn(self.user_with_wp), sddl)
        mod = "(A;;WP;;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: unicodePwd
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
add: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS2\"".encode('utf-16-le')) + """
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_CONSTRAINT_VIOLATION)
        else:
            # for some reason we get constraint violation instead of insufficient access error
            self.fail()

    def test_change_password3(self):
        """Make sure WP has no influence"""
        mod = "(D;;WP;;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: unicodePwd
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
add: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS2\"".encode('utf-16-le')) + """
""")

    def test_change_password5(self):
        """Make sure rights have no influence on dBCSPwd"""
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;WD)", "")
        sddl = sddl.replace("(OA;;CR;ab721a53-1e2f-11d0-9819-00aa0040529b;;PS)", "")
        self.modify_desc(self.get_user_dn(self.user_with_wp), sddl)
        mod = "(D;;WP;;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: dBCSPwd
dBCSPwd: XXXXXXXXXXXXXXXX
add: dBCSPwd
dBCSPwd: YYYYYYYYYYYYYYYY
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_UNWILLING_TO_PERFORM)
        else:
            self.fail()

    def test_change_password6(self):
        """Test uneven delete/adds"""
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: userPassword
userPassword: thatsAcomplPASS1
delete: userPassword
userPassword: thatsAcomplPASS1
add: userPassword
userPassword: thatsAcomplPASS2
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()
        mod = "(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: userPassword
userPassword: thatsAcomplPASS1
delete: userPassword
userPassword: thatsAcomplPASS1
add: userPassword
userPassword: thatsAcomplPASS2
""")
            # This fails on Windows 2000 domain level with constraint violation
        except LdbError, (num, _):
            self.assertTrue(num == ERR_CONSTRAINT_VIOLATION or
                            num == ERR_UNWILLING_TO_PERFORM)
        else:
            self.fail()


    def test_change_password7(self):
        """Try a password change operation without any CARs given"""
        #users have change password by default - remove for negative testing
        desc = self.read_desc(self.get_user_dn(self.user_with_wp))
        sddl = desc.as_sddl(self.domain_sid)
        self.modify_desc(self.get_user_dn(self.user_with_wp), sddl)
        #first change our own password
        self.ldb_user2.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_pc) + """
changetype: modify
delete: unicodePwd
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
add: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS1\"".encode('utf-16-le')) + """
""")
        #then someone else's
        self.ldb_user2.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
delete: unicodePwd
unicodePwd:: """ + base64.b64encode("\"samba123@\"".encode('utf-16-le')) + """
add: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS2\"".encode('utf-16-le')) + """
""")

    def test_reset_password1(self):
        """Try a user password reset operation (unicodePwd) before and after granting CAR"""
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS1\"".encode('utf-16-le')) + """
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()
        mod = "(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS1\"".encode('utf-16-le')) + """
""")

    def test_reset_password2(self):
        """Try a user password reset operation (userPassword) before and after granting CAR"""
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: userPassword
userPassword: thatsAcomplPASS1
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()
        mod = "(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: userPassword
userPassword: thatsAcomplPASS1
""")
            # This fails on Windows 2000 domain level with constraint violation
        except LdbError, (num, _):
            self.assertEquals(num, ERR_CONSTRAINT_VIOLATION)

    def test_reset_password3(self):
        """Grant WP and see what happens (unicodePwd)"""
        mod = "(A;;WP;;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS1\"".encode('utf-16-le')) + """
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_reset_password4(self):
        """Grant WP and see what happens (userPassword)"""
        mod = "(A;;WP;;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: userPassword
userPassword: thatsAcomplPASS1
""")
        except LdbError, (num, _):
            self.assertEquals(num, ERR_INSUFFICIENT_ACCESS_RIGHTS)
        else:
            self.fail()

    def test_reset_password5(self):
        """Explicitly deny WP but grant CAR (unicodePwd)"""
        mod = "(D;;WP;;;PS)(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: unicodePwd
unicodePwd:: """ + base64.b64encode("\"thatsAcomplPASS1\"".encode('utf-16-le')) + """
""")

    def test_reset_password6(self):
        """Explicitly deny WP but grant CAR (userPassword)"""
        mod = "(D;;WP;;;PS)(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;PS)"
        self.dacl_add_ace(self.get_user_dn(self.user_with_wp), mod)
        try:
            self.ldb_user.modify_ldif("""
dn: """ + self.get_user_dn(self.user_with_wp) + """
changetype: modify
replace: userPassword
userPassword: thatsAcomplPASS1
""")
            # This fails on Windows 2000 domain level with constraint violation
        except LdbError, (num, _):
            self.assertEquals(num, ERR_CONSTRAINT_VIOLATION)

class AclExtendedTests(AclTests):

    def setUp(self):
        super(AclExtendedTests, self).setUp()
        #regular user, will be the creator
        self.u1 = "ext_u1"
        #regular user
        self.u2 = "ext_u2"
        #admin user
        self.u3 = "ext_u3"
        self.create_enable_user(self.u1)
        self.create_enable_user(self.u2)
        self.create_enable_user(self.u3)
        self.ldb_admin.add_remove_group_members("Domain Admins", self.u3,
                                                add_members_operation=True)
        self.ldb_user1 = self.get_ldb_connection(self.u1, self.user_pass)
        self.ldb_user2 = self.get_ldb_connection(self.u2, self.user_pass)
        self.ldb_user3 = self.get_ldb_connection(self.u3, self.user_pass)
        self.user_sid1 = self.get_object_sid(self.get_user_dn(self.u1))
        self.user_sid2 = self.get_object_sid(self.get_user_dn(self.u2))

    def tearDown(self):
        super(AclExtendedTests, self).tearDown()
        self.delete_force(self.ldb_admin, self.get_user_dn(self.u1))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.u2))
        self.delete_force(self.ldb_admin, self.get_user_dn(self.u3))
        self.delete_force(self.ldb_admin, "CN=ext_group1,OU=ext_ou1," + self.base_dn)
        self.delete_force(self.ldb_admin, "ou=ext_ou1," + self.base_dn)

    def test_ntSecurityDescriptor(self):
        #create empty ou
        self.create_ou(self.ldb_admin, "ou=ext_ou1," + self.base_dn)
        #give u1 Create children access
        mod = "(A;;CC;;;%s)" % str(self.user_sid1)
        self.dacl_add_ace("OU=ext_ou1," + self.base_dn, mod)
        mod = "(A;;LC;;;%s)" % str(self.user_sid2)
        self.dacl_add_ace("OU=ext_ou1," + self.base_dn, mod)
        #create a group under that, grant RP to u2
        self.create_group(self.ldb_user1, "CN=ext_group1,OU=ext_ou1," + self.base_dn)
        mod = "(A;;RP;;;%s)" % str(self.user_sid2)
        self.dacl_add_ace("CN=ext_group1,OU=ext_ou1," + self.base_dn, mod)
        #u2 must not read the descriptor
        res = self.ldb_user2.search("CN=ext_group1,OU=ext_ou1," + self.base_dn,
                                    SCOPE_BASE, None, ["nTSecurityDescriptor"])
        self.assertNotEqual(res,[])
        self.assertFalse("nTSecurityDescriptor" in res[0].keys())
        #grant RC to u2 - still no access
        mod = "(A;;RC;;;%s)" % str(self.user_sid2)
        self.dacl_add_ace("CN=ext_group1,OU=ext_ou1," + self.base_dn, mod)
        res = self.ldb_user2.search("CN=ext_group1,OU=ext_ou1," + self.base_dn,
                                    SCOPE_BASE, None, ["nTSecurityDescriptor"])
        self.assertNotEqual(res,[])
        self.assertFalse("nTSecurityDescriptor" in res[0].keys())
        #u3 is member of administrators group, should be able to read sd
        res = self.ldb_user3.search("CN=ext_group1,OU=ext_ou1," + self.base_dn,
                                    SCOPE_BASE, None, ["nTSecurityDescriptor"])
        self.assertEqual(len(res),1)
        self.assertTrue("nTSecurityDescriptor" in res[0].keys())

# Important unit running information

if not "://" in host:
    host = "ldap://%s" % host
ldb = SamDB(host, credentials=creds, session_info=system_session(), lp=lp)

runner = SubunitTestRunner()
rc = 0
if not runner.run(unittest.makeSuite(AclAddTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclModifyTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclDeleteTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclRenameTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclCARTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclSearchTests)).wasSuccessful():
    rc = 1
if not runner.run(unittest.makeSuite(AclExtendedTests)).wasSuccessful():
    rc = 1


sys.exit(rc)
