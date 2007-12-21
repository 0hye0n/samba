#
#    backend code for provisioning a Samba4 server
#    Released under the GNU GPL v2 or later
#    Copyright Jelmer Vernooij 2007
#
# Based on the original in EJS:
#    Copyright Andrew Tridgell 2005
#

from base64 import b64encode
import os
import pwd
import grp
import time
import uuid, misc
from socket import gethostname, gethostbyname
import param
import registry
import samba
from samba import Ldb, substitute_var, valid_netbios_name
from samba.samdb import SamDB
import security
from ldb import Dn, SCOPE_SUBTREE, SCOPE_ONELEVEL, SCOPE_BASE, LdbError, \
        LDB_ERR_NO_SUCH_OBJECT, timestring, CHANGETYPE_MODIFY, CHANGETYPE_NONE


DEFAULTSITE = "Default-First-Site-Name"

class InvalidNetbiosName(Exception):
    def __init__(self, name):
        super(InvalidNetbiosName, self).__init__("The name '%r' is not a valid NetBIOS name" % name)


class ProvisionPaths:
    def __init__(self):
        self.smbconf = None
        self.shareconf = None
        self.hklm = None
        self.hkcu = None
        self.hkcr = None
        self.hku = None
        self.hkpd = None
        self.hkpt = None
        self.samdb = None
        self.secrets = None
        self.keytab = None
        self.dns_keytab = None
        self.dns = None
        self.winsdb = None
        self.ldap_basedn_ldif = None
        self.ldap_config_basedn_ldif = None
        self.ldap_schema_basedn_ldif = None


def install_ok(lp, session_info, credentials):
    """Check whether the current install seems ok."""
    if lp.get("realm") == "":
        return False
    ldb = Ldb(lp.get("sam database"), session_info=session_info, 
            credentials=credentials, lp=lp)
    if len(ldb.search(ldb.Dn("(cn=Administrator)"))) != 1:
        return False
    return True


def findnss(nssfn, *names):
    """Find a user or group from a list of possibilities."""
    for name in names:
        try:
            return nssfn(name)
        except KeyError:
            pass
    raise Exception("Unable to find user/group for %s" % arguments[1])


def open_ldb(session_info, credentials, lp, dbname):
    assert session_info is not None
    try:
        return Ldb(dbname, session_info=session_info, credentials=credentials, 
                   lp=lp)
    except LdbError, e:
        print e
        os.unlink(dbname)
        return Ldb(dbname, session_info=session_info, credentials=credentials,
                   lp=lp)


def setup_add_ldif(ldb, ldif_path, subst_vars=None):
    """Setup a ldb in the private dir."""
    assert isinstance(ldif_path, str)

    data = open(ldif_path, 'r').read()
    if subst_vars is not None:
        data = substitute_var(data, subst_vars)

    assert "${" not in data

    ldb.load_ldif_add(data)


def setup_modify_ldif(ldb, ldif_path, substvars=None):
    """Modify a ldb in the private dir.
    
    :param ldb: LDB object.
    :param ldif_path: LDIF file path.
    :param substvars: Optional dictionary with substitution variables.
    """
    data = open(ldif_path, 'r').read()
    if substvars is not None:
        data = substitute_var(data, substvars)

    assert "${" not in data

    for (changetype, msg) in ldb.parse_ldif(data):
        assert changetype == CHANGETYPE_MODIFY
        ldb.modify(msg)


def setup_ldb(ldb, ldif_path, subst_vars):
    assert ldb is not None
    ldb.transaction_start()
    try:
        setup_add_ldif(ldb, ldif_path, subst_vars)
    except:
        ldb.transaction_cancel()
        raise
    ldb.transaction_commit()


def setup_file(template, fname, substvars):
    """Setup a file in the private dir."""
    f = fname

    if os.path.exists(f):
        os.unlink(f)

    data = open(template, 'r').read()
    if substvars:
        data = substitute_var(data, substvars)
    assert not "${" in data

    open(f, 'w').write(data)


def provision_default_paths(lp, dnsdomain):
    """Set the default paths for provisioning.

    :param lp: Loadparm context.
    :param dnsdomain: DNS Domain name
    """
    paths = ProvisionPaths()
    private_dir = lp.get("private dir")
    paths.shareconf = os.path.join(private_dir, "share.ldb")
    paths.samdb = os.path.join(private_dir, lp.get("sam database") or "samdb.ldb")
    paths.secrets = os.path.join(private_dir, lp.get("secrets database") or "secrets.ldb")
    paths.templates = os.path.join(private_dir, "templates.ldb")
    paths.keytab = os.path.join(private_dir, "secrets.keytab")
    paths.dns_keytab = os.path.join(private_dir, "dns.keytab")
    paths.dns = os.path.join(private_dir, dnsdomain + ".zone")
    paths.winsdb = os.path.join(private_dir, "wins.ldb")
    paths.ldap_basedn_ldif = os.path.join(private_dir, 
                                          dnsdomain + ".ldif")
    paths.ldap_config_basedn_ldif = os.path.join(private_dir, 
                                             dnsdomain + "-config.ldif")
    paths.ldap_schema_basedn_ldif = os.path.join(private_dir, 
                                              dnsdomain + "-schema.ldif")
    paths.s4_ldapi_path = os.path.join(private_dir, "ldapi")
    paths.phpldapadminconfig = os.path.join(private_dir, 
                                            "phpldapadmin-config.php")
    paths.hklm = os.path.join(private_dir, "hklm.ldb")
    paths.sysvol = lp.get("sysvol", "path")
    if paths.sysvol is None:
        paths.sysvol = os.path.join(lp.get("lock dir"), "sysvol")

    paths.netlogon = lp.get("netlogon", "path")
    if paths.netlogon is None:
        paths.netlogon = os.path.join(os.path.join(paths.sysvol, "scripts"))

    return paths


def setup_name_mappings(ldb, sid, domaindn, root, nobody, nogroup, users, 
                        wheel, backup):
    """setup reasonable name mappings for sam names to unix names."""
    # add some foreign sids if they are not present already
    ldb.add_foreign(domaindn, "S-1-5-7", "Anonymous")
    ldb.add_foreign(domaindn, "S-1-1-0", "World")
    ldb.add_foreign(domaindn, "S-1-5-2", "Network")
    ldb.add_foreign(domaindn, "S-1-5-18", "System")
    ldb.add_foreign(domaindn, "S-1-5-11", "Authenticated Users")

    # some well known sids
    ldb.setup_name_mapping(domaindn, "S-1-5-7", nobody)
    ldb.setup_name_mapping(domaindn, "S-1-1-0", nogroup)
    ldb.setup_name_mapping(domaindn, "S-1-5-2", nogroup)
    ldb.setup_name_mapping(domaindn, "S-1-5-18", root)
    ldb.setup_name_mapping(domaindn, "S-1-5-11", users)
    ldb.setup_name_mapping(domaindn, "S-1-5-32-544", wheel)
    ldb.setup_name_mapping(domaindn, "S-1-5-32-545", users)
    ldb.setup_name_mapping(domaindn, "S-1-5-32-546", nogroup)
    ldb.setup_name_mapping(domaindn, "S-1-5-32-551", backup)

    # and some well known domain rids
    ldb.setup_name_mapping(domaindn, sid + "-500", root)
    ldb.setup_name_mapping(domaindn, sid + "-518", wheel)
    ldb.setup_name_mapping(domaindn, sid + "-519", wheel)
    ldb.setup_name_mapping(domaindn, sid + "-512", wheel)
    ldb.setup_name_mapping(domaindn, sid + "-513", users)
    ldb.setup_name_mapping(domaindn, sid + "-520", wheel)


def provision_become_dc(setup_dir, message, paths, lp, session_info, 
                        credentials):
    assert session_info is not None
    erase = False

    def setup_path(file):
        return os.path.join(setup_dir, file)
    os.path.unlink(paths.samdb)

    message("Setting up templates db")
    setup_templatesdb(paths.templates, setup_path, session_info, 
                      credentials, lp)

    # Also wipes the database
    message("Setting up sam.ldb")
    samdb = SamDB(paths.samdb, credentials=credentials, 
                  session_info=session_info, lp=lp)

    message("Setting up sam.ldb partitions")
    setup_samdb_partitions(samdb, setup_path, schemadn, 
                       configdn, domaindn)

    samdb = SamDB(paths.samdb, credentials=credentials, 
                  session_info=session_info, lp=lp)

    ldb.transaction_start()
    try:
        message("Setting up sam.ldb attributes")
        samdb.load_ldif_file_add(setup_path("provision_init.ldif"))

        message("Setting up sam.ldb rootDSE")
        setup_samdb_rootdse(samdb, setup_path, schemadn, domaindn, 
                            hostname, dnsdomain, realm, rootdn, configdn, 
                            netbiosname)

        if erase:
            message("Erasing data from partitions")
            samdb.erase_partitions()

        message("Setting up sam.ldb indexes")
        samdb.load_ldif_file_add(setup_path("provision_index.ldif"))
    except:
        samdb.transaction_cancel()
        raise

    samdb.transaction_commit()

    message("Setting up %s" % paths.secrets)
    secrets_ldb = setup_secretsdb(paths.secrets, setup_path, session_info, credentials, lp)
    setup_ldb(secrets_ldb, setup_path("secrets_dc.ldif"), 
              { "MACHINEPASS_B64": b64encode(machinepass) })


def setup_secretsdb(path, setup_path, session_info, credentials, lp):
    secrets_ldb = Ldb(path, session_info=session_info, credentials=credentials, lp=lp)
    secrets_ldb.erase()
    secrets_ldb.load_ldif_file_add(setup_path("secrets_init.ldif"))
    secrets_ldb.load_ldif_file_add(setup_path("secrets.ldif"))
    return secrets_ldb


def setup_templatesdb(path, setup_path, session_info, credentials, lp):
    templates_ldb = SamDB(path, session_info=session_info,
                        credentials=credentials, lp=lp)
    templates_ldb.erase()
    templates_ldb.load_ldif_file_add(setup_path("provision_templates.ldif"))


def setup_registry(path, setup_path, session_info, credentials, lp):
    reg = registry.Registry()
    hive = registry.Hive(path, session_info=session_info, 
                         credentials=credentials, lp_ctx=lp)
    reg.mount_hive(hive, "HKEY_LOCAL_MACHINE")
    provision_reg = setup_path("provision.reg")
    assert os.path.exists(provision_reg)
    reg.apply_patchfile(provision_reg)


def setup_samdb_rootdse(samdb, setup_path, schemadn, domaindn, hostname, 
                        dnsdomain, realm, rootdn, configdn, netbiosname):
    setup_add_ldif(samdb, setup_path("provision_rootdse_add.ldif"), {
        "SCHEMADN": schemadn, 
        "NETBIOSNAME": netbiosname,
        "DNSDOMAIN": dnsdomain,
        "DEFAULTSITE": DEFAULTSITE,
        "REALM": realm,
        "DNSNAME": "%s.%s" % (hostname, dnsdomain),
        "DOMAINDN": domaindn,
        "ROOTDN": rootdn,
        "CONFIGDN": configdn,
        "VERSION": samba.version(),
        })


def setup_samdb_partitions(samdb, setup_path, schemadn, configdn, domaindn):
    #Add modules to the list to activate them by default
    #beware often order is important
    #
    # Some Known ordering constraints:
    # - rootdse must be first, as it makes redirects from "" -> cn=rootdse
    # - objectclass must be before password_hash, because password_hash checks
    #   that the objectclass is of type person (filled in by objectclass
    #   module when expanding the objectclass list)
    # - partition must be last
    # - each partition has its own module list then
    modules_list = ["rootdse",
                    "paged_results",
                    "ranged_results",
                    "anr",
                    "server_sort",
                    "extended_dn",
                    "asq",
                    "samldb",
                    "rdn_name",
                    "objectclass",
                    "kludge_acl",
                    "operational"]
    tdb_modules_list = [
                    "subtree_rename",
                    "subtree_delete",
                    "linked_attributes"]
    modules_list2 = ["show_deleted",
                    "partition"]
 
    setup_add_ldif(samdb, setup_path("provision_partitions.ldif"), {
        "SCHEMADN": schemadn, 
        "SCHEMADN_LDB": "schema.ldb",
        "SCHEMADN_MOD2": ",objectguid",
        "CONFIGDN": configdn,
        "CONFIGDN_LDB": "configuration.ldb",
        "DOMAINDN": domaindn,
        "DOMAINDN_LDB": "users.ldb",
        "SCHEMADN_MOD": "schema_fsmo",
        "CONFIGDN_MOD": "naming_fsmo",
        "CONFIGDN_MOD2": ",objectguid",
        "DOMAINDN_MOD": "pdc_fsmo,password_hash",
        "DOMAINDN_MOD2": ",objectguid",
        "MODULES_LIST": ",".join(modules_list),
        "TDB_MODULES_LIST": ","+",".join(tdb_modules_list),
        "MODULES_LIST2": ",".join(modules_list2),
        })



def provision(lp, setup_dir, message, blank, paths, session_info, 
              credentials, ldapbackend, realm=None, domain=None, hostname=None, 
              hostip=None, domainsid=None, hostguid=None, adminpass=None, 
              krbtgtpass=None, domainguid=None, policyguid=None, 
              invocationid=None, machinepass=None, dnspass=None, root=None,
              nobody=None, nogroup=None, users=None, wheel=None, backup=None, 
              aci=None, serverrole=None):
    """Provision samba4
    
    :note: caution, this wipes all existing data!
    """

    def setup_path(file):
        return os.path.join(setup_dir, file)

    erase = False

    if domainsid is None:
        domainsid = security.random_sid()
    if policyguid is None:
        policyguid = uuid.random()
    if invocationid is None:
        invocationid = uuid.random()
    if adminpass is None:
        adminpass = misc.random_password(12)
    if krbtgtpass is None:
        krbtgtpass = misc.random_password(12)
    if machinepass is None:
        machinepass  = misc.random_password(12)
    if dnspass is None:
        dnspass = misc.random_password(12)
    if root is None:
        root = findnss(pwd.getpwnam, "root")[4]
    if nobody is None:
        nobody = findnss(pwd.getpwnam, "nobody")[4]
    if nogroup is None:
        nogroup = findnss(grp.getgrnam, "nogroup", "nobody")[2]
    if users is None:
        users = findnss(grp.getgrnam, "users", "guest", "other", "unknown", "usr")[2]
    if wheel is None:
        wheel = findnss(grp.getgrnam, "wheel", "root", "staff", "adm")[2]
    if backup is None:
        backup = findnss(grp.getgrnam, "backup", "wheel", "root", "staff")[2]
    if aci is None:
        aci = "# no aci for local ldb"
    if serverrole is None:
        serverrole = lp.get("server role")

    if realm is None:
        realm = lp.get("realm")
    else:
        if lp.get("realm").upper() != realm.upper():
            raise Error("realm '%s' in smb.conf must match chosen realm '%s'\n" %
                (lp.get("realm"), realm))

    assert realm is not None
    realm = realm.upper()

    if domain is None:
        domain = lp.get("workgroup")
    else:
        if lp.get("workgroup").upper() != domain.upper():
            raise Error("workgroup '%s' in smb.conf must match chosen domain '%s'\n",
                lp.get("workgroup"), domain)

    assert domain is not None
    domain = domain.upper()
    if not valid_netbios_name(domain):
        raise InvalidNetbiosName(domain)

    if hostname is None:
        hostname = gethostname().split(".")[0].lower()

    if hostip is None:
        hostip = gethostbyname(hostname)

    netbiosname = hostname.upper()
    if not valid_netbios_name(netbiosname):
        raise InvalidNetbiosName(netbiosname)

    dnsdomain    = realm.lower()
    domaindn     = "DC=" + dnsdomain.replace(".", ",DC=")
    rootdn       = domaindn
    configdn     = "CN=Configuration," + rootdn
    schemadn     = "CN=Schema," + configdn

    rdn_dc = domaindn.split(",")[0][len("DC="):]

    message("set DOMAIN SID: %s" % str(domainsid))
    message("Provisioning for %s in realm %s" % (domain, realm))
    message("Using administrator password: %s" % adminpass)

    assert paths.smbconf is not None

    # only install a new smb.conf if there isn't one there already
    if not os.path.exists(paths.smbconf):
        message("Setting up smb.conf")
        if serverrole == "domain controller":
            smbconfsuffix = "dc"
        elif serverrole == "member":
            smbconfsuffix = "member"
        else:
            assert "Invalid server role setting: %s" % serverrole
        setup_file(setup_path("provision.smb.conf.%s" % smbconfsuffix), paths.smbconf, {
            "HOSTNAME": hostname,
            "DOMAIN_CONF": domain,
            "REALM_CONF": realm,
            "SERVERROLE": serverrole,
            "NETLOGONPATH": paths.netlogon,
            "SYSVOLPATH": paths.sysvol,
            })
        lp.reload()

    # only install a new shares config db if there is none
    if not os.path.exists(paths.shareconf):
        message("Setting up share.ldb")
        share_ldb = Ldb(paths.shareconf, session_info=session_info, 
                        credentials=credentials, lp=lp)
        share_ldb.load_ldif_file_add(setup_path("share.ldif"))

    message("Setting up secrets.ldb")
    secrets_ldb = setup_secretsdb(paths.secrets, setup_path, session_info=session_info, 
                    credentials=credentials, lp=lp)

    message("Setting up the registry")
    # FIXME: Still fails for some reason
    #setup_registry(paths.hklm, setup_path, session_info, 
    #               credentials=credentials, lp=lp)

    message("Setting up templates db")
    setup_templatesdb(paths.templates, setup_path, session_info=session_info, 
                      credentials=credentials, lp=lp)

    samdb = SamDB(paths.samdb, session_info=session_info, 
                  credentials=credentials, lp=lp)

    message("Setting up sam.ldb partitions")
    setup_samdb_partitions(samdb, setup_path, schemadn, configdn, domaindn)

    samdb = SamDB(paths.samdb, session_info=session_info, 
                  credentials=credentials, lp=lp)

    samdb.transaction_start()
    try:
        message("Setting up sam.ldb attributes")
        samdb.load_ldif_file_add(setup_path("provision_init.ldif"))

        message("Setting up sam.ldb rootDSE")
        setup_samdb_rootdse(samdb, setup_path, schemadn, domaindn, 
                            hostname, dnsdomain, realm, rootdn, configdn, 
                            netbiosname)

        if erase:
            message("Erasing data from partitions")
            samdb.erase_partitions()
    except:
        samdb.transaction_cancel()
        raise

    samdb.transaction_commit()

    message("Pre-loading the Samba 4 and AD schema")
    samdb = SamDB(paths.samdb, session_info=session_info, 
                  credentials=credentials, lp=lp)
    samdb.set_domain_sid(domainsid)
    load_schema(setup_path, samdb, schemadn, netbiosname, configdn)

    samdb.transaction_start()
        
    try:
        message("Adding DomainDN: %s (permitted to fail)" % domaindn)
        setup_add_ldif(samdb, setup_path("provision_basedn.ldif"), {
            "DOMAINDN": domaindn,
            "ACI": aci,
            "EXTENSIBLEOBJECT": "# no objectClass: extensibleObject for local ldb",
            "RDN_DC": rdn_dc,
            })

        message("Modifying DomainDN: " + domaindn + "")
        if domainguid is not None:
            domainguid_mod = "replace: objectGUID\nobjectGUID: %s\n-" % domainguid
        else:
            domainguid_mod = ""

        setup_modify_ldif(samdb, setup_path("provision_basedn_modify.ldif"), {
            "RDN_DC": rdn_dc,
            "LDAPTIME": timestring(int(time.time())),
            "DOMAINSID": str(domainsid),
            "SCHEMADN": schemadn, 
            "NETBIOSNAME": netbiosname,
            "DEFAULTSITE": DEFAULTSITE,
            "CONFIGDN": configdn,
            "POLICYGUID": policyguid,
            "DOMAINDN": domaindn,
            "DOMAINGUID_MOD": domainguid_mod,
            })

        message("Adding configuration container (permitted to fail)")
        setup_add_ldif(samdb, setup_path("provision_configuration_basedn.ldif"), {
            "CONFIGDN": configdn, 
            "ACI": aci,
            "EXTENSIBLEOBJECT": "# no objectClass: extensibleObject for local ldb",
            })
        message("Modifying configuration container")
        setup_modify_ldif(samdb, setup_path("provision_configuration_basedn_modify.ldif"), {
            "CONFIGDN": configdn, 
            "SCHEMADN": schemadn,
            })

        message("Adding schema container (permitted to fail)")
        setup_add_ldif(samdb, setup_path("provision_schema_basedn.ldif"), {
            "SCHEMADN": schemadn,
            "ACI": aci,
            "EXTENSIBLEOBJECT": "# no objectClass: extensibleObject for local ldb"
            })
        message("Modifying schema container")
        setup_modify_ldif(samdb, setup_path("provision_schema_basedn_modify.ldif"), {
            "SCHEMADN": schemadn,
            "NETBIOSNAME": netbiosname,
            "DEFAULTSITE": DEFAULTSITE,
            "CONFIGDN": configdn,
            })

        message("Setting up sam.ldb Samba4 schema")
        setup_add_ldif(samdb, setup_path("schema_samba4.ldif"), 
                       {"SCHEMADN": schemadn })
        message("Setting up sam.ldb AD schema")
        setup_add_ldif(samdb, setup_path("schema.ldif"), 
                       {"SCHEMADN": schemadn})

        message("Setting up sam.ldb configuration data")
        setup_add_ldif(samdb, setup_path("provision_configuration.ldif"), {
            "CONFIGDN": configdn,
            "NETBIOSNAME": netbiosname,
            "DEFAULTSITE": DEFAULTSITE,
            "DNSDOMAIN": dnsdomain,
            "DOMAIN": domain,
            "SCHEMADN": schemadn,
            "DOMAINDN": domaindn,
            })

        message("Setting up display specifiers")
        setup_add_ldif(samdb, setup_path("display_specifiers.ldif"), 
                       {"CONFIGDN": configdn})

        message("Adding users container (permitted to fail)")
        setup_add_ldif(samdb, setup_path("provision_users_add.ldif"), {
            "DOMAINDN": domaindn})
        message("Modifying users container")
        setup_modify_ldif(samdb, setup_path("provision_users_modify.ldif"), {
            "DOMAINDN": domaindn})
        message("Adding computers container (permitted to fail)")
        setup_add_ldif(samdb, setup_path("provision_computers_add.ldif"), {
            "DOMAINDN": domaindn})
        message("Modifying computers container")
        setup_modify_ldif(samdb, setup_path("provision_computers_modify.ldif"), {
            "DOMAINDN": domaindn})
        message("Setting up sam.ldb data")
        setup_add_ldif(samdb, setup_path("provision.ldif"), {
            "DOMAINDN": domaindn,
            "NETBIOSNAME": netbiosname,
            "DEFAULTSITE": DEFAULTSITE,
            "CONFIGDN": configdn,
            })

        if not blank:

    #    message("Activate schema module")
    #    setup_modify_ldif("schema_activation.ldif", info, samdb, False)
    #
    #    // (hack) Reload, now we have the schema loaded.  
    #    commit_ok = samdb.transaction_commit()
    #    if (!commit_ok) {
    #        message("samdb commit failed: " + samdb.errstring() + "\n")
    #        assert(commit_ok)
    #    }
    #    samdb.close()
    #
    #    samdb = open_ldb(info, paths.samdb, False)
    #
            message("Setting up sam.ldb users and groups")
            setup_add_ldif(samdb, setup_path("provision_users.ldif"), {
                "DOMAINDN": domaindn,
                "DOMAINSID": str(domainsid),
                "CONFIGDN": configdn,
                "ADMINPASS_B64": b64encode(adminpass),
                "KRBTGTPASS_B64": b64encode(krbtgtpass),
                })

            if lp.get("server role") == "domain controller":
                message("Setting up self join")
                if hostguid is not None:
                    hostguid_add = "objectGUID: %s" % hostguid
                else:
                    hostguid_add = ""

                setup_add_ldif(samdb, setup_path("provision_self_join.ldif"), { 
                          "CONFIGDN": configdn, 
                          "SCHEMADN": schemadn,
                          "DOMAINDN": domaindn,
                          "INVOCATIONID": invocationid,
                          "NETBIOSNAME": netbiosname,
                          "DEFAULTSITE": DEFAULTSITE,
                          "DNSNAME": "%s.%s" % (hostname, dnsdomain),
                          "MACHINEPASS_B64": b64encode(machinepass),
                          "DNSPASS_B64": b64encode(dnspass),
                          "REALM": realm,
                          "DOMAIN": domain,
                          "HOSTGUID_ADD": hostguid_add,
                          "DNSDOMAIN": dnsdomain})
                setup_add_ldif(samdb, setup_path("provision_group_policy.ldif"), { 
                          "POLICYGUID": policyguid,
                          "DNSDOMAIN": dnsdomain,
                          "DOMAINSID": str(domainsid),
                          "DOMAINDN": domaindn})

                os.makedirs(os.path.join(paths.sysvol, dnsdomain, "Policies", "{" + policyguid + "}"), 0755)
                os.makedirs(os.path.join(paths.sysvol, dnsdomain, "Policies", "{" + policyguid + "}", "Machine"), 0755)
                os.makedirs(os.path.join(paths.sysvol, dnsdomain, "Policies", "{" + policyguid + "}", "User"), 0755)
                if not os.path.isdir(paths.netlogon):
                    os.makedirs(paths.netlogon, 0755)
                setup_ldb(secrets_ldb, setup_path("secrets_dc.ldif"), { 
                    "MACHINEPASS_B64": b64encode(machinepass),
                    "DOMAIN": domain,
                    "REALM": realm,
                    "LDAPTIME": timestring(int(time.time())),
                    "DNSDOMAIN": dnsdomain,
                    "DOMAINSID": str(domainsid),
                    "SECRETS_KEYTAB": paths.keytab,
                    "NETBIOSNAME": netbiosname,
                    "SAM_LDB": paths.samdb,
                    "DNS_KEYTAB": paths.dns_keytab,
                    "DNSPASS_B64": b64encode(dnspass),
                    })

            setup_name_mappings(samdb, str(domainsid), 
                                domaindn, root=root, nobody=nobody, 
                                nogroup=nogroup, wheel=wheel, users=users,
                                backup=backup)

        message("Setting up sam.ldb index")
        samdb.load_ldif_file_add(setup_path("provision_index.ldif"))

        message("Setting up sam.ldb rootDSE marking as syncronized")
        setup_modify_ldif(samdb, setup_path("provision_rootdse_modify.ldif"))
    except:
        samdb.transaction_cancel()
        raise

    samdb.transaction_commit()

    message("Setting up phpLDAPadmin configuration")
    create_phplpapdadmin_config(paths.phpldapadminconfig, setup_path, paths.s4_ldapi_path)

    message("Please install the phpLDAPadmin configuration located at %s into /etc/phpldapadmin/config.php" % paths.phpldapadminconfig)

    message("Setting up DNS zone: %s" % dnsdomain)
    create_zone_file(paths.dns, setup_path, samdb, 
                  hostname=hostname, hostip=hostip, dnsdomain=dnsdomain,
                  domaindn=domaindn, dnspass=dnspass, realm=realm)
    message("Please install the zone located in %s into your DNS server" % paths.dns)

def create_phplpapdadmin_config(path, setup_path, s4_ldapi_path):
    setup_file(setup_path("phpldapadmin-config.php"), 
               path, {"S4_LDAPI_URI": "ldapi://%s" % s4_ldapi_path.replace("/", "%2F")})


def create_zone_file(path, setup_path, samdb, dnsdomain, domaindn, 
                  hostip, hostname, dnspass, realm):
    """Write out a DNS zone file, from the info in the current database."""

    # connect to the sam
    # These values may have changed, due to an incoming SamSync,
    # or may not have been specified, so fetch them from the database
    domainguid = samdb.searchone(Dn(samdb, domaindn), "objectGUID")
    hostguid = samdb.searchone(Dn(samdb, domaindn), "objectGUID" ,
                     expression="(&(objectClass=computer)(cn=%s))" % hostname)

    setup_file(setup_path("provision.zone"), path, {
            "DNSPASS_B64": b64encode(dnspass),
            "HOSTNAME": hostname,
            "DNSDOMAIN": dnsdomain,
            "REALM": realm,
            "HOSTIP": hostip,
            "DOMAINGUID": domainguid,
            "DATESTRING": time.strftime("%Y%m%d%H"),
            "DEFAULTSITE": DEFAULTSITE,
            "HOSTGUID": hostguid,
        })


def provision_ldapbase(setup_dir, message, paths):
    """Write out a DNS zone file, from the info in the current database."""
    message("Setting up LDAP base entry: %s" % domaindn)
    rdns = domaindn.split(",")

    rdn_dc = rdns[0][len("DC="):]

    def setup_path(file):
        return os.path.join(setup_dir, file)

    setup_file(setup_path("provision_basedn.ldif"), 
           paths.ldap_basedn_ldif)

    setup_file(setup_path("provision_configuration_basedn.ldif"), 
           paths.ldap_config_basedn_ldif)

    setup_file(setup_path("provision_schema_basedn.ldif"), 
           paths.ldap_schema_basedn_ldif, {
            "SCHEMADN": schemadn,
            "ACI": "# no aci for local ldb",
            "EXTENSIBLEOBJECT": "objectClass: extensibleObject"})

    message("Please install the LDIF located in " + paths.ldap_basedn_ldif + ", " + paths.ldap_config_basedn_ldif + " and " + paths.ldap_schema_basedn_ldif + " into your LDAP server, and re-run with --ldap-backend=ldap://my.ldap.server")


def load_schema(setup_path, samdb, schemadn, netbiosname, configdn):
    """Load schema."""
    schema_data = open(setup_path("schema.ldif"), 'r').read()
    schema_data += open(setup_path("schema_samba4.ldif"), 'r').read()
    schema_data = substitute_var(schema_data, {"SCHEMADN": schemadn})
    head_data = open(setup_path("provision_schema_basedn_modify.ldif"), 'r').read()
    head_data = substitute_var(head_data, {
                    "SCHEMADN": schemadn,
                    "NETBIOSNAME": netbiosname,
                    "CONFIGDN": configdn,
                    "DEFAULTSITE": DEFAULTSITE})
    samdb.attach_schema_from_ldif(head_data, schema_data)


def join_domain(domain, netbios_name, join_type, creds):
    ctx = NetContext(creds)
    joindom = object()
    joindom.domain = domain
    joindom.join_type = join_type
    joindom.netbios_name = netbios_name
    if not ctx.JoinDomain(joindom):
        raise Exception("Domain Join failed: " + joindom.error_string)


def vampire(domain, session_info, credentials, message):
    """Vampire a remote domain.  
    
    Session info and credentials are required for for
    access to our local database (might be remote ldap)
    """
    ctx = NetContext(credentials)
    machine_creds = Credentials()
    machine_creds.set_domain(form.domain)
    if not machine_creds.set_machine_account():
        raise Exception("Failed to access domain join information!")
    vampire_ctx.machine_creds = machine_creds
    vampire_ctx.session_info = session_info
    if not ctx.SamSyncLdb(vampire_ctx):
        raise Exception("Migration of remote domain to Samba failed: %s " % vampire_ctx.error_string)



