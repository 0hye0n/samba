#!/usr/bin/python
# This is a port of the original in testprogs/ejs/ldap.js

import sys

if len(sys.argv) < 2:
    print "Usage: %s <HOST>" % sys.argv[0]
    sys.exit(1)

host = sys.argv[1]

def assertEquals(a1, a2):
    assert a1 == a2

def basic_tests(ldb, gc_ldb, base_dn, configuration_dn, schema_dn):
	print "Running basic tests"

	ldb.delete("cn=ldaptestuser,cn=users," + base_dn)
	ldb.delete("cn=ldaptestgroup,cn=users," + base_dn)

	print "Testing group add with invalid member"
	ok = ldb.add({
        "dn": "cn=ldaptestgroup,cn=uSers," + base_dn,
        "objectclass": "group",
        "member": "cn=ldaptestuser,cn=useRs," + base_dn})

	if (ok.error != 32) { # LDAP_NO_SUCH_OBJECT
		print ok.errstr
		assertEquals(ok.error, 32)
	}

	print "Testing user add"
	ok = ldb.add({
        "dn": "cn=ldaptestuser,cn=uSers," + base_dn,
        "objectclass": ["user", "person"],
        "cN": "LDAPtestUSER",
        "givenname": "ldap",
        "sn": "testy"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestuser,cn=users," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
		ok = ldb.add({
            "dn": "cn=ldaptestuser,cn=uSers," + base_dn,
            "objectclass": ["user", "person"],
            "cN": "LDAPtestUSER",
            "givenname": "ldap",
            "sn": "testy"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	ok = ldb.add({
        "dn": "cn=ldaptestgroup,cn=uSers," + base_dn,
        "objectclass": "group",
        "member": "cn=ldaptestuser,cn=useRs," + base_dn})
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.add({
        "dn": "cn=ldaptestcomputer,cn=computers," + base_dn,
        "objectclass": "computer",
        "cN": "LDAPtestCOMPUTER"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestcomputer,cn=computers," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
		ok = ldb.add({
            "dn": "cn=ldaptestcomputer,cn=computers," + base_dn,
            "objectClass": "computer",
            "cn": "LDAPtestCOMPUTER"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.add({"dn": "cn=ldaptest2computer,cn=computers," + base_dn,
        "objectClass": "computer",
        "cn": "LDAPtest2COMPUTER",
        "userAccountControl": "4096",
        "displayname": "ldap testy"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptest2computer,cn=computers," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
		ok = ldb.add({
            "dn": "cn=ldaptest2computer,cn=computers," + base_dn,
            "objectClass": "computer",
            "cn": "LDAPtest2COMPUTER",
            "userAccountControl": "4096",
            "displayname": "ldap testy"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	    print "Testing attribute or value exists behaviour"
	    ok = ldb.modify("
dn: cn=ldaptest2computer,cn=computers," + base_dn + "
changetype: modify
replace: servicePrincipalName
servicePrincipalName: host/ldaptest2computer
servicePrincipalName: host/ldaptest2computer
servicePrincipalName: cifs/ldaptest2computer
")

#LDB_ERR_ATTRIBUTE_OR_VALUE_EXISTS
		if (ok.error != 20) {
			print "Expected error LDB_ERR_ATTRIBUTE_OR_VALUE_EXISTS, got :" + ok.errstr
			assertEquals(ok.error, 20)
		}

	    ok = ldb.modify("
dn: cn=ldaptest2computer,cn=computers," + base_dn + "
changetype: modify
replace: servicePrincipalName
servicePrincipalName: host/ldaptest2computer
servicePrincipalName: cifs/ldaptest2computer
")

	    if (ok.error != 0) {
		    print "Failed to replace servicePrincpalName:" + ok.errstr
		    assertEquals(ok.error, 20)
	    }

	    ok = ldb.modify("
dn: cn=ldaptest2computer,cn=computers," + base_dn + "
changetype: modify
add: servicePrincipalName
servicePrincipalName: host/ldaptest2computer
")

#LDB_ERR_ATTRIBUTE_OR_VALUE_EXISTS
	    if (ok.error != 20) {
		    print "Expected error LDB_ERR_ATTRIBUTE_OR_VALUE_EXISTS, got :" + ok.errstr
			assertEquals(ok.error, 20)
	    }
	    
	    print "Testing ranged results"
	    ok = ldb.modify("
dn: cn=ldaptest2computer,cn=computers," + base_dn + "
changetype: modify
replace: servicePrincipalName
")
	    if (ok.error != 0) {
		    print "Failed to replace servicePrincpalName:" + ok.errstr
		    assertEquals(ok.error, 0)
	    }
	    
	    ok = ldb.modify("
dn: cn=ldaptest2computer,cn=computers," + base_dn + "
changetype: modify
add: servicePrincipalName
servicePrincipalName: host/ldaptest2computer0
servicePrincipalName: host/ldaptest2computer1
servicePrincipalName: host/ldaptest2computer2
servicePrincipalName: host/ldaptest2computer3
servicePrincipalName: host/ldaptest2computer4
servicePrincipalName: host/ldaptest2computer5
servicePrincipalName: host/ldaptest2computer6
servicePrincipalName: host/ldaptest2computer7
servicePrincipalName: host/ldaptest2computer8
servicePrincipalName: host/ldaptest2computer9
servicePrincipalName: host/ldaptest2computer10
servicePrincipalName: host/ldaptest2computer11
servicePrincipalName: host/ldaptest2computer12
servicePrincipalName: host/ldaptest2computer13
servicePrincipalName: host/ldaptest2computer14
servicePrincipalName: host/ldaptest2computer15
servicePrincipalName: host/ldaptest2computer16
servicePrincipalName: host/ldaptest2computer17
servicePrincipalName: host/ldaptest2computer18
servicePrincipalName: host/ldaptest2computer19
servicePrincipalName: host/ldaptest2computer20
servicePrincipalName: host/ldaptest2computer21
servicePrincipalName: host/ldaptest2computer22
servicePrincipalName: host/ldaptest2computer23
servicePrincipalName: host/ldaptest2computer24
servicePrincipalName: host/ldaptest2computer25
servicePrincipalName: host/ldaptest2computer26
servicePrincipalName: host/ldaptest2computer27
servicePrincipalName: host/ldaptest2computer28
servicePrincipalName: host/ldaptest2computer29
")

	    if (ok.error != 0) {
		    print "Failed to replace servicePrincpalName:" + ok.errstr
		    assertEquals(ok.error, 0)
	    }
	    
	    res = ldb.search(base_dn, expression="(cn=ldaptest2computer))", scope=ldb.SCOPE_SUBTREE, 
                         attrs=["servicePrincipalName;range=0-*"])
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
#	    print res[0]["servicePrincipalName;range=0-*"].length
	    assertEquals(res[0]["servicePrincipalName;range=0-*"].length, 30)

	    attrs = ["servicePrincipalName;range=0-19"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
#	    print res[0]["servicePrincipalName;range=0-19"].length
	    assertEquals(res[0]["servicePrincipalName;range=0-19"].length, 20)

	    attrs = ["servicePrincipalName;range=0-30"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=0-*"].length, 30)

	    attrs = ["servicePrincipalName;range=0-40"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=0-*"].length, 30)

	    attrs = ["servicePrincipalName;range=30-40"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=30-*"].length, 0)

	    attrs = ["servicePrincipalName;range=10-40"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=10-*"].length, 20)
#	    pos_11 = res[0]["servicePrincipalName;range=10-*"][18]

	    attrs = ["servicePrincipalName;range=11-40"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=11-*"].length, 19)
#	    print res[0]["servicePrincipalName;range=11-*"][18]
#	    print pos_11
#	    assertEquals((res[0]["servicePrincipalName;range=11-*"][18]), pos_11)

	    attrs = ["servicePrincipalName;range=11-15"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
	    assertEquals(res[0]["servicePrincipalName;range=11-15"].length, 5)
#	    assertEquals(res[0]["servicePrincipalName;range=11-15"][4], pos_11)

	    attrs = ["servicePrincipalName"]
	    res = ldb.search("(cn=ldaptest2computer))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	    if (res.error != 0 || len(res) != 1) {
		    print "Could not find (cn=ldaptest2computer)"
		    assertEquals(res.error, 0)
		    assertEquals(len(res), 1)
	    }
#	    print res[0]["servicePrincipalName"][18]
#	    print pos_11
	    assertEquals(res[0]["servicePrincipalName"].length, 30)
#	    assertEquals(res[0]["servicePrincipalName"][18], pos_11)

	    ok = ldb.add({
        "dn": "cn=ldaptestuser2,cn=useRs," + base_dn,
        "objectClass": ["person", "user"],
        "cn": "LDAPtestUSER2",
        "givenname": "testy",
        "sn": "ldap user2"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestuser2,cn=users," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	        ok = ldb.add({
                "dn": "cn=ldaptestuser2,cn=useRs," + base_dn,
                "objectClass": ["person", "user"],
                "cn": "LDAPtestUSER2",
                "givenname": "testy",
                "sn": "ldap user2"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}


	print "Testing Ambigious Name Resolution"
#   Testing ldb.search for (&(anr=ldap testy)(objectClass=user))
	res = ldb.search("(&(anr=ldap testy)(objectClass=user))")
	if (res.error != 0 || len(res) != 3) {
		print "Could not find (&(anr=ldap testy)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 3)
	}

#   Testing ldb.search for (&(anr=testy ldap)(objectClass=user))
	res = ldb.search("(&(anr=testy ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 2) {
		print "Found only " + len(res) + " for (&(anr=testy ldap)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 2)
	}

#   Testing ldb.search for (&(anr=ldap)(objectClass=user))
	res = ldb.search("(&(anr=ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 4) {
		print "Found only " + len(res) + " for (&(anr=ldap)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 4)
	} 

#   Testing ldb.search for (&(anr==ldap)(objectClass=user))
	res = ldb.search("(&(anr==ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Found only " + len(res) + " for (&(anr=ldap)(objectClass=user))"
		print "Could not find (&(anr==ldap)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser")
	assertEquals(res[0].name, "ldaptestuser")

#   Testing ldb.search for (&(anr=testy)(objectClass=user))
	res = ldb.search("(&(anr=testy)(objectClass=user))")
	if (res.error != 0 || len(res) != 2) {
		print "Found only " + len(res) + " for (&(anr=testy)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 2)
	}

#   Testing ldb.search for (&(anr=ldap testy)(objectClass=user))
	res = ldb.search("(&(anr=testy ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 2) {
		print "Found only " + len(res) + " for (&(anr=ldap testy)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 2)
	}

#   Testing ldb.search for (&(anr==ldap testy)(objectClass=user))
	res = ldb.search("(&(anr==testy ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Found only " + len(res) + " for (&(anr==ldap testy)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser")
	assertEquals(res[0].name, "ldaptestuser")

# Testing ldb.search for (&(anr==testy ldap)(objectClass=user))
	res = ldb.search("(&(anr==testy ldap)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(anr==testy ldap)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser")
	assertEquals(res[0].name, "ldaptestuser")

	# Testing ldb.search for (&(anr=testy ldap user)(objectClass=user))
	res = ldb.search("(&(anr=testy ldap user)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(anr=testy ldap user)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser2")
	assertEquals(res[0].name, "ldaptestuser2")

	# Testing ldb.search for (&(anr==testy ldap user2)(objectClass=user))
	res = ldb.search("(&(anr==testy ldap user2)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(anr==testy ldap user2)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser2")
	assertEquals(res[0].name, "ldaptestuser2")

	# Testing ldb.search for (&(anr==ldap user2)(objectClass=user))
	res = ldb.search("(&(anr==ldap user2)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(anr==ldap user2)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser2")
	assertEquals(res[0].name, "ldaptestuser2")

	# Testing ldb.search for (&(anr==not ldap user2)(objectClass=user))
	res = ldb.search("(&(anr==not ldap user2)(objectClass=user))")
	if (res.error != 0 || len(res) != 0) {
		print "Must not find (&(anr==not ldap user2)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 0)
	}

	# Testing ldb.search for (&(anr=not ldap user2)(objectClass=user))
	res = ldb.search("(&(anr=not ldap user2)(objectClass=user))")
	if (res.error != 0 || len(res) != 0) {
		print "Must not find (&(anr=not ldap user2)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 0)
	}

	print "Testing Group Modifies"
	ok = ldb.modify("
dn: cn=ldaptestgroup,cn=users," + base_dn + "
changetype: modify
add: member
member: cn=ldaptestuser2,cn=users," + base_dn + "
member: cn=ldaptestcomputer,cn=computers," + base_dn + "
")

	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.delete("cn=ldaptestuser3,cn=users," + base_dn)

	print "Testing adding non-existent user to a group"
	ok = ldb.modify("
dn: cn=ldaptestgroup,cn=users," + base_dn + "
changetype: modify
add: member
member: cn=ldaptestuser3,cn=users," + base_dn + "
")
	if (ok.error != 32) { # LDAP_NO_SUCH_OBJECT
		print ok.errstr
		assertEquals(ok.error, 32)
	}

	print "Testing Renames"

	ok = ldb.rename("cn=ldaptestuser2,cn=users," + base_dn, "cn=ldaptestuser3,cn=users," + base_dn)
	if (ok.error != 0) {
		print "Could not rename cn=ldaptestuser2,cn=users," + base_dn + " into cn=ldaptestuser3,cn=users," + base_dn + ": " + ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestuser3,cn=users," + base_dn)
	if (ok.error != 0) {
		print "Could not rename cn=ldaptestuser3,cn=users," + base_dn + " onto itself: " + ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestUSER3,cn=users," + base_dn)
	if (ok.error != 0) {
		print "Could not rename cn=ldaptestuser3,cn=users," + base_dn + " into cn=ldaptestUSER3,cn=users," + base_dn + ": " + ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptestuser3)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestuser3)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestuser3)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestUSER3,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestUSER3")
	assertEquals(res[0].name, "ldaptestUSER3")

# This is a Samba special, and does not exist in real AD
#	print "Testing ldb.search for (dn=CN=ldaptestUSER3,CN=Users," + base_dn + ")"
#	res = ldb.search("(dn=CN=ldaptestUSER3,CN=Users," + base_dn + ")")
#	if (res.error != 0 || len(res) != 1) {
#		print "Could not find (dn=CN=ldaptestUSER3,CN=Users," + base_dn + ")"
#		assertEquals(res.error, 0)
#		assertEquals(len(res), 1)
#	}
#	assertEquals(res[0].dn, ("CN=ldaptestUSER3,CN=Users," + base_dn))
#	assertEquals(res[0].cn, "ldaptestUSER3")
#	assertEquals(res[0].name, "ldaptestUSER3")

	print "Testing ldb.search for (distinguishedName=CN=ldaptestUSER3,CN=Users," + base_dn + ")"
	res = ldb.search("(distinguishedName=CN=ldaptestUSER3,CN=Users," + base_dn + ")")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (dn=CN=ldaptestUSER3,CN=Users," + base_dn + ")"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}
	assertEquals(res[0].dn, ("CN=ldaptestUSER3,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestUSER3")
	assertEquals(res[0].name, "ldaptestUSER3")

	# ensure we cannot add it again
	ok = ldb.add({"dn": "cn=ldaptestuser3,cn=userS," + base_dn,
                  "objectClass": ["person", "user"],
                  "cn": "LDAPtestUSER3"})
#LDB_ERR_ENTRY_ALREADY_EXISTS
	if (ok.error != 68) {
		print "expected error LDB_ERR_ENTRY_ALREADY_EXISTS, got: " + ok.errstr
		assertEquals(ok.error, 68)
	}

	# rename back
	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestuser2,cn=users," + base_dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	# ensure we cannnot rename it twice
	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestuser2,cn=users," + base_dn)
#LDB_ERR_NO_SUCH_OBJECT
	assertEquals(ok.error, 32)

	# ensure can now use that name
	ok = ldb.add({"dn": "cn=ldaptestuser3,cn=users," + base_dn,
                  "objectClass": ["person", "user"],
                  "cn": "LDAPtestUSER3"})
	
	# ensure we now cannnot rename
	ok = ldb.rename("cn=ldaptestuser2,cn=users," + base_dn, "cn=ldaptestuser3,cn=users," + base_dn)
#LDB_ERR_ENTRY_ALREADY_EXISTS
	if (ok.error != 68) {
		print "expected error LDB_ERR_ENTRY_ALREADY_EXISTS, got: " + ok.errstr
		assertEquals(ok.error, 68)
	}
	assertEquals(ok.error, 68)
	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestuser3,cn=configuration," + base_dn)
	if (ok.error != 71 && ok.error != 64) {
		print "expected error LDB_ERR_ENTRY_ALREADY_EXISTS or LDAP_NAMING_VIOLATION, got: " + ok.errstr
		assertEquals(ok.error == 71 || ok.error, 64)
	}
	assertEquals(ok.error == 71 || ok.error, 64)

	ok = ldb.rename("cn=ldaptestuser3,cn=users," + base_dn, "cn=ldaptestuser5,cn=users," + base_dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.delete("cn=ldaptestuser5,cn=users," + base_dn)

	ok = ldb.delete("cn=ldaptestgroup2,cn=users," + base_dn)

	ok = ldb.rename("cn=ldaptestgroup,cn=users," + base_dn, "cn=ldaptestgroup2,cn=users," + base_dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing subtree Renames"

	ok = ldb.add({"dn": "cn=ldaptestcontainer," + base_dn, "objectClass": "container"})
	
	ok = ldb.add({"dn": "CN=ldaptestuser4,CN=ldaptestcontainer," + base_dn, 
                  "objectClass": ["person", "user"],
                  "cn": "LDAPtestUSER4"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestuser4,cn=ldaptestcontainer," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
		ok = ldb.add({"dn": "CN=ldaptestuser4,CN=ldaptestcontainer," + base_dn,
                      "objectClass": ["person", "user"],
                      "cn": "LDAPtestUSER4"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
add: member
member: cn=ldaptestuser4,cn=ldaptestcontainer," + base_dn + "
")
	if (ok.error != 0) {
		print "Failure adding ldaptestuser4 to a group"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	print "Testing ldb.rename of cn=ldaptestcontainer," + base_dn + " to cn=ldaptestcontainer2," + base_dn
	ok = ldb.rename("CN=ldaptestcontainer," + base_dn, "CN=ldaptestcontainer2," + base_dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptestuser4)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestuser4)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	print "Testing subtree ldb.search for (&(cn=ldaptestuser4)(objectClass=user)) in (just renamed from) cn=ldaptestcontainer," + base_dn
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))", "cn=ldaptestcontainer," + base_dn, ldb.SCOPE_SUBTREE)
	if (res.error != 32) {
		print res.errstr
		assertEquals(res.error, 32)
	}

	print "Testing one-level ldb.search for (&(cn=ldaptestuser4)(objectClass=user)) in (just renamed from) cn=ldaptestcontainer," + base_dn
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))", "cn=ldaptestcontainer," + base_dn, ldb.SCOPE_ONELEVEL)
	if (res.error != 32) {
		print res.errstr
		assertEquals(res.error, 32)
	}

	print "Testing ldb.search for (&(cn=ldaptestuser4)(objectClass=user)) in renamed container"
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))", "cn=ldaptestcontainer2," + base_dn, ldb.SCOPE_SUBTREE)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestuser4)(objectClass=user)) under cn=ldaptestcontainer2," + base_dn
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn))
	assertEquals(strupper(res[0].memberOf[0]), strupper(("CN=ldaptestgroup2,CN=Users," + base_dn)))

	print "Testing ldb.search for (&(member=CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn + ")(objectclass=group)) to check subtree renames and linked attributes"
	res = ldb.search("(&(member=CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn + ")(objectclass=group))", base_dn, ldb.SCOPE_SUBTREE)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(member=CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn + ")(objectclass=group)), perhaps linked attributes are not conistant with subtree renames?"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	print "Testing ldb.rename (into itself) of cn=ldaptestcontainer2," + base_dn + " to cn=ldaptestcontainer,cn=ldaptestcontainer2," + base_dn
	ok = ldb.rename("cn=ldaptestcontainer2," + base_dn, "cn=ldaptestcontainer,cn=ldaptestcontainer2," + base_dn)
	if (ok.error != 53) { # LDAP_UNWILLING_TO_PERFORM
		print ok.errstr
		assertEquals(ok.error, 53)
	}

	print "Testing ldb.rename (into non-existent container) of cn=ldaptestcontainer2," + base_dn + " to cn=ldaptestcontainer,cn=ldaptestcontainer3," + base_dn
	ok = ldb.rename("cn=ldaptestcontainer2," + base_dn, "cn=ldaptestcontainer,cn=ldaptestcontainer3," + base_dn)
	if (ok.error != 53 && ok.error != 80) { # LDAP_UNWILLING_TO_PERFORM or LDAP_OTHER
		print ok.errstr
		assertEquals(ok.error == 53 || ok.error, 80)
	}

	print "Testing delete (should fail, not a leaf node) of renamed cn=ldaptestcontainer2," + base_dn
	ok = ldb.delete("cn=ldaptestcontainer2," + base_dn)
	if (ok.error != 66) { # LDB_ERR_NOT_ALLOWED_ON_NON_LEAF
		print ok.errstr
		assertEquals(ok.error, 66)
	}

	print "Testing base ldb.search for CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn
	res = ldb.search("(objectclass=*)", ("CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn), ldb.SCOPE_BASE)
	if (res.error == 0 && res.count == 1) {
		assertEquals(res.error == 0 && res.count, 1)
	}
	res = ldb.search("(cn=ldaptestuser40)", ("CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn), ldb.SCOPE_BASE)
	if (res.error == 0 && res.count == 0) {
		assertEquals(res.error == 0 && res.count, 0)
	}

	print "Testing one-level ldb.search for (&(cn=ldaptestuser4)(objectClass=user)) in cn=ldaptestcontainer2," + base_dn
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))", "cn=ldaptestcontainer2," + base_dn, ldb.SCOPE_ONELEVEL)
	if (res.error == 0 && res.count == 0) {
		assertEquals(res.error == 0 && res.count, 0)
	}

	print "Testing one-level ldb.search for (&(cn=ldaptestuser4)(objectClass=user)) in cn=ldaptestcontainer2," + base_dn
	res = ldb.search("(&(cn=ldaptestuser4)(objectClass=user))", "cn=ldaptestcontainer2," + base_dn, ldb.SCOPE_SUBTREE)
	if (res.error == 0 && res.count == 0) {
		assertEquals(res.error == 0 && res.count, 0)
	}

	print "Testing delete of subtree renamed "+("CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn)
	ok = ldb.delete(("CN=ldaptestuser4,CN=ldaptestcontainer2," + base_dn))
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	print "Testing delete of renamed cn=ldaptestcontainer2," + base_dn
	ok = ldb.delete("cn=ldaptestcontainer2," + base_dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
    ok = ldb.add({"dn": "cn=ldaptestutf8user èùéìòà ,cn=users," + base_dn, "objectClass": "user"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestutf8user èùéìòà ,cn=users," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	ok = ldb.add({"dn": "cn=ldaptestutf8user èùéìòà ,cn=users," + base_dn, "objectClass": "user"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	ok = ldb.add({"dn": "cn=ldaptestutf8user2  èùéìòà ,cn=users," + base_dn, "objectClass": "user"})
	if (ok.error != 0) {
		ok = ldb.delete("cn=ldaptestutf8user2  èùéìòà ,cn=users," + base_dn)
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
    ok = ldb.add({"dn": "cn=ldaptestutf8user2  èùéìòà ,cn=users," + base_dn,
                  "objectClass": "user"})
		if (ok.error != 0) {
			print ok.errstr
			assertEquals(ok.error, 0)
		}
	}

	print "Testing ldb.search for (&(cn=ldaptestuser)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestuser)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestuser)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser")
	assertEquals(res[0].name, "ldaptestuser")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "person")
	assertEquals(res[0].objectClass[2], "organizationalPerson")
	assertEquals(res[0].objectClass[3], "user")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)
	assertEquals(res[0].objectCategory, ("CN=Person,CN=Schema,CN=Configuration," + base_dn))
	assertEquals(res[0].sAMAccountType, 805306368)
#	assertEquals(res[0].userAccountControl, 546)
	assertEquals(res[0].memberOf[0], ("CN=ldaptestgroup2,CN=Users," + base_dn))
	assertEquals(res[0].memberOf.length, 1)
 
	print "Testing ldb.search for (&(cn=ldaptestuser)(objectCategory=cn=person,cn=schema,cn=configuration," + base_dn + "))"
	res2 = ldb.search("(&(cn=ldaptestuser)(objectCategory=cn=person,cn=schema,cn=configuration," + base_dn + "))")
	if (res2.error != 0 || res2.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestuser)(objectCategory=cn=person,cn=schema,cn=configuration," + base_dn + "))"
		assertEquals(res2.error, 0)
		assertEquals(res2.msgs.length, 1)
	}

	assertEquals(res[0].dn, res2.msgs[0].dn)

	print "Testing ldb.search for (&(cn=ldaptestuser)(objectCategory=PerSon))"
	res3 = ldb.search("(&(cn=ldaptestuser)(objectCategory=PerSon))")
	if (res3.error != 0) {
		print "Could not find (&(cn=ldaptestuser)(objectCategory=PerSon)): " + res3.errstr
		assertEquals(res3.error, 0)
	} else if (res3.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestuser)(objectCategory=PerSon)): matched " + res3.msgs.length
		assertEquals(res3.msgs.length, 1)
	}

	assertEquals(res[0].dn, res3.msgs[0].dn)

	if (gc_ldb != undefined) {
		print "Testing ldb.search for (&(cn=ldaptestuser)(objectCategory=PerSon)) in Global Catalog"
		res3gc = gc_ldb.search("(&(cn=ldaptestuser)(objectCategory=PerSon))")
		if (res3gc.error != 0) {
			print "Could not find (&(cn=ldaptestuser)(objectCategory=PerSon)) in Global Catalog: " + res3gc.errstr
			assertEquals(res3gc.error, 0)
		} else if (res3gc.msgs.length != 1) {
			print "Could not find (&(cn=ldaptestuser)(objectCategory=PerSon)) in Global Catalog: matched " + res3gc.msgs.length
			assertEquals(res3gc.msgs.length, 1)
		}
	
		assertEquals(res[0].dn, res3gc.msgs[0].dn)
	}

	print "Testing ldb.search for (&(cn=ldaptestuser)(objectCategory=PerSon)) in with 'phantom root' control"
	attrs = ["cn"]
	controls = ["search_options:1:2"]
	res3control = gc_ldb.search("(&(cn=ldaptestuser)(objectCategory=PerSon))", base_dn, ldb.SCOPE_SUBTREE, attrs, controls)
	if (res3control.error != 0 || res3control.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestuser)(objectCategory=PerSon)) in Global Catalog"
		assertEquals(res3control.error, 0)
		assertEquals(res3control.msgs.length, 1)
	}
	
	assertEquals(res[0].dn, res3control.msgs[0].dn)

	ok = ldb.delete(res[0].dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptestcomputer)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestcomputer)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestuser)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestcomputer,CN=Computers," + base_dn))
	assertEquals(res[0].cn, "ldaptestcomputer")
	assertEquals(res[0].name, "ldaptestcomputer")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "person")
	assertEquals(res[0].objectClass[2], "organizationalPerson")
	assertEquals(res[0].objectClass[3], "user")
	assertEquals(res[0].objectClass[4], "computer")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)
	assertEquals(res[0].objectCategory, ("CN=Computer,CN=Schema,CN=Configuration," + base_dn))
	assertEquals(res[0].primaryGroupID, 513)
#	assertEquals(res[0].sAMAccountType, 805306368)
#	assertEquals(res[0].userAccountControl, 546)
	assertEquals(res[0].memberOf[0], ("CN=ldaptestgroup2,CN=Users," + base_dn))
	assertEquals(res[0].memberOf.length, 1)

	print "Testing ldb.search for (&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + "))"
	res2 = ldb.search("(&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + "))")
	if (res2.error != 0 || res2.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + "))"
		assertEquals(res2.error, 0)
		assertEquals(res2.msgs.length, 1)
	}

	assertEquals(res[0].dn, res2.msgs[0].dn)

	if (gc_ldb != undefined) {
		print "Testing ldb.search for (&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + ")) in Global Catlog"
		res2gc = gc_ldb.search("(&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + "))")
		if (res2gc.error != 0 || res2gc.msgs.length != 1) {
			print "Could not find (&(cn=ldaptestcomputer)(objectCategory=cn=computer,cn=schema,cn=configuration," + base_dn + ")) in Global Catlog"
			assertEquals(res2gc.error, 0)
			assertEquals(res2gc.msgs.length, 1)
		}

		assertEquals(res[0].dn, res2gc.msgs[0].dn)
	}

	print "Testing ldb.search for (&(cn=ldaptestcomputer)(objectCategory=compuTER))"
	res3 = ldb.search("(&(cn=ldaptestcomputer)(objectCategory=compuTER))")
	if (res3.error != 0 || res3.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestcomputer)(objectCategory=compuTER))"
		assertEquals(res3.error, 0)
		assertEquals(res3.msgs.length, 1)
	}

	assertEquals(res[0].dn, res3.msgs[0].dn)

	if (gc_ldb != undefined) {
		print "Testing ldb.search for (&(cn=ldaptestcomputer)(objectCategory=compuTER)) in Global Catalog"
		res3gc = gc_ldb.search("(&(cn=ldaptestcomputer)(objectCategory=compuTER))")
		if (res3gc.error != 0 || res3gc.msgs.length != 1) {
			print "Could not find (&(cn=ldaptestcomputer)(objectCategory=compuTER)) in Global Catalog"
			assertEquals(res3gc.error, 0)
			assertEquals(res3gc.msgs.length, 1)
		}

		assertEquals(res[0].dn, res3gc.msgs[0].dn)
	}

	print "Testing ldb.search for (&(cn=ldaptestcomp*r)(objectCategory=compuTER))"
	res4 = ldb.search("(&(cn=ldaptestcomp*r)(objectCategory=compuTER))")
	if (res4.error != 0 || res4.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestcomp*r)(objectCategory=compuTER))"
		assertEquals(res4.error, 0)
		assertEquals(res4.msgs.length, 1)
	}

	assertEquals(res[0].dn, res4.msgs[0].dn)

	print "Testing ldb.search for (&(cn=ldaptestcomput*)(objectCategory=compuTER))"
	res5 = ldb.search("(&(cn=ldaptestcomput*)(objectCategory=compuTER))")
	if (res5.error != 0 || res5.msgs.length != 1) {
		print "Could not find (&(cn=ldaptestcomput*)(objectCategory=compuTER))"
		assertEquals(res5.error, 0)
		assertEquals(res5.msgs.length, 1)
	}

	assertEquals(res[0].dn, res5.msgs[0].dn)

	print "Testing ldb.search for (&(cn=*daptestcomputer)(objectCategory=compuTER))"
	res6 = ldb.search("(&(cn=*daptestcomputer)(objectCategory=compuTER))")
	if (res6.error != 0 || res6.msgs.length != 1) {
		print "Could not find (&(cn=*daptestcomputer)(objectCategory=compuTER))"
		assertEquals(res6.error, 0)
		assertEquals(res6.msgs.length, 1)
	}

	assertEquals(res[0].dn, res6.msgs[0].dn)

	ok = ldb.delete(res[0].dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptest2computer)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptest2computer)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptest2computer)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptest2computer,CN=Computers," + base_dn))
	assertEquals(res[0].cn, "ldaptest2computer")
	assertEquals(res[0].name, "ldaptest2computer")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "person")
	assertEquals(res[0].objectClass[2], "organizationalPerson")
	assertEquals(res[0].objectClass[3], "user")
	assertEquals(res[0].objectClass[4], "computer")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)
	assertEquals(res[0].objectCategory, "cn=Computer,cn=Schema,cn=Configuration," + base_dn)
	assertEquals(res[0].sAMAccountType, 805306369)
#	assertEquals(res[0].userAccountControl, 4098)

   	ok = ldb.delete(res[0].dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

    attrs = ["cn", "name", "objectClass", "objectGUID", "whenCreated", "nTSecurityDescriptor", "memberOf"]
	print "Testing ldb.search for (&(cn=ldaptestUSer2)(objectClass=user))"
	res = ldb.search(base_dn, "(&(cn=ldaptestUSer2)(objectClass=user))", ldb.SCOPE_SUBTREE, attrs)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestUSer2)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestuser2")
	assertEquals(res[0].name, "ldaptestuser2")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "person")
	assertEquals(res[0].objectClass[2], "organizationalPerson")
	assertEquals(res[0].objectClass[3], "user")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)
	assert(res[0].nTSecurityDescriptor != undefined)
	assertEquals(res[0].memberOf[0], ("CN=ldaptestgroup2,CN=Users," + base_dn))

        attrs = ["cn", "name", "objectClass", "objectGUID", "whenCreated", "nTSecurityDescriptor", "member"]
	print "Testing ldb.search for (&(cn=ldaptestgroup2)(objectClass=group))"
	res = ldb.search("(&(cn=ldaptestgroup2)(objectClass=group))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestgroup2)(objectClass=group))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestgroup2,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestgroup2")
	assertEquals(res[0].name, "ldaptestgroup2")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "group")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)
	assert(res[0].nTSecurityDescriptor != undefined)
	assertEquals(res[0].member[0], ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].member.length, 1)

	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
replace: member
member: CN=ldaptestuser2,CN=Users," + base_dn + "
member: CN=ldaptestutf8user èùéìòà,CN=Users," + base_dn + "
")
	if (ok.error != 0) {
		print "Failure testing replace of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	print "Testing Linked attribute behaviours"
	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
delete: member
")
	if (ok.error != 0) {
		print "Failure testing delete of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
add: member
member: CN=ldaptestuser2,CN=Users," + base_dn + "
member: CN=ldaptestutf8user èùéìòà,CN=Users," + base_dn + "
")
	if (ok.error != 0) {
		print "Failure testing add of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
replace: member
")
	if (ok.error != 0) {
		print "Failure testing replace of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
add: member
member: CN=ldaptestuser2,CN=Users," + base_dn + "
member: CN=ldaptestutf8user èùéìòà,CN=Users," + base_dn + "
")
	if (ok.error != 0) {
		print "Failure testing add of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	ok = ldb.modify("
dn: cn=ldaptestgroup2,cn=users," + base_dn + "
changetype: modify
delete: member
member: CN=ldaptestutf8user èùéìòà,CN=Users," + base_dn + "
")
	if (ok.error != 0) {
		print "Failure testing replace of linked attributes"
		print ok.errstr
		assertEquals(ok.error, 0)
	}
	
	res = ldb.search("(&(cn=ldaptestgroup2)(objectClass=group))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestgroup2)(objectClass=group))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestgroup2,CN=Users," + base_dn))
	assertEquals(res[0].member[0], ("CN=ldaptestuser2,CN=Users," + base_dn))
	assertEquals(res[0].member.length, 1)

	ok = ldb.delete(("CN=ldaptestuser2,CN=Users," + base_dn))
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

        attrs = ["cn", "name", "objectClass", "objectGUID", "whenCreated", "nTSecurityDescriptor", "member"]
	print "Testing ldb.search for (&(cn=ldaptestgroup2)(objectClass=group)) to check linked delete"
	res = ldb.search("(&(cn=ldaptestgroup2)(objectClass=group))", base_dn, ldb.SCOPE_SUBTREE, attrs)
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestgroup2)(objectClass=group)) to check linked delete"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestgroup2,CN=Users," + base_dn))
	assertEquals(res[0].member, undefined)

	print "Testing ldb.search for (&(cn=ldaptestutf8user ÈÙÉÌÒÀ)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestutf8user ÈÙÉÌÒÀ)(objectClass=user))")

	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestutf8user ÈÙÉÌÒÀ)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	assertEquals(res[0].dn, ("CN=ldaptestutf8user èùéìòà,CN=Users," + base_dn))
	assertEquals(res[0].cn, "ldaptestutf8user èùéìòà")
	assertEquals(res[0].name, "ldaptestutf8user èùéìòà")
	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "person")
	assertEquals(res[0].objectClass[2], "organizationalPerson")
	assertEquals(res[0].objectClass[3], "user")
	assert(res[0].objectGUID != undefined)
	assert(res[0].whenCreated != undefined)

	ok = ldb.delete(res[0].dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptestutf8user2*)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestutf8user2*)(objectClass=user))")
	if (res.error != 0 || len(res) != 1) {
		print "Could not find (&(cn=ldaptestutf8user2*)(objectClass=user))"
		assertEquals(res.error, 0)
		assertEquals(len(res), 1)
	}

	ok = ldb.delete(res[0].dn)
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	ok = ldb.delete(("CN=ldaptestgroup2,CN=Users," + base_dn))
	if (ok.error != 0) {
		print ok.errstr
		assertEquals(ok.error, 0)
	}

	print "Testing ldb.search for (&(cn=ldaptestutf8user2 ÈÙÉÌÒÀ)(objectClass=user))"
	res = ldb.search("(&(cn=ldaptestutf8user ÈÙÉÌÒÀ)(objectClass=user))")

	if (res.error != 0 || len(res) != 1) {
		print "Could not find (expect space collapse, win2k3 fails) (&(cn=ldaptestutf8user2 ÈÙÉÌÒÀ)(objectClass=user))"
	} else {
		assertEquals(res[0].dn, ("cn=ldaptestutf8user2 èùéìòà,cn=users," + base_dn))
		assertEquals(res[0].cn, "ldaptestutf8user2 èùéìòà")
	}

	print "Testing that we can't get at the configuration DN from the main search base"
	attrs = ["cn"]
	res = ldb.search("objectClass=crossRef", base_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	if (len(res) != 0) {
		print "Got configuration DN " + res[0].dn + " which should not be able to be seen from main search base"
	}
	assertEquals(len(res), 0)

	print "Testing that we can get at the configuration DN from the main search base on the LDAP port with the 'phantom root' search_options control"
	attrs = ["cn"]
	controls = ["search_options:1:2"]
	res = ldb.search("objectClass=crossRef", base_dn, ldb.SCOPE_SUBTREE, attrs, controls)
	assertEquals(res.error, 0)
	assert(len(res) > 0)

	if (gc_ldb != undefined) {
		print "Testing that we can get at the configuration DN from the main search base on the GC port with the search_options control == 0"
		attrs = ["cn"]
		controls = ["search_options:1:0"]
		res = gc_ldb.search("objectClass=crossRef", base_dn, gc_ldb.SCOPE_SUBTREE, attrs, controls)
		assertEquals(res.error, 0)
		assert(len(res) > 0)

		print "Testing that we do find configuration elements in the global catlog"
		attrs = ["cn"]
		res = gc_ldb.search("objectClass=crossRef", base_dn, ldb.SCOPE_SUBTREE, attrs)
		assertEquals(res.error, 0)
		assert (len(res) > 0)
	
		print "Testing that we do find configuration elements and user elements at the same time"
		attrs = ["cn"]
		res = gc_ldb.search("(|(objectClass=crossRef)(objectClass=person))", base_dn, ldb.SCOPE_SUBTREE, attrs)
		assertEquals(res.error, 0)
		assert (len(res) > 0)

		print "Testing that we do find configuration elements in the global catlog, with the configuration basedn"
		attrs = ["cn"]
		res = gc_ldb.search("objectClass=crossRef", configuration_dn, ldb.SCOPE_SUBTREE, attrs)
		assertEquals(res.error, 0)
		assert (len(res) > 0)
	}

	print "Testing that we can get at the configuration DN on the main LDAP port"
	attrs = ["cn"]
	res = ldb.search("objectClass=crossRef", configuration_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	assert (len(res) > 0)

	print "Testing objectCategory canonacolisation"
	attrs = ["cn"]
	res = ldb.search("objectCategory=ntDsDSA", configuration_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	if (len(res) == 0) {
		print "Didn't find any records with objectCategory=ntDsDSA"
	}
	assert(len(res) != 0)
	
	attrs = ["cn"]
	res = ldb.search("objectCategory=CN=ntDs-DSA," + schema_dn, configuration_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	if (len(res) == 0) {
		print "Didn't find any records with objectCategory=CN=ntDs-DSA," + schema_dn
	}
	assert(len(res) != 0)
	
	print "Testing objectClass attribute order on "+ base_dn
	attrs = ["objectClass"]
	res = ldb.search("objectClass=domain", base_dn, ldb.SCOPE_BASE, attrs)
	assertEquals(res.error, 0)
	assertEquals(len(res), 1)

	assertEquals(res[0].objectClass[0], "top")
	assertEquals(res[0].objectClass[1], "domain")
	assertEquals(res[0].objectClass[2], "domainDNS")

#  check enumeration

 	attrs = ["cn"]
	print "Testing ldb.search for objectCategory=person"
	res = ldb.search("objectCategory=person", base_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	assert(len(res) > 0)

 	attrs = ["cn"]
	controls = ["domain_scope:1"]
	print "Testing ldb.search for objectCategory=person with domain scope control"
	res = ldb.search("objectCategory=person", base_dn, ldb.SCOPE_SUBTREE, attrs, controls)
	assertEquals(res.error, 0)
	assert(len(res) > 0)
 
	attrs = ["cn"]
	print "Testing ldb.search for objectCategory=user"
	res = ldb.search("objectCategory=user", base_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	assert(len(res) > 0)

 	attrs = ["cn"]
	controls = ["domain_scope:1"]
	print "Testing ldb.search for objectCategory=user with domain scope control"
	res = ldb.search("objectCategory=user", base_dn, ldb.SCOPE_SUBTREE, attrs, controls)
	assertEquals(res.error, 0)
	assert(len(res) > 0)
	
 	attrs = ["cn"]
	print "Testing ldb.search for objectCategory=group"
	res = ldb.search("objectCategory=group", base_dn, ldb.SCOPE_SUBTREE, attrs)
	assertEquals(res.error, 0)
	assert(len(res) > 0)

 	attrs = ["cn"]
	controls = ["domain_scope:1"]
	print "Testing ldb.search for objectCategory=group with domain scope control"
	res = ldb.search("objectCategory=group", base_dn, ldb.SCOPE_SUBTREE, attrs, controls)
	assertEquals(res.error, 0)
	assert(len(res) > 0)
	
}

def basedn_tests(ldb, gc_ldb):
	print "Testing for all rootDSE attributes"
	attrs = []
	res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
	assertEquals(res.error, 0)
	assertEquals(len(res), 1)

	print "Testing for highestCommittedUSN"
	attrs = ["highestCommittedUSN"]
	res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
	assertEquals(res.error, 0)
	assertEquals(len(res), 1)
	assert(res[0].highestCommittedUSN != undefined)
	assert(res[0].highestCommittedUSN != 0)

	print "Testing for netlogon via LDAP"
	attrs = ["netlogon"]
	res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
	assertEquals(res.error, 0)
	assertEquals(len(res), 0)

	print "Testing for netlogon and highestCommittedUSN via LDAP"
	attrs = ["netlogon", "highestCommittedUSN"]
	res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
	assertEquals(res.error, 0)
	assertEquals(len(res), 0)

def find_basedn(ldb):
    attrs = ["defaultNamingContext"]
    res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
    assertEquals(res.error, 0)
    assertEquals(len(res), 1)
    return res[0].defaultNamingContext

def find_configurationdn(ldb):
    attrs = ["configurationNamingContext"]
    res = ldb.search("", "", ldb.SCOPE_BASE, attrs)
    assertEquals(res.error, 0)
    assertEquals(len(res), 1)
    return res[0].configurationNamingContext

def find_schemadn(ldb):
    res = ldb.search("", "", ldb.SCOPE_BASE, attrs=["schemaNamingContext"])
    assertEquals(res.error, 0)
    assertEquals(len(res), 1)
    return res[0].schemaNamingContext

# use command line creds if available
ldb.credentials = options.get_credentials()
gc_ldb.credentials = options.get_credentials()

ok = ldb.connect("ldap://" + host)
base_dn = find_basedn(ldb)

configuration_dn = find_configurationdn(ldb)
schema_dn = find_schemadn(ldb)

print "baseDN: %s\n" % base_dn

ok = gc_ldb.connect("ldap://" + host + ":3268")
if (!ok) {
	gc_ldb = undefined
}

basic_tests(ldb, gc_ldb, base_dn, configuration_dn, schema_dn)
basedn_tests(ldb, gc_ldb)
