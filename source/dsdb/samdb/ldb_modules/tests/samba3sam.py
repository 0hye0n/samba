#!/usr/bin/python

# Unix SMB/CIFS implementation.
# Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2005-2007
# Copyright (C) Martin Kuehl <mkhl@samba.org> 2006
#
# This is a Python port of the original in testprogs/ejs/samba3sam.js
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
import sys
import samba
import ldb
from samba import Ldb, substitute_var
from samba.tests import LdbTestCase, TestCaseInTempDir

datadir = os.path.join(os.path.dirname(__file__), "../../../../../testdata/samba3")

class Samba3SamTestCase(TestCaseInTempDir):
    def setup_data(self, obj, ldif):
        self.assertTrue(ldif is not None)
        obj.db.add_ldif(substitute_var(ldif, obj.substvars))

    def setup_modules(self, ldb, s3, s4):

        ldif = """
dn: @MAP=samba3sam
@FROM: """ + s4.basedn + """
@TO: sambaDomainName=TESTS,""" + s3.basedn + """

dn: @MODULES
@LIST: rootdse,paged_results,server_sort,extended_dn,asq,samldb,password_hash,operational,objectguid,rdn_name,samba3sam,partition

dn: @PARTITION
partition: """ + s4.basedn + ":" + s4.url + """
partition: """ + s3.basedn + ":" + s3.url + """
replicateEntries: @SUBCLASSES
replicateEntries: @ATTRIBUTES
replicateEntries: @INDEXLIST
"""
        ldb.add_ldif(ldif)

    def _test_s3sam_search(self, ldb):
        print "Looking up by non-mapped attribute"
        msg = ldb.search(expression="(cn=Administrator)")
        self.assertEquals(len(msg), 1)
        self.assertEquals(msg[0]["cn"], "Administrator")

        print "Looking up by mapped attribute"
        msg = ldb.search(expression="(name=Backup Operators)")
        self.assertEquals(len(msg), 1)
        self.assertEquals(msg[0]["name"], "Backup Operators")

        print "Looking up by old name of renamed attribute"
        msg = ldb.search(expression="(displayName=Backup Operators)")
        self.assertEquals(len(msg), 0)

        print "Looking up mapped entry containing SID"
        msg = ldb.search(expression="(cn=Replicator)")
        self.assertEquals(len(msg), 1)
        print msg[0].dn
        self.assertEquals(str(msg[0].dn), "cn=Replicator,ou=Groups,dc=vernstok,dc=nl")
        self.assertEquals(msg[0]["objectSid"], "S-1-5-21-4231626423-2410014848-2360679739-552")

        print "Checking mapping of objectClass"
        oc = set(msg[0]["objectClass"])
        self.assertTrue(oc is not None)
        for i in oc:
            self.assertEquals(oc[i] == "posixGroup" or oc[i], "group")

        print "Looking up by objectClass"
        msg = ldb.search(expression="(|(objectClass=user)(cn=Administrator))")
        self.assertEquals(len(msg), 2)
        for i in range(len(msg)):
            self.assertEquals((str(msg[i].dn), "unixName=Administrator,ou=Users,dc=vernstok,dc=nl") or
                   (str(msg[i].dn) == "unixName=nobody,ou=Users,dc=vernstok,dc=nl"))


    def _test_s3sam_modify(ldb, s3):
        print "Adding a record that will be fallbacked"
        ldb.add_ldif("""
dn: cn=Foo
foo: bar
blah: Blie
cn: Foo
showInAdvancedViewOnly: TRUE
    """)

        print "Checking for existence of record (local)"
        # TODO: This record must be searched in the local database, which is currently only supported for base searches
        # msg = ldb.search(expression="(cn=Foo)", ['foo','blah','cn','showInAdvancedViewOnly')]
        # TODO: Actually, this version should work as well but doesn't...
        # 
        #    
        attrs =  ['foo','blah','cn','showInAdvancedViewOnly']
        msg = ldb.search(expression="(cn=Foo)", base="cn=Foo", scope=ldb.LDB_SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(msg), 1)
        self.assertEquals(msg[0]["showInAdvancedViewOnly"], "TRUE")
        self.assertEquals(msg[0]["foo"], "bar")
        self.assertEquals(msg[0]["blah"], "Blie")

        print "Adding record that will be mapped"
        ldb.add_ldif("""
dn: cn=Niemand,cn=Users,dc=vernstok,dc=nl
objectClass: user
unixName: bin
sambaUnicodePwd: geheim
cn: Niemand
""")

        print "Checking for existence of record (remote)"
        msg = ldb.search(expression="(unixName=bin)", attrs=['unixName','cn','dn', 'sambaUnicodePwd'])
        self.assertEquals(len(msg), 1)
        self.assertEquals(msg[0]["cn"], "Niemand")
        self.assertEquals(msg[0]["sambaUnicodePwd"], "geheim")

        print "Checking for existence of record (local && remote)"
        msg = ldb.search(expression="(&(unixName=bin)(sambaUnicodePwd=geheim))", 
                         attrs=['unixName','cn','dn', 'sambaUnicodePwd'])
        self.assertEquals(len(msg), 1)           # TODO: should check with more records
        self.assertEquals(msg[0]["cn"], "Niemand")
        self.assertEquals(msg[0]["unixName"], "bin")
        self.assertEquals(msg[0]["sambaUnicodePwd"], "geheim")

        print "Checking for existence of record (local || remote)"
        msg = ldb.search(expression="(|(unixName=bin)(sambaUnicodePwd=geheim))", 
                         attrs=['unixName','cn','dn', 'sambaUnicodePwd'])
        print "got " + len(msg) + " replies"
        self.assertEquals(len(msg), 1)        # TODO: should check with more records
        self.assertEquals(msg[0]["cn"], "Niemand")
        self.assertEquals(msg[0]["unixName"] == "bin" or msg[0]["sambaUnicodePwd"], "geheim")

        print "Checking for data in destination database"
        msg = s3.db.search("(cn=Niemand)")
        self.assertTrue(len(msg) >= 1)
        self.assertEquals(msg[0]["sambaSID"], "S-1-5-21-4231626423-2410014848-2360679739-2001")
        self.assertEquals(msg[0]["displayName"], "Niemand")

        print "Adding attribute..."
        ldb.modify_ldif("""
dn: cn=Niemand,cn=Users,dc=vernstok,dc=nl
changetype: modify
add: description
description: Blah
""")

        print "Checking whether changes are still there..."
        msg = ldb.search(expression="(cn=Niemand)")
        self.assertTrue(len(msg) >= 1)
        self.assertEquals(msg[0]["cn"], "Niemand")
        self.assertEquals(msg[0]["description"], "Blah")

        print "Modifying attribute..."
        ldb.modify_ldif("""
dn: cn=Niemand,cn=Users,dc=vernstok,dc=nl
changetype: modify
replace: description
description: Blie
""")

        print "Checking whether changes are still there..."
        msg = ldb.search(expression="(cn=Niemand)")
        self.assertTrue(len(msg) >= 1)
        self.assertEquals(msg[0]["description"], "Blie")

        print "Deleting attribute..."
        ldb.modify_ldif("""
dn: cn=Niemand,cn=Users,dc=vernstok,dc=nl
changetype: modify
delete: description
""")

        print "Checking whether changes are no longer there..."
        msg = ldb.search(expression="(cn=Niemand)")
        self.assertTrue(len(msg) >= 1)
        self.assertEquals(msg[0]["description"], undefined)

        print "Renaming record..."
        ldb.rename("cn=Niemand,cn=Users,dc=vernstok,dc=nl", "cn=Niemand2,cn=Users,dc=vernstok,dc=nl")

        print "Checking whether DN has changed..."
        msg = ldb.search(expression="(cn=Niemand2)")
        self.assertEquals(len(msg), 1)
        self.assertEquals(str(msg[0].dn), "cn=Niemand2,cn=Users,dc=vernstok,dc=nl")

        print "Deleting record..."
        ldb.delete("cn=Niemand2,cn=Users,dc=vernstok,dc=nl")

        print "Checking whether record is gone..."
        msg = ldb.search(expression="(cn=Niemand2)")
        self.assertEquals(len(msg), 0)

    def _test_map_search(self, ldb, s3, s4):
        print "Running search tests on mapped data"
        ldif = """
dn: """ + "sambaDomainName=TESTS,""" + s3.basedn + """
objectclass: sambaDomain
objectclass: top
sambaSID: S-1-5-21-4231626423-2410014848-2360679739
sambaNextRid: 2000
sambaDomainName: TESTS"""
        s3.db.add_ldif(substitute_var(ldif, s3.substvars))

        print "Add a set of split records"
        ldif = """
dn: """ + s4.dn("cn=X") + """
objectClass: user
cn: X
codePage: x
revision: x
dnsHostName: x
nextRid: y
lastLogon: x
description: x
objectSid: S-1-5-21-4231626423-2410014848-2360679739-552
primaryGroupID: 1-5-21-4231626423-2410014848-2360679739-512

dn: """ + s4.dn("cn=Y") + """
objectClass: top
cn: Y
codePage: x
revision: x
dnsHostName: y
nextRid: y
lastLogon: y
description: x

dn: """ + s4.dn("cn=Z") + """
objectClass: top
cn: Z
codePage: x
revision: y
dnsHostName: z
nextRid: y
lastLogon: z
description: y
"""

        ldb.add_ldif(substitute_var(ldif, s4.substvars))

        print "Add a set of remote records"

        ldif = """
dn: """ + s3.dn("cn=A") + """
objectClass: posixAccount
cn: A
sambaNextRid: x
sambaBadPasswordCount: x
sambaLogonTime: x
description: x
sambaSID: S-1-5-21-4231626423-2410014848-2360679739-552
sambaPrimaryGroupSID: S-1-5-21-4231626423-2410014848-2360679739-512

dn: """ + s3.dn("cn=B") + """
objectClass: top
cn:B
sambaNextRid: x
sambaBadPasswordCount: x
sambaLogonTime: y
description: x

dn: """ + s3.dn("cn=C") + """
objectClass: top
cn: C
sambaNextRid: x
sambaBadPasswordCount: y
sambaLogonTime: z
description: y
"""
        s3.add_ldif(substitute_var(ldif, s3.substvars))

        print "Testing search by DN"

        # Search remote record by local DN
        dn = s4.dn("cn=A")
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(str(res[0].dn)), dn)
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "x")

        # Search remote record by remote DN
        dn = s3.dn("cn=A")
        attrs = ["dnsHostName", "lastLogon", "sambaLogonTime"]
        res = s3.db.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(str(res[0].dn)), dn)
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], undefined)
        self.assertEquals(res[0]["sambaLogonTime"], "x")

        # Search split record by local DN
        dn = s4.dn("cn=X")
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(str(res[0].dn)), dn)
        self.assertEquals(res[0]["dnsHostName"], "x")
        self.assertEquals(res[0]["lastLogon"], "x")

        # Search split record by remote DN
        dn = s3.dn("cn=X")
        attrs = ["dnsHostName", "lastLogon", "sambaLogonTime"]
        res = s3.db.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(str(res[0].dn)), dn)
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], undefined)
        self.assertEquals(res[0]["sambaLogonTime"], "x")

        print "Testing search by attribute"

        # Search by ignored attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(revision=x)", scope=ldb.SCOPE_DEFAULT, attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(str(res[0].dn)), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(str(res[1].dn)), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")

        # Search by kept attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(description=y)", scope=ldb.SCOPE_DEFAULT, attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(str(res[0].dn)), s4.dn("cn=Z"))
        self.assertEquals(res[0]["dnsHostName"], "z")
        self.assertEquals(res[0]["lastLogon"], "z")
        self.assertEquals(str(str(res[1].dn)), s4.dn("cn=C"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "z")

        # Search by renamed attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(badPwdCount=x)", scope=ldb.SCOPE_DEFAULT, attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")

        # Search by converted attribute
        attrs = ["dnsHostName", "lastLogon", "objectSid"]
        # TODO:
        #   Using the SID directly in the parse tree leads to conversion
        #   errors, letting the search fail with no results.
        #res = ldb.search("(objectSid=S-1-5-21-4231626423-2410014848-2360679739-552)", NULL, ldb. SCOPE_DEFAULT, attrs)
        res = ldb.search(expression="(objectSid=*)", attrs=attrs)
        self.assertEquals(len(res), 3)
        self.assertEquals(str(res[0].dn), s4.dn("cn=X"))
        self.assertEquals(res[0]["dnsHostName"], "x")
        self.assertEquals(res[0]["lastLogon"], "x")
        self.assertEquals(res[0]["objectSid"], "S-1-5-21-4231626423-2410014848-2360679739-552")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(res[1]["objectSid"], "S-1-5-21-4231626423-2410014848-2360679739-552")

        # Search by generated attribute 
        # In most cases, this even works when the mapping is missing
        # a `convert_operator' by enumerating the remote db.
        attrs = ["dnsHostName", "lastLogon", "primaryGroupID"]
        res = ldb.search(expression="(primaryGroupID=512)", attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), s4.dn("cn=A"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "x")
        self.assertEquals(res[0]["primaryGroupID"], "512")

        # TODO: There should actually be two results, A and X.  The
        # primaryGroupID of X seems to get corrupted somewhere, and the
        # objectSid isn't available during the generation of remote (!) data,
        # which can be observed with the following search.  Also note that Xs
        # objectSid seems to be fine in the previous search for objectSid... */
        #res = ldb.search(expression="(primaryGroupID=*)", NULL, ldb. SCOPE_DEFAULT, attrs)
        #print len(res) + " results found"
        #for i in range(len(res)):
        #    for (obj in res[i]) {
        #        print obj + ": " + res[i][obj]
        #    }
        #    print "---"
        #    

        # Search by remote name of renamed attribute */
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(sambaBadPasswordCount=*)", attrs=attrs)
        self.assertEquals(len(res), 0)

        # Search by objectClass
        attrs = ["dnsHostName", "lastLogon", "objectClass"]
        res = ldb.search(expression="(objectClass=user)", attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=X"))
        self.assertEquals(res[0]["dnsHostName"], "x")
        self.assertEquals(res[0]["lastLogon"], "x")
        self.assertTrue(res[0]["objectClass"] is not None)
        self.assertEquals(res[0]["objectClass"][0], "user")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertTrue(res[1]["objectClass"] is not None)
        self.assertEquals(res[1]["objectClass"][0], "user")

        # Prove that the objectClass is actually used for the search
        res = ldb.search(expression="(|(objectClass=user)(badPwdCount=x))", attrs=attrs)
        self.assertEquals(len(res), 3)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertTrue(res[0]["objectClass"] is not None)
        for oc in set(res[0]["objectClass"]):
            self.assertEquals(oc, "user")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertTrue(res[1]["objectClass"] is not None)
        self.assertEquals(res[1]["objectClass"][0], "user")
        self.assertEquals(str(res[2].dn), s4.dn("cn=A"))
        self.assertEquals(res[2]["dnsHostName"], undefined)
        self.assertEquals(res[2]["lastLogon"], "x")
        self.assertTrue(res[2]["objectClass"] is not None)
        self.assertEquals(res[2]["objectClass"][0], "user")

        print "Testing search by parse tree"

        # Search by conjunction of local attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(&(codePage=x)(revision=x))", attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")

        # Search by conjunction of remote attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(&(lastLogon=x)(description=x))", attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=X"))
        self.assertEquals(res[0]["dnsHostName"], "x")
        self.assertEquals(res[0]["lastLogon"], "x")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        
        # Search by conjunction of local and remote attribute 
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(&(codePage=x)(description=x))", attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")

        # Search by conjunction of local and remote attribute w/o match
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(&(codePage=x)(nextRid=x))", attrs=attrs)
        self.assertEquals(len(res), 0)
        res = ldb.search(expression="(&(revision=x)(lastLogon=z))", attrs=attrs)
        self.assertEquals(len(res), 0)

        # Search by disjunction of local attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(|(revision=x)(dnsHostName=x))", attrs=attrs)
        self.assertEquals(len(res), 2)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")

        # Search by disjunction of remote attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(|(badPwdCount=x)(lastLogon=x))", attrs=attrs)
        self.assertEquals(len(res), 3)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=A"))
        self.assertEquals(res[2]["dnsHostName"], undefined)
        self.assertEquals(res[2]["lastLogon"], "x")

        # Search by disjunction of local and remote attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(|(revision=x)(lastLogon=y))", attrs=attrs)
        self.assertEquals(len(res), 3)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=B"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "y")
        self.assertEquals(str(res[2].dn), s4.dn("cn=X"))
        self.assertEquals(res[2]["dnsHostName"], "x")
        self.assertEquals(res[2]["lastLogon"], "x")

        # Search by disjunction of local and remote attribute w/o match
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(|(codePage=y)(nextRid=z))", attrs=attrs)
        self.assertEquals(len(res), 0)

        # Search by negated local attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(revision=x))", attrs=attrs)
        self.assertEquals(len(res), 5)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=Z"))
        self.assertEquals(res[2]["dnsHostName"], "z")
        self.assertEquals(res[2]["lastLogon"], "z")
        self.assertEquals(str(res[3].dn), s4.dn("cn=C"))
        self.assertEquals(res[3]["dnsHostName"], undefined)
        self.assertEquals(res[3]["lastLogon"], "z")

        # Search by negated remote attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(description=x))", attrs=attrs)
        self.assertEquals(len(res), 3)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Z"))
        self.assertEquals(res[0]["dnsHostName"], "z")
        self.assertEquals(res[0]["lastLogon"], "z")
        self.assertEquals(str(res[1].dn), s4.dn("cn=C"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "z")

        # Search by negated conjunction of local attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(&(codePage=x)(revision=x)))", attrs=attrs)
        self.assertEquals(len(res), 5)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=Z"))
        self.assertEquals(res[2]["dnsHostName"], "z")
        self.assertEquals(res[2]["lastLogon"], "z")
        self.assertEquals(str(res[3].dn), s4.dn("cn=C"))
        self.assertEquals(res[3]["dnsHostName"], undefined)
        self.assertEquals(res[3]["lastLogon"], "z")

        # Search by negated conjunction of remote attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(&(lastLogon=x)(description=x)))", attrs=attrs)
        self.assertEquals(len(res), 5)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=B"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "y")
        self.assertEquals(str(res[2].dn), s4.dn("cn=Z"))
        self.assertEquals(res[2]["dnsHostName"], "z")
        self.assertEquals(res[2]["lastLogon"], "z")
        self.assertEquals(str(res[3].dn), s4.dn("cn=C"))
        self.assertEquals(res[3]["dnsHostName"], undefined)
        self.assertEquals(res[3]["lastLogon"], "z")

        # Search by negated conjunction of local and remote attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(&(codePage=x)(description=x)))", attrs=attrs)
        self.assertEquals(len(res), 5)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=Z"))
        self.assertEquals(res[2]["dnsHostName"], "z")
        self.assertEquals(res[2]["lastLogon"], "z")
        self.assertEquals(str(res[3].dn), s4.dn("cn=C"))
        self.assertEquals(res[3]["dnsHostName"], undefined)
        self.assertEquals(res[3]["lastLogon"], "z")

        # Search by negated disjunction of local attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(|(revision=x)(dnsHostName=x)))", attrs=attrs)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=A"))
        self.assertEquals(res[1]["dnsHostName"], undefined)
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=Z"))
        self.assertEquals(res[2]["dnsHostName"], "z")
        self.assertEquals(res[2]["lastLogon"], "z")
        self.assertEquals(str(res[3].dn), s4.dn("cn=C"))
        self.assertEquals(res[3]["dnsHostName"], undefined)
        self.assertEquals(res[3]["lastLogon"], "z")

        # Search by negated disjunction of remote attributes
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(|(badPwdCount=x)(lastLogon=x)))", attrs=attrs)
        self.assertEquals(len(res), 4)
        self.assertEquals(str(res[0].dn), s4.dn("cn=Y"))
        self.assertEquals(res[0]["dnsHostName"], "y")
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=Z"))
        self.assertEquals(res[1]["dnsHostName"], "z")
        self.assertEquals(res[1]["lastLogon"], "z")
        self.assertEquals(str(res[2].dn), s4.dn("cn=C"))
        self.assertEquals(res[2]["dnsHostName"], undefined)
        self.assertEquals(res[2]["lastLogon"], "z")

        # Search by negated disjunction of local and remote attribute
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(!(|(revision=x)(lastLogon=y)))", attrs=attrs)
        self.assertEquals(len(res), 4)
        self.assertEquals(str(res[0].dn), s4.dn("cn=A"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "x")
        self.assertEquals(str(res[1].dn), s4.dn("cn=Z"))
        self.assertEquals(res[1]["dnsHostName"], "z")
        self.assertEquals(res[1]["lastLogon"], "z")
        self.assertEquals(str(res[2].dn), s4.dn("cn=C"))
        self.assertEquals(res[2]["dnsHostName"], undefined)
        self.assertEquals(res[2]["lastLogon"], "z")

        print "Search by complex parse tree"
        attrs = ["dnsHostName", "lastLogon"]
        res = ldb.search(expression="(|(&(revision=x)(dnsHostName=x))(!(&(description=x)(nextRid=y)))(badPwdCount=y))", attrs=attrs)
        self.assertEquals(len(res), 6)
        self.assertEquals(str(res[0].dn), s4.dn("cn=B"))
        self.assertEquals(res[0]["dnsHostName"], undefined)
        self.assertEquals(res[0]["lastLogon"], "y")
        self.assertEquals(str(res[1].dn), s4.dn("cn=X"))
        self.assertEquals(res[1]["dnsHostName"], "x")
        self.assertEquals(res[1]["lastLogon"], "x")
        self.assertEquals(str(res[2].dn), s4.dn("cn=A"))
        self.assertEquals(res[2]["dnsHostName"], undefined)
        self.assertEquals(res[2]["lastLogon"], "x")
        self.assertEquals(str(res[3].dn), s4.dn("cn=Z"))
        self.assertEquals(res[3]["dnsHostName"], "z")
        self.assertEquals(res[3]["lastLogon"], "z")
        self.assertEquals(str(res[4].dn), s4.dn("cn=C"))
        self.assertEquals(res[4]["dnsHostName"], undefined)
        self.assertEquals(res[4]["lastLogon"], "z")

        # Clean up
        dns = [s4.dn("cn=%s" % n) for n in ["A","B","C","X","Y","Z"]]
        for dn in dns:
            ldb.delete(dn)

    def _test_map_modify(self, ldb, s3, s4):
        print "Running modification tests on mapped data"

        print "Testing modification of local records"

        # Add local record
        dn = "cn=test,dc=idealx,dc=org"
        ldif = """
dn: """ + dn + """
cn: test
foo: bar
revision: 1
description: test
"""
        ldb.add_ldif(ldif)
        # Check it's there
        attrs = ["foo", "revision", "description"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["foo"], "bar")
        self.assertEquals(res[0]["revision"], "1")
        self.assertEquals(res[0]["description"], "test")
        # Check it's not in the local db
        res = s4.db.search("(cn=test)", NULL, ldb.SCOPE_DEFAULT, attrs)
        self.assertEquals(len(res), 0)
        # Check it's not in the remote db
        res = s3.db.search("(cn=test)", NULL, ldb.SCOPE_DEFAULT, attrs)
        self.assertEquals(len(res), 0)

        # Modify local record
        ldif = """
dn: """ + dn + """
replace: foo
foo: baz
replace: description
description: foo
"""
        ldb.modify_ldif(ldif)
        # Check in local db
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["foo"], "baz")
        self.assertEquals(res[0]["revision"], "1")
        self.assertEquals(res[0]["description"], "foo")

        # Rename local record
        dn2 = "cn=toast,dc=idealx,dc=org"
        ldb.rename(dn, dn2)
        # Check in local db
        res = ldb.search(dn2, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["foo"], "baz")
        self.assertEquals(res[0]["revision"], "1")
        self.assertEquals(res[0]["description"], "foo")

        # Delete local record
        ldb.delete(dn2)
        # Check it's gone
        res = ldb.search(dn2, scope=ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)

        print "Testing modification of remote records"

        # Add remote record
        dn = s4.dn("cn=test")
        dn2 = s3.dn("cn=test")
        ldif = """
dn: """ + dn2 + """
cn: test
description: foo
sambaBadPasswordCount: 3
sambaNextRid: 1001
"""
        s3.db.add_ldif(ldif)
        # Check it's there
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "foo")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "3")
        self.assertEquals(res[0]["sambaNextRid"], "1001")
        # Check in mapped db
        attrs = ["description", "badPwdCount", "nextRid"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "foo")
        self.assertEquals(res[0]["badPwdCount"], "3")
        self.assertEquals(res[0]["nextRid"], "1001")
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 0)

        # Modify remote data of remote record
        ldif = """
dn: """ + dn + """
replace: description
description: test
replace: badPwdCount
badPwdCount: 4
"""
        ldb.modify_ldif(ldif)
        # Check in mapped db
        attrs = ["description", "badPwdCount", "nextRid"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["badPwdCount"], "4")
        self.assertEquals(res[0]["nextRid"], "1001")
        # Check in remote db
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "4")
        self.assertEquals(res[0]["sambaNextRid"], "1001")

        # Rename remote record
        dn2 = s4.dn("cn=toast")
        ldb.rename(dn, dn2)
        # Check in mapped db
        dn = dn2
        attrs = ["description", "badPwdCount", "nextRid"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["badPwdCount"], "4")
        self.assertEquals(res[0]["nextRid"], "1001")
        # Check in remote db 
        dn2 = s3.dn("cn=toast")
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "4")
        self.assertEquals(res[0]["sambaNextRid"], "1001")

        # Delete remote record
        ldb.delete(dn)
        # Check in mapped db
        res = ldb.search(dn, scope=ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)
        # Check in remote db
        res = s3.db.search("", dn2, ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)

        # Add remote record (same as before)
        dn = s4.dn("cn=test")
        dn2 = s3.dn("cn=test")
        ldif = """
dn: """ + dn2 + """
cn: test
description: foo
sambaBadPasswordCount: 3
sambaNextRid: 1001
"""
        s3.db.add_ldif(ldif)

        # Modify local data of remote record
        ldif = """
dn: """ + dn + """
add: revision
revision: 1
replace: description
description: test
"""
        ldb.modify_ldif(ldif)
        # Check in mapped db
        attrs = ["revision", "description"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["revision"], "1")
        # Check in remote db
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["revision"], undefined)
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], undefined)
        self.assertEquals(res[0]["revision"], "1")

        # Delete (newly) split record
        ldb.delete(dn)

        print "Testing modification of split records"

        # Add split record
        dn = s4.dn("cn=test")
        dn2 = s3.dn("cn=test")
        ldif = """
dn: """ + dn + """
cn: test
description: foo
badPwdCount: 3
nextRid: 1001
revision: 1
"""
        ldb.add_ldif(ldif)
        # Check it's there
        attrs = ["description", "badPwdCount", "nextRid", "revision"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "foo")
        self.assertEquals(res[0]["badPwdCount"], "3")
        self.assertEquals(res[0]["nextRid"], "1001")
        self.assertEquals(res[0]["revision"], "1")
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], undefined)
        self.assertEquals(res[0]["badPwdCount"], undefined)
        self.assertEquals(res[0]["nextRid"], undefined)
        self.assertEquals(res[0]["revision"], "1")
        # Check in remote db
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid", "revision"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "foo")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "3")
        self.assertEquals(res[0]["sambaNextRid"], "1001")
        self.assertEquals(res[0]["revision"], undefined)

        # Modify of split record
        ldif = """
dn: """ + dn + """
replace: description
description: test
replace: badPwdCount
badPwdCount: 4
replace: revision
revision: 2
"""
        ldb.modify_ldif(ldif)
        # Check in mapped db
        attrs = ["description", "badPwdCount", "nextRid", "revision"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["badPwdCount"], "4")
        self.assertEquals(res[0]["nextRid"], "1001")
        self.assertEquals(res[0]["revision"], "2")
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], undefined)
        self.assertEquals(res[0]["badPwdCount"], undefined)
        self.assertEquals(res[0]["nextRid"], undefined)
        self.assertEquals(res[0]["revision"], "2")
        # Check in remote db
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid", "revision"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "4")
        self.assertEquals(res[0]["sambaNextRid"], "1001")
        self.assertEquals(res[0]["revision"], undefined)

        # Rename split record
        dn2 = s4.dn("cn=toast")
        ldb.rename(dn, dn2)
        # Check in mapped db
        dn = dn2
        attrs = ["description", "badPwdCount", "nextRid", "revision"]
        res = ldb.search(dn, scope=ldb.SCOPE_BASE, attrs=attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["badPwdCount"], "4")
        self.assertEquals(res[0]["nextRid"], "1001")
        self.assertEquals(res[0]["revision"], "2")
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn)
        self.assertEquals(res[0]["description"], undefined)
        self.assertEquals(res[0]["badPwdCount"], undefined)
        self.assertEquals(res[0]["nextRid"], undefined)
        self.assertEquals(res[0]["revision"], "2")
        # Check in remote db
        dn2 = s3.dn("cn=toast")
        attrs = ["description", "sambaBadPasswordCount", "sambaNextRid", "revision"]
        res = s3.db.search("", dn2, ldb.SCOPE_BASE, attrs)
        self.assertEquals(len(res), 1)
        self.assertEquals(str(res[0].dn), dn2)
        self.assertEquals(res[0]["description"], "test")
        self.assertEquals(res[0]["sambaBadPasswordCount"], "4")
        self.assertEquals(res[0]["sambaNextRid"], "1001")
        self.assertEquals(res[0]["revision"], undefined)

        # Delete split record
        ldb.delete(dn)
        # Check in mapped db
        res = ldb.search(dn, scope=ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)
        # Check in local db
        res = s4.db.search("", dn, ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)
        # Check in remote db
        res = s3.db.search("", dn2, ldb.SCOPE_BASE)
        self.assertEquals(len(res), 0)

    def setUp(self):
        super(Samba3SamTestCase, self).setUp()

        def make_dn(basedn, rdn):
            return rdn + ",sambaDomainName=TESTS," + basedn

        def make_s4dn(basedn, rdn):
            return rdn + "," + basedn

        self.ldbfile = os.path.join(self.tempdir, "test.ldb")
        self.ldburl = "tdb://" + self.ldbfile

        tempdir = self.tempdir
        print tempdir

        class Target:
            """Simple helper class that contains data for a specific SAM connection."""
            def __init__(self, file, basedn, dn):
                self.file = os.path.join(tempdir, file)
                self.url = "tdb://" + self.file
                self.basedn = basedn
                self.substvars = {"BASEDN": self.basedn}
                self.db = Ldb()
                self._dn = dn

            def dn(self, rdn):
                return self._dn(rdn, self.basedn)

            def connect(self):
                return self.db.connect(self.url)

        self.samba4 = Target("samba4.ldb", "dc=vernstok,dc=nl", make_s4dn)
        self.samba3 = Target("samba3.ldb", "cn=Samba3Sam", make_dn)
        self.templates = Target("templates.ldb", "cn=templates", None)

        self.samba3.connect()
        self.templates.connect()
        self.samba4.connect()

    def tearDown(self):
        os.unlink(self.ldbfile)
        os.unlink(self.samba3.file)
        os.unlink(self.templates.file)
        os.unlink(self.samba4.file)
        super(Samba3SamTestCase, self).tearDown()

    def test_s3sam(self):
        ldb = Ldb(self.ldburl)
        self.setup_data(self.samba3, open(os.path.join(datadir, "samba3.ldif"), 'r').read())
        self.setup_data(self.templates, open(os.path.join(datadir, "provision_samba3sam_templates.ldif"), 'r').read())
        ldif = open(os.path.join(datadir, "provision_samba3sam.ldif"), 'r').read()
        ldb.add_ldif(substitute_var(ldif, self.samba4.substvars))
        self.setup_modules(ldb, self.samba3, self.samba4)

        ldb = Ldb(self.ldburl)

        self._test_s3sam_search(ldb)
        self._test_s3sam_modify(ldb, self.samba3)

    def test_map(self):
        ldb = Ldb(self.ldburl)
        self.setup_data(self.templates, open(os.path.join(datadir, "provision_samba3sam_templates.ldif"), 'r').read())
        ldif = open(os.path.join(datadir, "provision_samba3sam.ldif"), 'r').read()
        ldb.add_ldif(substitute_var(ldif, self.samba4.substvars))
        self.setup_modules(ldb, self.samba3, self.samba4)

        ldb = Ldb(self.ldburl)
        self._test_map_search(ldb, self.samba3, self.samba4)
        self._test_map_modify(ldb, self.samba3, self.samba4)

