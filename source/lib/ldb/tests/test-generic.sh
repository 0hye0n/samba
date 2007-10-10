#!/bin/sh

echo "LDB_URL: $LDB_URL"

echo "Adding base elements"
$VALGRIND ldbadd $LDBDIR/tests/test.ldif || exit 1

echo "Modifying elements"
$VALGRIND ldbmodify $LDBDIR/tests/test-modify.ldif || exit 1

echo "Showing modified record"
$VALGRIND ldbsearch '(uid=uham)'  || exit 1

echo "Rename entry"
OLDDN="cn=Ursula Hampster,ou=Alumni Association,ou=People,o=University of Michigan,c=TEST"
NEWDN="cn=Hampster Ursula,ou=Alumni Association,ou=People,o=University of Michigan,c=TEST"
$VALGRIND ldbrename "$OLDDN" "$NEWDN"  || exit 1

echo "Showing renamed record"
$VALGRIND ldbsearch '(uid=uham)' || exit 1

echo "Starting ldbtest"
time $VALGRIND ldbtest --num-records 100 --num-searches 10  || exit 1

echo "Adding index"
$VALGRIND ldbadd $LDBDIR/tests/test-index.ldif  || exit 1

echo "Adding attributes"
$VALGRIND ldbadd $LDBDIR/tests/test-wrong_attributes.ldif  || exit 1

echo "testing indexed search"
$VALGRIND ldbsearch '(uid=uham)'  || exit 1
$VALGRIND ldbsearch '(&(objectclass=person)(objectclass=person)(objectclass=top))' || exit 1
$VALGRIND ldbsearch '(&(uid=uham)(uid=uham))'  || exit 1
$VALGRIND ldbsearch '(|(uid=uham)(uid=uham))'  || exit 1
$VALGRIND ldbsearch '(|(uid=uham)(uid=uham)(objectclass=OpenLDAPperson))'  || exit 1
$VALGRIND ldbsearch '(&(uid=uham)(uid=uham)(!(objectclass=xxx)))'  || exit 1
$VALGRIND ldbsearch '(&(objectclass=person)(uid=uham)(!(uid=uhamxx)))' uid \* \+ dn  || exit 1
$VALGRIND ldbsearch '(&(uid=uham)(uid=uha*)(title=*))' uid || exit 1

# note that the "((" is treated as an attribute not an expression
# this matches the openldap ldapsearch behaviour of looking for a '='
# to see if the first argument is an expression or not
$VALGRIND ldbsearch '((' uid || exit 1
$VALGRIND ldbsearch '(objectclass=)' uid || exit 1
$VALGRIND ldbsearch -b 'cn=Hampster Ursula,ou=Alumni Association,ou=People,o=University of Michigan,c=TEST' -s base "" sn || exit 1

echo "Test wildcard match"
$VALGRIND ldbadd $LDBDIR/tests/test-wildcard.ldif  || exit 1
$VALGRIND ldbsearch '(cn=test*multi)'  || exit 1
$VALGRIND ldbsearch '(cn=*test*multi*)'  || exit 1
$VALGRIND ldbsearch '(cn=*test_multi)'  || exit 1
$VALGRIND ldbsearch '(cn=test_multi*)'  || exit 1
$VALGRIND ldbsearch '(cn=test*multi*test*multi)'  || exit 1
$VALGRIND ldbsearch '(cn=test*multi*test*multi*multi_*)' || exit 1

echo "Starting ldbtest indexed"
time $VALGRIND ldbtest --num-records 100 --num-searches 500  || exit 1

echo "Testing one level search"
count=`$VALGRIND ldbsearch -b 'ou=Groups,o=University of Michigan,c=TEST' -s one 'objectclass=*' none |grep '^dn' | wc -l`
if [ $count != 3 ]; then
    echo returned $count records - expected 3
    exit 1
fi

echo "Testing binary file attribute value"
$VALGRIND ldbmodify $LDBDIR/tests/photo.ldif || exit 1
