/*
	backend code for provisioning a Samba4 server
	Copyright Andrew Tridgell 2005
	Released under the GNU GPL v2 or later
*/

/* used to generate sequence numbers for records */
provision_next_usn = 1;

sys = sys_init();

/*
  return true if the current install seems to be OK
*/
function install_ok()
{
	var lp = loadparm_init();
	var ldb = ldb_init();
	if (lp.get("realm") == "") {
		return false;
	}
	var ok = ldb.connect(lp.get("sam database"));
	if (!ok) {
		return false;
	}
	var res = ldb.search("(name=Administrator)");
	if (res.length != 1) {
		return false;
	}
	return true;
}

/*
  find a user or group from a list of possibilities
*/
function findnss()
{
	var i;
	assert(arguments.length >= 2);
	var nssfn = arguments[0];
	for (i=1;i<arguments.length;i++) {
		if (nssfn(arguments[i]) != undefined) {
			return arguments[i];
		}
	}
	printf("Unable to find user/group for %s\n", arguments[1]);
	assert(i<arguments.length);
}

/*
   add a foreign security principle
 */
function add_foreign(str, sid, desc, unixname)
{
	var add = "
dn: CN=${SID},CN=ForeignSecurityPrincipals,${BASEDN}
objectClass: top
objectClass: foreignSecurityPrincipal
description: ${DESC}
unixName: ${UNIXNAME}
uSNCreated: 1
uSNChanged: 1
";
	var sub = new Object();
	sub.SID = sid;
	sub.DESC = desc;
	sub.UNIXNAME = unixname;
	return str + substitute_var(add, sub);
}

/*
  return current time as a nt time string
*/
function nttime()
{
	return "" + sys.nttime();
}

/*
  return current time as a ldap time string
*/
function ldaptime()
{
	return sys.ldaptime(sys.nttime());
}

/*
  return a date string suitable for a dns zone serial number
*/
function datestring()
{
	var t = sys.gmtime(sys.nttime());
	return sprintf("%04u%02u%02u%02u",
		       t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour);
}

/*
  return first host IP
*/
function hostip()
{
	var list = sys.interfaces();
	return list[0];
}

/*
  return next USN in the sequence
*/
function nextusn()
{
	provision_next_usn = provision_next_usn+1;
	return provision_next_usn;
}

/*
  return first part of hostname
*/
function hostname()
{
	var s = split(".", sys.hostname());
	return s[0];
}


/*
  erase an ldb, removing all records
*/
function ldb_erase(ldb)
{
	var attrs = new Array("dn");

	/* delete the specials */
	ldb.del("@INDEXLIST");
	ldb.del("@ATTRIBUTES");
	ldb.del("@SUBCLASSES");
	ldb.del("@MODULES");

	/* and the rest */
	var res = ldb.search("(|(objectclass=*)(dn=*))", attrs);
	var i;
	for (i=0;i<res.length;i++) {
		ldb.del(res[i].dn);
	}
	res = ldb.search("(objectclass=*)", attrs);
	assert(res.length == 0);
}


/*
  setup a ldb in the private dir
 */
function setup_ldb(ldif, dbname, subobj)
{
	var erase = true;
	var extra = "";
	var ldb = ldb_init();
	var lp = loadparm_init();

	if (arguments.length >= 4) {
		extra = arguments[3];
	}

	if (arguments.length == 5) {
	        erase = arguments[4];
        }

	var src = lp.get("setup directory") + "/" + ldif;

	var data = sys.file_load(src);
	data = data + extra;
	data = substitute_var(data, subobj);

	var ok = ldb.connect(dbname);
	assert(ok);

	if (erase) {
		ldb_erase(ldb);	
	}

	ok = ldb.add(data);
	assert(ok);
}

/*
  setup a file in the private dir
 */
function setup_file(template, fname, subobj)
{
	var lp = loadparm_init();
	var f = fname;
	var src = lp.get("setup directory") + "/" + template;

	sys.unlink(f);

	var data = sys.file_load(src);
	data = substitute_var(data, subobj);

	ok = sys.file_save(f, data);
	assert(ok);
}

/*
  provision samba4 - caution, this wipes all existing data!
*/
function provision(subobj, message, blank)
{
	var data = "";
	var lp = loadparm_init();
	var sys = sys_init();
	var smbconf = lp.get("config file");

	/*
	  some options need to be upper/lower case
	*/
	subobj.REALM       = strlower(subobj.REALM);
	subobj.HOSTNAME    = strlower(subobj.HOSTNAME);
	subobj.DOMAIN      = strupper(subobj.DOMAIN);
	subobj.NETBIOSNAME = strupper(subobj.HOSTNAME);
	var rdns = split(",", subobj.BASEDN);
	subobj.RDN_DC = substr(rdns[0], strlen("DC="));

	data = add_foreign(data, "S-1-5-7",  "Anonymous",           "${NOBODY}");
	data = add_foreign(data, "S-1-1-0",  "World",               "${NOGROUP}");
	data = add_foreign(data, "S-1-5-2",  "Network",             "${NOGROUP}");
	data = add_foreign(data, "S-1-5-18", "System",              "${ROOT}");
	data = add_foreign(data, "S-1-5-11", "Authenticated Users", "${USERS}");

	provision_next_usn = 1;

	/* only install a new smb.conf if there isn't one there already */
	var st = sys.stat(smbconf);
	if (st == undefined) {
		message("Setting up smb.conf\n");
		setup_file("provision.smb.conf", smbconf, subobj);
		lp.reload();
	}
	message("Setting up hklm.ldb\n");
	setup_ldb("hklm.ldif", "hklm.ldb", subobj);
	message("Setting up sam.ldb attributes\n");
	setup_ldb("provision_init.ldif", "sam.ldb", subobj);
	message("Setting up sam.ldb templates\n");
	setup_ldb("provision_templates.ldif", "sam.ldb", subobj, NULL, false);
	message("Setting up sam.ldb data\n");
	setup_ldb("provision.ldif", "sam.ldb", subobj, NULL, false);
	if (blank == false) {
		message("Setting up sam.ldb users and groups\n");
		setup_ldb("provision_users.ldif", "sam.ldb", subobj, data, false);
	}
	message("Setting up rootdse.ldb\n");
	setup_ldb("rootdse.ldif", "rootdse.ldb", subobj);
	message("Setting up secrets.ldb\n");
	setup_ldb("secrets.ldif", "secrets.ldb", subobj);
	message("Setting up DNS zone file\n");
	setup_file("provision.zone", 
		   lp.get("private dir") + "/" + subobj.DNSDOMAIN + ".zone", 
		   subobj);
}

/*
  guess reasonably default options for provisioning
*/
function provision_guess()
{
	var subobj = new Object();
	var nss = nss_init();
	var lp = loadparm_init();
	var rdn_list;
	random_init(local);

	subobj.REALM        = lp.get("realm");
	subobj.DOMAIN       = lp.get("workgroup");
	subobj.HOSTNAME     = hostname();

	assert(subobj.REALM);
	assert(subobj.DOMAIN);
	assert(subobj.HOSTNAME);

	subobj.HOSTIP       = hostip();
	subobj.DOMAINGUID   = randguid();
	subobj.DOMAINSID    = randsid();
	subobj.HOSTGUID     = randguid();
	subobj.INVOCATIONID = randguid();
	subobj.KRBTGTPASS   = randpass(12);
	subobj.MACHINEPASS  = randpass(12);
	subobj.ADMINPASS    = randpass(12);
	subobj.DEFAULTSITE  = "Default-First-Site-Name";
	subobj.NEWGUID      = randguid;
	subobj.NTTIME       = nttime;
	subobj.LDAPTIME     = ldaptime;
	subobj.DATESTRING   = datestring;
	subobj.USN          = nextusn;
	subobj.ROOT         = findnss(nss.getpwnam, "root");
	subobj.NOBODY       = findnss(nss.getpwnam, "nobody");
	subobj.NOGROUP      = findnss(nss.getgrnam, "nogroup", "nobody");
	subobj.WHEEL        = findnss(nss.getgrnam, "wheel", "root", "staff");
	subobj.USERS        = findnss(nss.getgrnam, "users", "guest", "other");
	subobj.DNSDOMAIN    = strlower(subobj.REALM);
	subobj.DNSNAME      = sprintf("%s.%s", 
				      strlower(subobj.HOSTNAME), 
				      subobj.DNSDOMAIN);
	rdn_list = split(".", subobj.REALM);
	subobj.BASEDN       = "DC=" + join(",DC=", rdn_list);
	return subobj;
}

/*
  search for one attribute as a string
 */
function searchone(ldb, expression, attribute)
{
	var attrs = new Array(attribute);
	res = ldb.search(expression, attrs);
	if (res.length != 1 ||
	    res[0][attribute] == undefined) {
		return undefined;
	}
	return res[0][attribute];
}

/*
  modify an account to remove the 
*/
function enable_account(ldb, user_dn)
{
	var attrs = new Array("userAccountControl");
	var res = ldb.search(NULL, user_dn, ldb.SCOPE_ONELEVEL, attrs);
	assert(res.length == 1);
	var userAccountControl = res[0].userAccountControl;
	userAccountControl = userAccountControl - 2; /* remove disabled bit */
	var mod = sprintf("
dn: %s
changetype: modify
replace: userAccountControl
userAccountControl: %u
", 
			  user_dn, userAccountControl);
	var ok = ldb.modify(mod);
	return ok;	
}


/*
  add a new user record
*/
function newuser(username, unixname, password, message)
{
	var lp = loadparm_init();
	var samdb = lp.get("sam database");
	var ldb = ldb_init();
	random_init(local);

	/* connect to the sam */
	var ok = ldb.connect(samdb);
	assert(ok);

	/* find the DNs for the domain and the domain users group */
	var domain_dn = searchone(ldb, "objectClass=domainDNS", "dn");
	assert(domain_dn != undefined);
	var dom_users = searchone(ldb, "name=Domain Users", "dn");
	assert(dom_users != undefined);

	var user_dn = sprintf("CN=%s,CN=Users,%s", username, domain_dn);


	/*
	  the new user record. note the reliance on the samdb module to fill
	  in a sid, guid etc
	*/
	var ldif = sprintf("
dn: %s
sAMAccountName: %s
name: %s
memberOf: %s
unixName: %s
objectGUID: %s
unicodePwd: %s
objectClass: user
",
			   user_dn, username, username, dom_users,
			   unixname, randguid(), password);
	/*
	  add the user to the users group as well
	*/
	var modgroup = sprintf("
dn: %s
changetype: modify
add: member
member: %s
", 
			       dom_users, user_dn);


	/*
	  now the real work
	*/
	message("Adding user %s\n", user_dn);
	ok = ldb.add(ldif);
	if (ok != true) {
		message("Failed to add %s - %s\n", user_dn, ldb.errstring());
		return false;
	}

	message("Modifying group %s\n", dom_users);
	ok = ldb.modify(modgroup);
	if (ok != true) {
		message("Failed to modify %s - %s\n", dom_users, ldb.errstring());
		return false;
	}

	/*
	  modify the userAccountControl to remove the disabled bit
	*/
	return enable_account(ldb, user_dn);
}


return 0;
