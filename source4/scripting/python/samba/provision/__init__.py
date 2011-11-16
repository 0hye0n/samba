
# Unix SMB/CIFS implementation.
# backend code for provisioning a Samba4 server

# Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007-2010
# Copyright (C) Andrew Bartlett <abartlet@samba.org> 2008-2009
# Copyright (C) Oliver Liebel <oliver@itc.li> 2008-2009
#
# Based on the original in EJS:
# Copyright (C) Andrew Tridgell <tridge@samba.org> 2005
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

"""Functions for setting up a Samba configuration."""

__docformat__ = "restructuredText"

from base64 import b64encode
import os
import re
import pwd
import grp
import logging
import time
import uuid
import socket
import urllib
import shutil
import string

import ldb

from samba.auth import system_session, admin_session
import samba
from samba.dsdb import DS_DOMAIN_FUNCTION_2000
from samba import (
    Ldb,
    check_all_substituted,
    read_and_sub_file,
    setup_file,
    substitute_var,
    valid_netbios_name,
    version,
    )
from samba.dcerpc import security, misc
from samba.dcerpc.misc import (
    SEC_CHAN_BDC,
    SEC_CHAN_WKSTA,
    )
from samba.dsdb import (
    DS_DOMAIN_FUNCTION_2003,
    DS_DOMAIN_FUNCTION_2008_R2,
    ENC_ALL_TYPES,
    )
from samba.idmap import IDmapDB
from samba.ms_display_specifiers import read_ms_ldif
from samba.ntacls import setntacl, dsacl2fsacl
from samba.ndr import ndr_pack, ndr_unpack
from samba.provision.backend import (
    ExistingBackend,
    FDSBackend,
    LDBBackend,
    OpenLDAPBackend,
    )
from samba.provision.sambadns import setup_ad_dns, create_dns_update_list

import samba.param
import samba.registry
from samba.schema import Schema
from samba.samdb import SamDB
from samba.dbchecker import dbcheck


VALID_NETBIOS_CHARS = " !#$%&'()-.@^_{}~"
DEFAULT_POLICY_GUID = "31B2F340-016D-11D2-945F-00C04FB984F9"
DEFAULT_DC_POLICY_GUID = "6AC1786C-016F-11D2-945F-00C04fB984F9"
DEFAULTSITE = "Default-First-Site-Name"
LAST_PROVISION_USN_ATTRIBUTE = "lastProvisionUSN"


def setup_path(file):
    """Return an absolute path to the provision tempate file specified by file"""
    return os.path.join(samba.param.setup_dir(), file)

# Descriptors of naming contexts and other important objects

# "get_schema_descriptor" is located in "schema.py"

def get_config_descriptor(domain_sid):
    sddl = "O:EAG:EAD:(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
           "(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
           "(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
           "(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
           "(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
           "(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
           "(A;;RPLCLORC;;;AU)(A;CI;RPWPCRCCDCLCLORCWOWDSDDTSW;;;EA)" \
           "(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;SY)(A;CIIO;RPWPCRCCLCLORCWOWDSDSW;;;DA)" \
           "(OA;;CR;1131f6ad-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
           "(OA;;CR;89e95b76-444d-4c62-991a-0facbeda640c;;ED)" \
           "(OA;;CR;1131f6ad-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
           "(OA;;CR;89e95b76-444d-4c62-991a-0facbeda640c;;BA)" \
           "(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;ER)" \
           "S:(AU;SA;WPWOWD;;;WD)(AU;SA;CR;;;BA)(AU;SA;CR;;;DU)" \
           "(OU;SA;CR;45ec5156-db7e-47bb-b53f-dbeb2d03c40f;;WD)"
    sec = security.descriptor.from_sddl(sddl, domain_sid)
    return ndr_pack(sec)


def get_domain_descriptor(domain_sid):
    sddl= "O:BAG:BAD:AI(OA;CIIO;RP;4c164200-20c0-11d0-a768-00aa006e0529;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
        "(OA;CIIO;RP;4c164200-20c0-11d0-a768-00aa006e0529;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;CIIO;RP;5f202010-79a5-11d0-9020-00c04fc2d4cf;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
    "(OA;CIIO;RP;5f202010-79a5-11d0-9020-00c04fc2d4cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;CIIO;RP;bc0ac240-79a9-11d0-9020-00c04fc2d4cf;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
    "(OA;CIIO;RP;bc0ac240-79a9-11d0-9020-00c04fc2d4cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;CIIO;RP;59ba2f42-79a2-11d0-9020-00c04fc2d3cf;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
    "(OA;CIIO;RP;59ba2f42-79a2-11d0-9020-00c04fc2d3cf;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;CIIO;RP;037088f8-0ae1-11d2-b422-00a0c968f939;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
    "(OA;CIIO;RP;037088f8-0ae1-11d2-b422-00a0c968f939;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;ER)" \
    "(OA;;CR;1131f6ad-9c07-11d1-f79f-00c04fc2dcd2;;DD)" \
    "(OA;CIIO;RP;b7c69e6d-2cc7-11d2-854e-00a0c983f608;bf967a86-0de6-11d0-a285-00aa003049e2;ED)" \
    "(OA;CIIO;RP;b7c69e6d-2cc7-11d2-854e-00a0c983f608;bf967a9c-0de6-11d0-a285-00aa003049e2;ED)" \
    "(OA;CIIO;RP;b7c69e6d-2cc7-11d2-854e-00a0c983f608;bf967aba-0de6-11d0-a285-00aa003049e2;ED)" \
    "(OA;;CR;89e95b76-444d-4c62-991a-0facbeda640c;;BA)" \
    "(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
    "(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
    "(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
    "(OA;;CR;1131f6ad-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
    "(OA;;CR;1131f6ae-9c07-11d1-f79f-00c04fc2dcd2;;BA)" \
    "(OA;;CR;e2a36dc9-ae17-47c3-b58b-be34c55ba633;;IF)" \
    "(OA;;RP;c7407360-20bf-11d0-a768-00aa006e0529;;RU)" \
    "(OA;;RP;b8119fd0-04f6-4762-ab7a-4986c76b3f9a;;RU)" \
    "(OA;CIIO;RPLCLORC;;4828cc14-1437-45bc-9b07-ad6f015e5f28;RU)" \
    "(OA;CIIO;RPLCLORC;;bf967a9c-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;CIIO;RPLCLORC;;bf967aba-0de6-11d0-a285-00aa003049e2;RU)" \
    "(OA;;CR;05c74c5e-4deb-43b4-bd9f-86664c2a7fd5;;AU)" \
    "(OA;;CR;89e95b76-444d-4c62-991a-0facbeda640c;;ED)" \
    "(OA;;CR;ccc2dc7d-a6ad-4a7a-8846-c04e3cc53501;;AU)" \
    "(OA;;CR;280f369c-67c7-438e-ae98-1d46f3c6f541;;AU)" \
    "(OA;;CR;1131f6aa-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
    "(OA;;CR;1131f6ab-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
    "(OA;;CR;1131f6ac-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
    "(OA;;CR;1131f6ae-9c07-11d1-f79f-00c04fc2dcd2;;ED)" \
    "(OA;;RP;b8119fd0-04f6-4762-ab7a-4986c76b3f9a;;AU)" \
    "(OA;CIIO;RPWPCR;91e647de-d96f-4b70-9557-d63ff4f3ccd8;;PS)" \
    "(A;;RPWPCRCCLCLORCWOWDSW;;;DA)" \
    "(A;CI;RPWPCRCCDCLCLORCWOWDSDDTSW;;;EA)" \
    "(A;;RPRC;;;RU)" \
    "(A;CI;LC;;;RU)" \
    "(A;CI;RPWPCRCCLCLORCWOWDSDSW;;;BA)" \
    "(A;;RP;;;WD)" \
    "(A;;RPLCLORC;;;ED)" \
    "(A;;RPLCLORC;;;AU)" \
    "(A;;RPWPCRCCDCLCLORCWOWDSDDTSW;;;SY)" \
    "S:AI(OU;CISA;WP;f30e3bbe-9ff0-11d1-b603-0000f80367c1;bf967aa5-0de6-11d0-a285-00aa003049e2;WD)" \
    "(OU;CISA;WP;f30e3bbf-9ff0-11d1-b603-0000f80367c1;bf967aa5-0de6-11d0-a285-00aa003049e2;WD)" \
    "(AU;SA;CR;;;DU)(AU;SA;CR;;;BA)(AU;SA;WPWOWD;;;WD)"
    sec = security.descriptor.from_sddl(sddl, domain_sid)
    return ndr_pack(sec)


class ProvisionPaths(object):

    def __init__(self):
        self.shareconf = None
        self.hklm = None
        self.hkcu = None
        self.hkcr = None
        self.hku = None
        self.hkpd = None
        self.hkpt = None
        self.samdb = None
        self.idmapdb = None
        self.secrets = None
        self.keytab = None
        self.dns_keytab = None
        self.dns = None
        self.winsdb = None
        self.private_dir = None


class ProvisionNames(object):

    def __init__(self):
        self.rootdn = None
        self.domaindn = None
        self.configdn = None
        self.schemadn = None
        self.ldapmanagerdn = None
        self.dnsdomain = None
        self.realm = None
        self.netbiosname = None
        self.domain = None
        self.hostname = None
        self.sitename = None
        self.smbconf = None

def find_provision_key_parameters(samdb, secretsdb, idmapdb, paths, smbconf, lp):
    """Get key provision parameters (realm, domain, ...) from a given provision

    :param samdb: An LDB object connected to the sam.ldb file
    :param secretsdb: An LDB object connected to the secrets.ldb file
    :param idmapdb: An LDB object connected to the idmap.ldb file
    :param paths: A list of path to provision object
    :param smbconf: Path to the smb.conf file
    :param lp: A LoadParm object
    :return: A list of key provision parameters
    """
    names = ProvisionNames()
    names.adminpass = None

    # NT domain, kerberos realm, root dn, domain dn, domain dns name
    names.domain = string.upper(lp.get("workgroup"))
    names.realm = lp.get("realm")
    names.dnsdomain = names.realm.lower()
    basedn = samba.dn_from_dns_name(names.dnsdomain)
    names.realm = string.upper(names.realm)
    # netbiosname
    # Get the netbiosname first (could be obtained from smb.conf in theory)
    res = secretsdb.search(expression="(flatname=%s)" %
                            names.domain,base="CN=Primary Domains",
                            scope=ldb.SCOPE_SUBTREE, attrs=["sAMAccountName"])
    names.netbiosname = str(res[0]["sAMAccountName"]).replace("$","")

    names.smbconf = smbconf

    # That's a bit simplistic but it's ok as long as we have only 3
    # partitions
    current = samdb.search(expression="(objectClass=*)",
        base="", scope=ldb.SCOPE_BASE,
        attrs=["defaultNamingContext", "schemaNamingContext",
               "configurationNamingContext","rootDomainNamingContext"])

    names.configdn = current[0]["configurationNamingContext"]
    configdn = str(names.configdn)
    names.schemadn = current[0]["schemaNamingContext"]
    if not (ldb.Dn(samdb, basedn) == (ldb.Dn(samdb,
                                       current[0]["defaultNamingContext"][0]))):
        raise ProvisioningError(("basedn in %s (%s) and from %s (%s)"
                                 "is not the same ..." % (paths.samdb,
                                    str(current[0]["defaultNamingContext"][0]),
                                    paths.smbconf, basedn)))

    names.domaindn=current[0]["defaultNamingContext"]
    names.rootdn=current[0]["rootDomainNamingContext"]
    # default site name
    res3 = samdb.search(expression="(objectClass=site)",
        base="CN=Sites," + configdn, scope=ldb.SCOPE_ONELEVEL, attrs=["cn"])
    names.sitename = str(res3[0]["cn"])

    # dns hostname and server dn
    res4 = samdb.search(expression="(CN=%s)" % names.netbiosname,
                            base="OU=Domain Controllers,%s" % basedn,
                            scope=ldb.SCOPE_ONELEVEL, attrs=["dNSHostName"])
    names.hostname = str(res4[0]["dNSHostName"]).replace("." + names.dnsdomain,"")

    server_res = samdb.search(expression="serverReference=%s" % res4[0].dn,
                                attrs=[], base=configdn)
    names.serverdn = server_res[0].dn

    # invocation id/objectguid
    res5 = samdb.search(expression="(objectClass=*)",
            base="CN=NTDS Settings,%s" % str(names.serverdn), scope=ldb.SCOPE_BASE,
            attrs=["invocationID", "objectGUID"])
    names.invocation = str(ndr_unpack(misc.GUID, res5[0]["invocationId"][0]))
    names.ntdsguid = str(ndr_unpack(misc.GUID, res5[0]["objectGUID"][0]))

    # domain guid/sid
    res6 = samdb.search(expression="(objectClass=*)", base=basedn,
            scope=ldb.SCOPE_BASE, attrs=["objectGUID",
                "objectSid","msDS-Behavior-Version" ])
    names.domainguid = str(ndr_unpack(misc.GUID, res6[0]["objectGUID"][0]))
    names.domainsid = ndr_unpack( security.dom_sid, res6[0]["objectSid"][0])
    if res6[0].get("msDS-Behavior-Version") is None or \
        int(res6[0]["msDS-Behavior-Version"][0]) < DS_DOMAIN_FUNCTION_2000:
        names.domainlevel = DS_DOMAIN_FUNCTION_2000
    else:
        names.domainlevel = int(res6[0]["msDS-Behavior-Version"][0])

    # policy guid
    res7 = samdb.search(expression="(displayName=Default Domain Policy)",
                        base="CN=Policies,CN=System," + basedn,
                        scope=ldb.SCOPE_ONELEVEL, attrs=["cn","displayName"])
    names.policyid = str(res7[0]["cn"]).replace("{","").replace("}","")
    # dc policy guid
    res8 = samdb.search(expression="(displayName=Default Domain Controllers"
                                   " Policy)",
                            base="CN=Policies,CN=System," + basedn,
                            scope=ldb.SCOPE_ONELEVEL, attrs=["cn","displayName"])
    if len(res8) == 1:
        names.policyid_dc = str(res8[0]["cn"]).replace("{","").replace("}","")
    else:
        names.policyid_dc = None
    res9 = idmapdb.search(expression="(cn=%s)" %
                            (security.SID_BUILTIN_ADMINISTRATORS),
                            attrs=["xidNumber"])
    if len(res9) == 1:
        names.wheel_gid = res9[0]["xidNumber"]
    else:
        raise ProvisioningError("Unable to find uid/gid for Domain Admins rid")
    return names

def update_provision_usn(samdb, low, high, id, replace=False):
    """Update the field provisionUSN in sam.ldb

    This field is used to track range of USN modified by provision and
    upgradeprovision.
    This value is used afterward by next provision to figure out if
    the field have been modified since last provision.

    :param samdb: An LDB object connect to sam.ldb
    :param low: The lowest USN modified by this upgrade
    :param high: The highest USN modified by this upgrade
    :param id: The invocation id of the samba's dc
    :param replace: A boolean indicating if the range should replace any
                    existing one or appended (default)
    """

    tab = []
    if not replace:
        entry = samdb.search(base="@PROVISION",
                             scope=ldb.SCOPE_BASE,
                             attrs=[LAST_PROVISION_USN_ATTRIBUTE, "dn"])
        for e in entry[0][LAST_PROVISION_USN_ATTRIBUTE]:
            if not re.search(';', e):
                e = "%s;%s" % (e, id)
            tab.append(str(e))

    tab.append("%s-%s;%s" % (low, high, id))
    delta = ldb.Message()
    delta.dn = ldb.Dn(samdb, "@PROVISION")
    delta[LAST_PROVISION_USN_ATTRIBUTE] = ldb.MessageElement(tab,
        ldb.FLAG_MOD_REPLACE, LAST_PROVISION_USN_ATTRIBUTE)
    entry = samdb.search(expression='provisionnerID=*',
                         base="@PROVISION", scope=ldb.SCOPE_BASE,
                         attrs=["provisionnerID"])
    if len(entry) == 0 or len(entry[0]) == 0:
        delta["provisionnerID"] = ldb.MessageElement(id, ldb.FLAG_MOD_ADD, "provisionnerID")
    samdb.modify(delta)


def set_provision_usn(samdb, low, high, id):
    """Set the field provisionUSN in sam.ldb
    This field is used to track range of USN modified by provision and
    upgradeprovision.
    This value is used afterward by next provision to figure out if
    the field have been modified since last provision.

    :param samdb: An LDB object connect to sam.ldb
    :param low: The lowest USN modified by this upgrade
    :param high: The highest USN modified by this upgrade
    :param id: The invocationId of the provision"""

    tab = []
    tab.append("%s-%s;%s" % (low, high, id))

    delta = ldb.Message()
    delta.dn = ldb.Dn(samdb, "@PROVISION")
    delta[LAST_PROVISION_USN_ATTRIBUTE] = ldb.MessageElement(tab,
        ldb.FLAG_MOD_ADD, LAST_PROVISION_USN_ATTRIBUTE)
    samdb.add(delta)


def get_max_usn(samdb,basedn):
    """ This function return the biggest USN present in the provision

    :param samdb: A LDB object pointing to the sam.ldb
    :param basedn: A string containing the base DN of the provision
                    (ie. DC=foo, DC=bar)
    :return: The biggest USN in the provision"""

    res = samdb.search(expression="objectClass=*",base=basedn,
                         scope=ldb.SCOPE_SUBTREE,attrs=["uSNChanged"],
                         controls=["search_options:1:2",
                                   "server_sort:1:1:uSNChanged",
                                   "paged_results:1:1"])
    return res[0]["uSNChanged"]


def get_last_provision_usn(sam):
    """Get USNs ranges modified by a provision or an upgradeprovision

    :param sam: An LDB object pointing to the sam.ldb
    :return: a dictionnary which keys are invocation id and values are an array
             of integer representing the different ranges
    """
    try:
        entry = sam.search(expression="%s=*" % LAST_PROVISION_USN_ATTRIBUTE,
                           base="@PROVISION", scope=ldb.SCOPE_BASE,
                           attrs=[LAST_PROVISION_USN_ATTRIBUTE, "provisionnerID"])
    except ldb.LdbError, (ecode, emsg):
        if ecode == ldb.ERR_NO_SUCH_OBJECT:
            return None
        raise
    if len(entry):
        myids = []
        range = {}
        p = re.compile(r'-')
        if entry[0].get("provisionnerID"):
            for e in entry[0]["provisionnerID"]:
                myids.append(str(e))
        for r in entry[0][LAST_PROVISION_USN_ATTRIBUTE]:
            tab1 = str(r).split(';')
            if len(tab1) == 2:
                id = tab1[1]
            else:
                id = "default"
            if (len(myids) > 0 and id not in myids):
                continue
            tab2 = p.split(tab1[0])
            if range.get(id) == None:
                range[id] = []
            range[id].append(tab2[0])
            range[id].append(tab2[1])
        return range
    else:
        return None


class ProvisionResult(object):

    def __init__(self):
        self.paths = None
        self.domaindn = None
        self.lp = None
        self.samdb = None
        self.idmap = None
        self.names = None


def check_install(lp, session_info, credentials):
    """Check whether the current install seems ok.

    :param lp: Loadparm context
    :param session_info: Session information
    :param credentials: Credentials
    """
    if lp.get("realm") == "":
        raise Exception("Realm empty")
    samdb = Ldb(lp.samdb_url(), session_info=session_info,
            credentials=credentials, lp=lp)
    if len(samdb.search("(cn=Administrator)")) != 1:
        raise ProvisioningError("No administrator account found")


def findnss(nssfn, names):
    """Find a user or group from a list of possibilities.

    :param nssfn: NSS Function to try (should raise KeyError if not found)
    :param names: Names to check.
    :return: Value return by first names list.
    """
    for name in names:
        try:
            return nssfn(name)
        except KeyError:
            pass
    raise KeyError("Unable to find user/group in %r" % names)


findnss_uid = lambda names: findnss(pwd.getpwnam, names)[2]
findnss_gid = lambda names: findnss(grp.getgrnam, names)[2]


def setup_add_ldif(ldb, ldif_path, subst_vars=None,controls=["relax:0"]):
    """Setup a ldb in the private dir.

    :param ldb: LDB file to import data into
    :param ldif_path: Path of the LDIF file to load
    :param subst_vars: Optional variables to subsitute in LDIF.
    :param nocontrols: Optional list of controls, can be None for no controls
    """
    assert isinstance(ldif_path, str)
    data = read_and_sub_file(ldif_path, subst_vars)
    ldb.add_ldif(data, controls)


def setup_modify_ldif(ldb, ldif_path, subst_vars=None,controls=["relax:0"]):
    """Modify a ldb in the private dir.

    :param ldb: LDB object.
    :param ldif_path: LDIF file path.
    :param subst_vars: Optional dictionary with substitution variables.
    """
    data = read_and_sub_file(ldif_path, subst_vars)
    ldb.modify_ldif(data, controls)


def setup_ldb(ldb, ldif_path, subst_vars):
    """Import a LDIF a file into a LDB handle, optionally substituting
    variables.

    :note: Either all LDIF data will be added or none (using transactions).

    :param ldb: LDB file to import into.
    :param ldif_path: Path to the LDIF file.
    :param subst_vars: Dictionary with substitution variables.
    """
    assert ldb is not None
    ldb.transaction_start()
    try:
        setup_add_ldif(ldb, ldif_path, subst_vars)
    except Exception:
        ldb.transaction_cancel()
        raise
    else:
        ldb.transaction_commit()


def provision_paths_from_lp(lp, dnsdomain):
    """Set the default paths for provisioning.

    :param lp: Loadparm context.
    :param dnsdomain: DNS Domain name
    """
    paths = ProvisionPaths()
    paths.private_dir = lp.get("private dir")

    # This is stored without path prefix for the "privateKeytab" attribute in
    # "secrets_dns.ldif".
    paths.dns_keytab = "dns.keytab"
    paths.keytab = "secrets.keytab"

    paths.shareconf = os.path.join(paths.private_dir, "share.ldb")
    paths.samdb = os.path.join(paths.private_dir, "sam.ldb")
    paths.idmapdb = os.path.join(paths.private_dir, "idmap.ldb")
    paths.secrets = os.path.join(paths.private_dir, "secrets.ldb")
    paths.privilege = os.path.join(paths.private_dir, "privilege.ldb")
    paths.dns = os.path.join(paths.private_dir, "dns", dnsdomain + ".zone")
    paths.dns_update_list = os.path.join(paths.private_dir, "dns_update_list")
    paths.spn_update_list = os.path.join(paths.private_dir, "spn_update_list")
    paths.namedconf = os.path.join(paths.private_dir, "named.conf")
    paths.namedconf_update = os.path.join(paths.private_dir, "named.conf.update")
    paths.namedtxt = os.path.join(paths.private_dir, "named.txt")
    paths.krb5conf = os.path.join(paths.private_dir, "krb5.conf")
    paths.winsdb = os.path.join(paths.private_dir, "wins.ldb")
    paths.s4_ldapi_path = os.path.join(paths.private_dir, "ldapi")
    paths.phpldapadminconfig = os.path.join(paths.private_dir,
                                            "phpldapadmin-config.php")
    paths.hklm = "hklm.ldb"
    paths.hkcr = "hkcr.ldb"
    paths.hkcu = "hkcu.ldb"
    paths.hku = "hku.ldb"
    paths.hkpd = "hkpd.ldb"
    paths.hkpt = "hkpt.ldb"
    paths.sysvol = lp.get("path", "sysvol")
    paths.netlogon = lp.get("path", "netlogon")
    paths.smbconf = lp.configfile
    return paths


def guess_names(lp=None, hostname=None, domain=None, dnsdomain=None,
                serverrole=None, rootdn=None, domaindn=None, configdn=None,
                schemadn=None, serverdn=None, sitename=None):
    """Guess configuration settings to use."""

    if hostname is None:
        hostname = socket.gethostname().split(".")[0]

    netbiosname = lp.get("netbios name")
    if netbiosname is None:
        netbiosname = hostname
        # remove forbidden chars
        newnbname = ""
        for x in netbiosname:
            if x.isalnum() or x in VALID_NETBIOS_CHARS:
                newnbname = "%s%c" % (newnbname, x)
        # force the length to be <16
        netbiosname = newnbname[0:15]
    assert netbiosname is not None
    netbiosname = netbiosname.upper()
    if not valid_netbios_name(netbiosname):
        raise InvalidNetbiosName(netbiosname)

    if dnsdomain is None:
        dnsdomain = lp.get("realm")
        if dnsdomain is None or dnsdomain == "":
            raise ProvisioningError("guess_names: 'realm' not specified in supplied %s!", lp.configfile)

    dnsdomain = dnsdomain.lower()

    if serverrole is None:
        serverrole = lp.get("server role")
        if serverrole is None:
            raise ProvisioningError("guess_names: 'server role' not specified in supplied %s!" % lp.configfile)

    serverrole = serverrole.lower()

    realm = dnsdomain.upper()

    if lp.get("realm") == "":
        raise ProvisioningError("guess_names: 'realm =' was not specified in supplied %s.  Please remove the smb.conf file and let provision generate it" % lp.configfile)

    if lp.get("realm").upper() != realm:
        raise ProvisioningError("guess_names: 'realm=%s' in %s must match chosen realm '%s'!  Please remove the smb.conf file and let provision generate it" % (lp.get("realm").upper(), realm, lp.configfile))

    if lp.get("server role").lower() != serverrole:
        raise ProvisioningError("guess_names: 'server role=%s' in %s must match chosen server role '%s'!  Please remove the smb.conf file and let provision generate it" % (lp.get("server role"), serverrole, lp.configfile))

    if serverrole == "domain controller":
        if domain is None:
            # This will, for better or worse, default to 'WORKGROUP'
            domain = lp.get("workgroup")
        domain = domain.upper()

        if lp.get("workgroup").upper() != domain:
            raise ProvisioningError("guess_names: Workgroup '%s' in smb.conf must match chosen domain '%s'!  Please remove the %s file and let provision generate it" % (lp.get("workgroup").upper(), domain, lp.configfile))

        if domaindn is None:
            domaindn = samba.dn_from_dns_name(dnsdomain)

        if domain == netbiosname:
            raise ProvisioningError("guess_names: Domain '%s' must not be equal to short host name '%s'!" % (domain, netbiosname))
    else:
        domain = netbiosname
        if domaindn is None:
            domaindn = "DC=" + netbiosname

    if not valid_netbios_name(domain):
        raise InvalidNetbiosName(domain)

    if hostname.upper() == realm:
        raise ProvisioningError("guess_names: Realm '%s' must not be equal to hostname '%s'!" % (realm, hostname))
    if netbiosname.upper() == realm:
        raise ProvisioningError("guess_names: Realm '%s' must not be equal to netbios hostname '%s'!" % (realm, netbiosname))
    if domain == realm:
        raise ProvisioningError("guess_names: Realm '%s' must not be equal to short domain name '%s'!" % (realm, domain))

    if rootdn is None:
       rootdn = domaindn

    if configdn is None:
        configdn = "CN=Configuration," + rootdn
    if schemadn is None:
        schemadn = "CN=Schema," + configdn

    if sitename is None:
        sitename=DEFAULTSITE

    names = ProvisionNames()
    names.rootdn = rootdn
    names.domaindn = domaindn
    names.configdn = configdn
    names.schemadn = schemadn
    names.ldapmanagerdn = "CN=Manager," + rootdn
    names.dnsdomain = dnsdomain
    names.domain = domain
    names.realm = realm
    names.netbiosname = netbiosname
    names.hostname = hostname
    names.sitename = sitename
    names.serverdn = "CN=%s,CN=Servers,CN=%s,CN=Sites,%s" % (
        netbiosname, sitename, configdn)

    return names


def make_smbconf(smbconf, hostname, domain, realm, serverrole,
                 targetdir, sid_generator="internal", eadb=False, lp=None):
    """Create a new smb.conf file based on a couple of basic settings.
    """
    assert smbconf is not None
    if hostname is None:
        hostname = socket.gethostname().split(".")[0]
        netbiosname = hostname.upper()
        # remove forbidden chars
        newnbname = ""
        for x in netbiosname:
            if x.isalnum() or x in VALID_NETBIOS_CHARS:
                newnbname = "%s%c" % (newnbname, x)
        #force the length to be <16
        netbiosname = newnbname[0:15]
    else:
        netbiosname = hostname.upper()

    if serverrole is None:
        serverrole = "standalone"

    assert serverrole in ("domain controller", "member server", "standalone")
    if serverrole == "domain controller":
        smbconfsuffix = "dc"
    elif serverrole == "member server":
        smbconfsuffix = "member"
    elif serverrole == "standalone":
        smbconfsuffix = "standalone"

    if sid_generator is None:
        sid_generator = "internal"

    assert domain is not None
    domain = domain.upper()

    assert realm is not None
    realm = realm.upper()

    if lp is None:
        lp = samba.param.LoadParm()
    #Load non-existant file
    if os.path.exists(smbconf):
        lp.load(smbconf)
    if eadb and not lp.get("posix:eadb"):
        if targetdir is not None:
            privdir = os.path.join(targetdir, "private")
        else:
            privdir = lp.get("private dir")
        lp.set("posix:eadb", os.path.abspath(os.path.join(privdir, "eadb.tdb")))

    if targetdir is not None:
        privatedir_line = "private dir = " + os.path.abspath(os.path.join(targetdir, "private"))
        lockdir_line = "lock dir = " + os.path.abspath(targetdir)
        statedir_line = "state directory = " + os.path.abspath(targetdir)
        cachedir_line = "cache directory = " + os.path.abspath(targetdir)

        lp.set("lock dir", os.path.abspath(targetdir))
        lp.set("state directory", os.path.abspath(targetdir))
        lp.set("cache directory", os.path.abspath(targetdir))
    else:
        privatedir_line = ""
        lockdir_line = ""
        statedir_line = ""
        cachedir_line = ""

    sysvol = os.path.join(lp.get("state directory"), "sysvol")
    netlogon = os.path.join(sysvol, realm.lower(), "scripts")

    setup_file(setup_path("provision.smb.conf.%s" % smbconfsuffix),
               smbconf, {
            "NETBIOS_NAME": netbiosname,
            "DOMAIN": domain,
            "REALM": realm,
            "SERVERROLE": serverrole,
            "NETLOGONPATH": netlogon,
            "SYSVOLPATH": sysvol,
            "PRIVATEDIR_LINE": privatedir_line,
            "LOCKDIR_LINE": lockdir_line,
            "STATEDIR_LINE": statedir_line,
            "CACHEDIR_LINE": cachedir_line
            })

    # reload the smb.conf
    lp.load(smbconf)

    # and dump it without any values that are the default
    # this ensures that any smb.conf parameters that were set
    # on the provision/join command line are set in the resulting smb.conf
    f = open(smbconf, mode='w')
    lp.dump(f, False)
    f.close()



def setup_name_mappings(idmap, sid, root_uid, nobody_uid,
                        users_gid, wheel_gid):
    """setup reasonable name mappings for sam names to unix names.

    :param samdb: SamDB object.
    :param idmap: IDmap db object.
    :param sid: The domain sid.
    :param domaindn: The domain DN.
    :param root_uid: uid of the UNIX root user.
    :param nobody_uid: uid of the UNIX nobody user.
    :param users_gid: gid of the UNIX users group.
    :param wheel_gid: gid of the UNIX wheel group.
    """
    idmap.setup_name_mapping("S-1-5-7", idmap.TYPE_UID, nobody_uid)
    idmap.setup_name_mapping("S-1-5-32-544", idmap.TYPE_GID, wheel_gid)

    idmap.setup_name_mapping(sid + "-500", idmap.TYPE_UID, root_uid)
    idmap.setup_name_mapping(sid + "-513", idmap.TYPE_GID, users_gid)


def setup_samdb_partitions(samdb_path, logger, lp, session_info,
                           provision_backend, names, schema, serverrole,
                           erase=False):
    """Setup the partitions for the SAM database.

    Alternatively, provision() may call this, and then populate the database.

    :note: This will wipe the Sam Database!

    :note: This function always removes the local SAM LDB file. The erase
        parameter controls whether to erase the existing data, which
        may not be stored locally but in LDAP.

    """
    assert session_info is not None

    # We use options=["modules:"] to stop the modules loading - we
    # just want to wipe and re-initialise the database, not start it up

    try:
        os.unlink(samdb_path)
    except OSError:
        pass

    samdb = Ldb(url=samdb_path, session_info=session_info,
                lp=lp, options=["modules:"])

    ldap_backend_line = "# No LDAP backend"
    if provision_backend.type is not "ldb":
        ldap_backend_line = "ldapBackend: %s" % provision_backend.ldap_uri

    samdb.transaction_start()
    try:
        logger.info("Setting up sam.ldb partitions and settings")
        setup_add_ldif(samdb, setup_path("provision_partitions.ldif"), {
                "LDAP_BACKEND_LINE": ldap_backend_line
        })


        setup_add_ldif(samdb, setup_path("provision_init.ldif"), {
                "BACKEND_TYPE": provision_backend.type,
                "SERVER_ROLE": serverrole
                })

        logger.info("Setting up sam.ldb rootDSE")
        setup_samdb_rootdse(samdb, names)
    except Exception:
        samdb.transaction_cancel()
        raise
    else:
        samdb.transaction_commit()


def secretsdb_self_join(secretsdb, domain,
                        netbiosname, machinepass, domainsid=None,
                        realm=None, dnsdomain=None,
                        keytab_path=None,
                        key_version_number=1,
                        secure_channel_type=SEC_CHAN_WKSTA):
    """Add domain join-specific bits to a secrets database.

    :param secretsdb: Ldb Handle to the secrets database
    :param machinepass: Machine password
    """
    attrs = ["whenChanged",
           "secret",
           "priorSecret",
           "priorChanged",
           "krb5Keytab",
           "privateKeytab"]

    if realm is not None:
        if dnsdomain is None:
            dnsdomain = realm.lower()
        dnsname = '%s.%s' % (netbiosname.lower(), dnsdomain.lower())
    else:
        dnsname = None
    shortname = netbiosname.lower()

    # We don't need to set msg["flatname"] here, because rdn_name will handle
    # it, and it causes problems for modifies anyway
    msg = ldb.Message(ldb.Dn(secretsdb, "flatname=%s,cn=Primary Domains" % domain))
    msg["secureChannelType"] = [str(secure_channel_type)]
    msg["objectClass"] = ["top", "primaryDomain"]
    if dnsname is not None:
        msg["objectClass"] = ["top", "primaryDomain", "kerberosSecret"]
        msg["realm"] = [realm]
        msg["saltPrincipal"] = ["host/%s@%s" % (dnsname, realm.upper())]
        msg["msDS-KeyVersionNumber"] = [str(key_version_number)]
        msg["privateKeytab"] = ["secrets.keytab"]

    msg["secret"] = [machinepass]
    msg["samAccountName"] = ["%s$" % netbiosname]
    msg["secureChannelType"] = [str(secure_channel_type)]
    if domainsid is not None:
        msg["objectSid"] = [ndr_pack(domainsid)]

    # This complex expression tries to ensure that we don't have more
    # than one record for this SID, realm or netbios domain at a time,
    # but we don't delete the old record that we are about to modify,
    # because that would delete the keytab and previous password.
    res = secretsdb.search(base="cn=Primary Domains", attrs=attrs,
        expression=("(&(|(flatname=%s)(realm=%s)(objectSid=%s))(objectclass=primaryDomain)(!(dn=%s)))" % (domain, realm, str(domainsid), str(msg.dn))),
        scope=ldb.SCOPE_ONELEVEL)

    for del_msg in res:
        secretsdb.delete(del_msg.dn)

    res = secretsdb.search(base=msg.dn, attrs=attrs, scope=ldb.SCOPE_BASE)

    if len(res) == 1:
        msg["priorSecret"] = [res[0]["secret"][0]]
        msg["priorWhenChanged"] = [res[0]["whenChanged"][0]]

        try:
            msg["privateKeytab"] = [res[0]["privateKeytab"][0]]
        except KeyError:
            pass

        try:
            msg["krb5Keytab"] = [res[0]["krb5Keytab"][0]]
        except KeyError:
            pass

        for el in msg:
            if el != 'dn':
                msg[el].set_flags(ldb.FLAG_MOD_REPLACE)
        secretsdb.modify(msg)
        secretsdb.rename(res[0].dn, msg.dn)
    else:
        spn = [ 'HOST/%s' % shortname ]
        if secure_channel_type == SEC_CHAN_BDC and dnsname is not None:
            # we are a domain controller then we add servicePrincipalName
            # entries for the keytab code to update.
            spn.extend([ 'HOST/%s' % dnsname ])
        msg["servicePrincipalName"] = spn

        secretsdb.add(msg)


def setup_secretsdb(paths, session_info, backend_credentials, lp):
    """Setup the secrets database.

   :note: This function does not handle exceptions and transaction on purpose,
       it's up to the caller to do this job.

    :param path: Path to the secrets database.
    :param session_info: Session info.
    :param credentials: Credentials
    :param lp: Loadparm context
    :return: LDB handle for the created secrets database
    """
    if os.path.exists(paths.secrets):
        os.unlink(paths.secrets)

    keytab_path = os.path.join(paths.private_dir, paths.keytab)
    if os.path.exists(keytab_path):
        os.unlink(keytab_path)

    dns_keytab_path = os.path.join(paths.private_dir, paths.dns_keytab)
    if os.path.exists(dns_keytab_path):
        os.unlink(dns_keytab_path)

    path = paths.secrets

    secrets_ldb = Ldb(path, session_info=session_info,
                      lp=lp)
    secrets_ldb.erase()
    secrets_ldb.load_ldif_file_add(setup_path("secrets_init.ldif"))
    secrets_ldb = Ldb(path, session_info=session_info,
                      lp=lp)
    secrets_ldb.transaction_start()
    try:
        secrets_ldb.load_ldif_file_add(setup_path("secrets.ldif"))

        if (backend_credentials is not None and
            backend_credentials.authentication_requested()):
            if backend_credentials.get_bind_dn() is not None:
                setup_add_ldif(secrets_ldb,
                    setup_path("secrets_simple_ldap.ldif"), {
                        "LDAPMANAGERDN": backend_credentials.get_bind_dn(),
                        "LDAPMANAGERPASS_B64": b64encode(backend_credentials.get_password())
                        })
            else:
                setup_add_ldif(secrets_ldb,
                    setup_path("secrets_sasl_ldap.ldif"), {
                        "LDAPADMINUSER": backend_credentials.get_username(),
                        "LDAPADMINREALM": backend_credentials.get_realm(),
                        "LDAPADMINPASS_B64": b64encode(backend_credentials.get_password())
                        })
    except Exception:
        secrets_ldb.transaction_cancel()
        raise
    return secrets_ldb



def setup_privileges(path, session_info, lp):
    """Setup the privileges database.

    :param path: Path to the privileges database.
    :param session_info: Session info.
    :param credentials: Credentials
    :param lp: Loadparm context
    :return: LDB handle for the created secrets database
    """
    if os.path.exists(path):
        os.unlink(path)
    privilege_ldb = Ldb(path, session_info=session_info, lp=lp)
    privilege_ldb.erase()
    privilege_ldb.load_ldif_file_add(setup_path("provision_privilege.ldif"))


def setup_registry(path, session_info, lp):
    """Setup the registry.

    :param path: Path to the registry database
    :param session_info: Session information
    :param credentials: Credentials
    :param lp: Loadparm context
    """
    reg = samba.registry.Registry()
    hive = samba.registry.open_ldb(path, session_info=session_info, lp_ctx=lp)
    reg.mount_hive(hive, samba.registry.HKEY_LOCAL_MACHINE)
    provision_reg = setup_path("provision.reg")
    assert os.path.exists(provision_reg)
    reg.diff_apply(provision_reg)


def setup_idmapdb(path, session_info, lp):
    """Setup the idmap database.

    :param path: path to the idmap database
    :param session_info: Session information
    :param credentials: Credentials
    :param lp: Loadparm context
    """
    if os.path.exists(path):
        os.unlink(path)

    idmap_ldb = IDmapDB(path, session_info=session_info, lp=lp)
    idmap_ldb.erase()
    idmap_ldb.load_ldif_file_add(setup_path("idmap_init.ldif"))
    return idmap_ldb


def setup_samdb_rootdse(samdb, names):
    """Setup the SamDB rootdse.

    :param samdb: Sam Database handle
    """
    setup_add_ldif(samdb, setup_path("provision_rootdse_add.ldif"), {
        "SCHEMADN": names.schemadn,
        "DOMAINDN": names.domaindn,
        "ROOTDN"  : names.rootdn,
        "CONFIGDN": names.configdn,
        "SERVERDN": names.serverdn,
        })


def setup_self_join(samdb, admin_session_info, names, fill, machinepass, dnspass,
                    domainsid, next_rid, invocationid,
                    policyguid, policyguid_dc, domainControllerFunctionality,
                    ntdsguid, dc_rid=None):
    """Join a host to its own domain."""
    assert isinstance(invocationid, str)
    if ntdsguid is not None:
        ntdsguid_line = "objectGUID: %s\n"%ntdsguid
    else:
        ntdsguid_line = ""

    if dc_rid is None:
        dc_rid = next_rid

    setup_add_ldif(samdb, setup_path("provision_self_join.ldif"), {
              "CONFIGDN": names.configdn,
              "SCHEMADN": names.schemadn,
              "DOMAINDN": names.domaindn,
              "SERVERDN": names.serverdn,
              "INVOCATIONID": invocationid,
              "NETBIOSNAME": names.netbiosname,
              "DNSNAME": "%s.%s" % (names.hostname, names.dnsdomain),
              "MACHINEPASS_B64": b64encode(machinepass.encode('utf-16-le')),
              "DOMAINSID": str(domainsid),
              "DCRID": str(dc_rid),
              "SAMBA_VERSION_STRING": version,
              "NTDSGUID": ntdsguid_line,
              "DOMAIN_CONTROLLER_FUNCTIONALITY": str(
                  domainControllerFunctionality),
              "RIDALLOCATIONSTART": str(next_rid + 100),
              "RIDALLOCATIONEND": str(next_rid + 100 + 499)})

    setup_add_ldif(samdb, setup_path("provision_group_policy.ldif"), {
              "POLICYGUID": policyguid,
              "POLICYGUID_DC": policyguid_dc,
              "DNSDOMAIN": names.dnsdomain,
              "DOMAINDN": names.domaindn})

    # If we are setting up a subdomain, then this has been replicated in, so we don't need to add it
    if fill == FILL_FULL:
        setup_add_ldif(samdb, setup_path("provision_self_join_config.ldif"), {
                "CONFIGDN": names.configdn,
                "SCHEMADN": names.schemadn,
                "DOMAINDN": names.domaindn,
                "SERVERDN": names.serverdn,
                "INVOCATIONID": invocationid,
                "NETBIOSNAME": names.netbiosname,
                "DNSNAME": "%s.%s" % (names.hostname, names.dnsdomain),
                "MACHINEPASS_B64": b64encode(machinepass.encode('utf-16-le')),
                "DOMAINSID": str(domainsid),
                "DCRID": str(dc_rid),
                "SAMBA_VERSION_STRING": version,
                "NTDSGUID": ntdsguid_line,
                "DOMAIN_CONTROLLER_FUNCTIONALITY": str(
                    domainControllerFunctionality)})

    # Setup fSMORoleOwner entries to point at the newly created DC entry
        setup_modify_ldif(samdb, setup_path("provision_self_join_modify_config.ldif"), {
                "CONFIGDN": names.configdn,
                "SCHEMADN": names.schemadn,
                "DEFAULTSITE": names.sitename,
                "NETBIOSNAME": names.netbiosname,
                "SERVERDN": names.serverdn,
                })

    system_session_info = system_session()
    samdb.set_session_info(system_session_info)
    # Setup fSMORoleOwner entries to point at the newly created DC entry

    # to modify a serverReference under cn=config when we are a subdomain, we must
    # be system due to ACLs
    setup_modify_ldif(samdb, setup_path("provision_self_join_modify.ldif"), {
              "DOMAINDN": names.domaindn,
              "SERVERDN": names.serverdn,
              "NETBIOSNAME": names.netbiosname,
              })

    samdb.set_session_info(admin_session_info)

    # This is Samba4 specific and should be replaced by the correct
    # DNS AD-style setup
    setup_add_ldif(samdb, setup_path("provision_dns_add_samba.ldif"), {
              "DNSDOMAIN": names.dnsdomain,
              "DOMAINDN": names.domaindn,
              "DNSPASS_B64": b64encode(dnspass.encode('utf-16-le')),
              "HOSTNAME" : names.hostname,
              "DNSNAME" : '%s.%s' % (
                  names.netbiosname.lower(), names.dnsdomain.lower())
              })


def getpolicypath(sysvolpath, dnsdomain, guid):
    """Return the physical path of policy given its guid.

    :param sysvolpath: Path to the sysvol folder
    :param dnsdomain: DNS name of the AD domain
    :param guid: The GUID of the policy
    :return: A string with the complete path to the policy folder
    """

    if guid[0] != "{":
        guid = "{%s}" % guid
    policy_path = os.path.join(sysvolpath, dnsdomain, "Policies", guid)
    return policy_path


def create_gpo_struct(policy_path):
    if not os.path.exists(policy_path):
        os.makedirs(policy_path, 0775)
    open(os.path.join(policy_path, "GPT.INI"), 'w').write(
                      "[General]\r\nVersion=0")
    p = os.path.join(policy_path, "MACHINE")
    if not os.path.exists(p):
        os.makedirs(p, 0775)
    p = os.path.join(policy_path, "USER")
    if not os.path.exists(p):
        os.makedirs(p, 0775)


def create_default_gpo(sysvolpath, dnsdomain, policyguid, policyguid_dc):
    """Create the default GPO for a domain

    :param sysvolpath: Physical path for the sysvol folder
    :param dnsdomain: DNS domain name of the AD domain
    :param policyguid: GUID of the default domain policy
    :param policyguid_dc: GUID of the default domain controler policy
    """
    policy_path = getpolicypath(sysvolpath,dnsdomain,policyguid)
    create_gpo_struct(policy_path)

    policy_path = getpolicypath(sysvolpath,dnsdomain,policyguid_dc)
    create_gpo_struct(policy_path)


def setup_samdb(path, session_info, provision_backend, lp, names,
        logger, fill, serverrole, schema, am_rodc=False):
    """Setup a complete SAM Database.

    :note: This will wipe the main SAM database file!
    """

    # Also wipes the database
    setup_samdb_partitions(path, logger=logger, lp=lp,
        provision_backend=provision_backend, session_info=session_info,
        names=names, serverrole=serverrole, schema=schema)

    # Load the database, but don's load the global schema and don't connect
    # quite yet
    samdb = SamDB(session_info=session_info, url=None, auto_connect=False,
                  credentials=provision_backend.credentials, lp=lp,
                  global_schema=False, am_rodc=am_rodc)

    logger.info("Pre-loading the Samba 4 and AD schema")

    # Load the schema from the one we computed earlier
    samdb.set_schema(schema)

    # Set the NTDS settings DN manually - in order to have it already around
    # before the provisioned tree exists and we connect
    samdb.set_ntds_settings_dn("CN=NTDS Settings,%s" % names.serverdn)

    # And now we can connect to the DB - the schema won't be loaded from the
    # DB
    samdb.connect(path)

    return samdb

def fill_samdb(samdb, lp, names,
        logger, domainsid, domainguid, policyguid, policyguid_dc, fill,
        adminpass, krbtgtpass, machinepass, invocationid, dnspass, ntdsguid,
        serverrole, am_rodc=False, dom_for_fun_level=None, schema=None,
        next_rid=None, dc_rid=None):

    if next_rid is None:
        next_rid = 1000

    # Provision does not make much sense values larger than 1000000000
    # as the upper range of the rIDAvailablePool is 1073741823 and
    # we don't want to create a domain that cannot allocate rids.
    if next_rid < 1000 or next_rid > 1000000000:
        error = "You want to run SAMBA 4 with a next_rid of %u, " % (next_rid)
        error += "the valid range is %u-%u. The default is %u." % (
            1000, 1000000000, 1000)
        raise ProvisioningError(error)

    # ATTENTION: Do NOT change these default values without discussion with the
    # team and/or release manager. They have a big impact on the whole program!
    domainControllerFunctionality = DS_DOMAIN_FUNCTION_2008_R2

    if dom_for_fun_level is None:
        dom_for_fun_level = DS_DOMAIN_FUNCTION_2003

    if dom_for_fun_level > domainControllerFunctionality:
        raise ProvisioningError("You want to run SAMBA 4 on a domain and forest function level which itself is higher than its actual DC function level (2008_R2). This won't work!")

    domainFunctionality = dom_for_fun_level
    forestFunctionality = dom_for_fun_level

    # Set the NTDS settings DN manually - in order to have it already around
    # before the provisioned tree exists and we connect
    samdb.set_ntds_settings_dn("CN=NTDS Settings,%s" % names.serverdn)

    samdb.transaction_start()
    try:
        # Set the domain functionality levels onto the database.
        # Various module (the password_hash module in particular) need
        # to know what level of AD we are emulating.

        # These will be fixed into the database via the database
        # modifictions below, but we need them set from the start.
        samdb.set_opaque_integer("domainFunctionality", domainFunctionality)
        samdb.set_opaque_integer("forestFunctionality", forestFunctionality)
        samdb.set_opaque_integer("domainControllerFunctionality",
            domainControllerFunctionality)

        samdb.set_domain_sid(str(domainsid))
        samdb.set_invocation_id(invocationid)

        logger.info("Adding DomainDN: %s" % names.domaindn)

        # impersonate domain admin
        admin_session_info = admin_session(lp, str(domainsid))
        samdb.set_session_info(admin_session_info)
        if domainguid is not None:
            domainguid_line = "objectGUID: %s\n-" % domainguid
        else:
            domainguid_line = ""

        descr = b64encode(get_domain_descriptor(domainsid))
        setup_add_ldif(samdb, setup_path("provision_basedn.ldif"), {
                "DOMAINDN": names.domaindn,
                "DOMAINSID": str(domainsid),
                "DESCRIPTOR": descr,
                "DOMAINGUID": domainguid_line
                })

        setup_modify_ldif(samdb, setup_path("provision_basedn_modify.ldif"), {
            "DOMAINDN": names.domaindn,
            "CREATTIME": str(samba.unix2nttime(int(time.time()))),
            "NEXTRID": str(next_rid),
            "DEFAULTSITE": names.sitename,
            "CONFIGDN": names.configdn,
            "POLICYGUID": policyguid,
            "DOMAIN_FUNCTIONALITY": str(domainFunctionality),
            "SAMBA_VERSION_STRING": version
            })

        # If we are setting up a subdomain, then this has been replicated in, so we don't need to add it
        if fill == FILL_FULL:
            logger.info("Adding configuration container")
            descr = b64encode(get_config_descriptor(domainsid))
            setup_add_ldif(samdb, setup_path("provision_configuration_basedn.ldif"), {
                    "CONFIGDN": names.configdn,
                    "DESCRIPTOR": descr,
                    })

            # The LDIF here was created when the Schema object was constructed
            logger.info("Setting up sam.ldb schema")
            samdb.add_ldif(schema.schema_dn_add, controls=["relax:0"])
            samdb.modify_ldif(schema.schema_dn_modify)
            samdb.write_prefixes_from_schema()
            samdb.add_ldif(schema.schema_data, controls=["relax:0"])
            setup_add_ldif(samdb, setup_path("aggregate_schema.ldif"),
                           {"SCHEMADN": names.schemadn})

        # Now register this container in the root of the forest
        msg = ldb.Message(ldb.Dn(samdb, names.domaindn))
        msg["subRefs"] = ldb.MessageElement(names.configdn , ldb.FLAG_MOD_ADD,
                    "subRefs")

    except Exception:
        samdb.transaction_cancel()
        raise
    else:
        samdb.transaction_commit()

    samdb.transaction_start()
    try:
        samdb.invocation_id = invocationid

        # If we are setting up a subdomain, then this has been replicated in, so we don't need to add it
        if fill == FILL_FULL:
            logger.info("Setting up sam.ldb configuration data")
            setup_add_ldif(samdb, setup_path("provision_configuration.ldif"), {
                    "CONFIGDN": names.configdn,
                    "NETBIOSNAME": names.netbiosname,
                    "DEFAULTSITE": names.sitename,
                    "DNSDOMAIN": names.dnsdomain,
                    "DOMAIN": names.domain,
                    "SCHEMADN": names.schemadn,
                    "DOMAINDN": names.domaindn,
                    "SERVERDN": names.serverdn,
                    "FOREST_FUNCTIONALITY": str(forestFunctionality),
                    "DOMAIN_FUNCTIONALITY": str(domainFunctionality),
                    })

            logger.info("Setting up display specifiers")
            display_specifiers_ldif = read_ms_ldif(
                setup_path('display-specifiers/DisplaySpecifiers-Win2k8R2.txt'))
            display_specifiers_ldif = substitute_var(display_specifiers_ldif,
                                                     {"CONFIGDN": names.configdn})
            check_all_substituted(display_specifiers_ldif)
            samdb.add_ldif(display_specifiers_ldif)

        logger.info("Adding users container")
        setup_add_ldif(samdb, setup_path("provision_users_add.ldif"), {
                "DOMAINDN": names.domaindn})
        logger.info("Modifying users container")
        setup_modify_ldif(samdb, setup_path("provision_users_modify.ldif"), {
                "DOMAINDN": names.domaindn})
        logger.info("Adding computers container")
        setup_add_ldif(samdb, setup_path("provision_computers_add.ldif"), {
                "DOMAINDN": names.domaindn})
        logger.info("Modifying computers container")
        setup_modify_ldif(samdb,
            setup_path("provision_computers_modify.ldif"), {
                "DOMAINDN": names.domaindn})
        logger.info("Setting up sam.ldb data")
        setup_add_ldif(samdb, setup_path("provision.ldif"), {
            "CREATTIME": str(samba.unix2nttime(int(time.time()))),
            "DOMAINDN": names.domaindn,
            "NETBIOSNAME": names.netbiosname,
            "DEFAULTSITE": names.sitename,
            "CONFIGDN": names.configdn,
            "SERVERDN": names.serverdn,
            "RIDAVAILABLESTART": str(next_rid + 600),
            "POLICYGUID_DC": policyguid_dc
            })

        # If we are setting up a subdomain, then this has been replicated in, so we don't need to add it
        if fill == FILL_FULL:
            setup_modify_ldif(samdb,
                              setup_path("provision_configuration_references.ldif"), {
                    "CONFIGDN": names.configdn,
                    "SCHEMADN": names.schemadn})

            logger.info("Setting up well known security principals")
            setup_add_ldif(samdb, setup_path("provision_well_known_sec_princ.ldif"), {
                "CONFIGDN": names.configdn,
                })

        if fill == FILL_FULL or fill == FILL_SUBDOMAIN:
            setup_modify_ldif(samdb,
                              setup_path("provision_basedn_references.ldif"),
                              {"DOMAINDN": names.domaindn})

            logger.info("Setting up sam.ldb users and groups")
            setup_add_ldif(samdb, setup_path("provision_users.ldif"), {
                "DOMAINDN": names.domaindn,
                "DOMAINSID": str(domainsid),
                "ADMINPASS_B64": b64encode(adminpass.encode('utf-16-le')),
                "KRBTGTPASS_B64": b64encode(krbtgtpass.encode('utf-16-le'))
                })

            logger.info("Setting up self join")
            setup_self_join(samdb, admin_session_info, names=names, fill=fill, invocationid=invocationid,
                            dnspass=dnspass,
                            machinepass=machinepass,
                            domainsid=domainsid,
                            next_rid=next_rid,
                            dc_rid=dc_rid,
                            policyguid=policyguid,
                            policyguid_dc=policyguid_dc,
                            domainControllerFunctionality=domainControllerFunctionality,
                            ntdsguid=ntdsguid)

            ntds_dn = "CN=NTDS Settings,%s" % names.serverdn
            names.ntdsguid = samdb.searchone(basedn=ntds_dn,
                attribute="objectGUID", expression="", scope=ldb.SCOPE_BASE)
            assert isinstance(names.ntdsguid, str)
    except Exception:
        samdb.transaction_cancel()
        raise
    else:
        samdb.transaction_commit()
        return samdb


FILL_FULL = "FULL"
FILL_SUBDOMAIN = "SUBDOMAIN"
FILL_NT4SYNC = "NT4SYNC"
FILL_DRS = "DRS"
SYSVOL_ACL = "O:LAG:BAD:P(A;OICI;0x001f01ff;;;BA)(A;OICI;0x001200a9;;;SO)(A;OICI;0x001f01ff;;;SY)(A;OICI;0x001200a9;;;AU)"
POLICIES_ACL = "O:LAG:BAD:P(A;OICI;0x001f01ff;;;BA)(A;OICI;0x001200a9;;;SO)(A;OICI;0x001f01ff;;;SY)(A;OICI;0x001200a9;;;AU)(A;OICI;0x001301bf;;;PA)"


def set_dir_acl(path, acl, lp, domsid):
    setntacl(lp, path, acl, domsid)
    for root, dirs, files in os.walk(path, topdown=False):
        for name in files:
            setntacl(lp, os.path.join(root, name), acl, domsid)
        for name in dirs:
            setntacl(lp, os.path.join(root, name), acl, domsid)


def set_gpos_acl(sysvol, dnsdomain, domainsid, domaindn, samdb, lp):
    """Set ACL on the sysvol/<dnsname>/Policies folder and the policy
    folders beneath.

    :param sysvol: Physical path for the sysvol folder
    :param dnsdomain: The DNS name of the domain
    :param domainsid: The SID of the domain
    :param domaindn: The DN of the domain (ie. DC=...)
    :param samdb: An LDB object on the SAM db
    :param lp: an LP object
    """

    # Set ACL for GPO root folder
    root_policy_path = os.path.join(sysvol, dnsdomain, "Policies")
    setntacl(lp, root_policy_path, POLICIES_ACL, str(domainsid))

    res = samdb.search(base="CN=Policies,CN=System,%s"%(domaindn),
                        attrs=["cn", "nTSecurityDescriptor"],
                        expression="", scope=ldb.SCOPE_ONELEVEL)

    for policy in res:
        acl = ndr_unpack(security.descriptor,
                         str(policy["nTSecurityDescriptor"])).as_sddl()
        policy_path = getpolicypath(sysvol, dnsdomain, str(policy["cn"]))
        set_dir_acl(policy_path, dsacl2fsacl(acl, str(domainsid)), lp,
                    str(domainsid))


def setsysvolacl(samdb, netlogon, sysvol, gid, domainsid, dnsdomain, domaindn,
    lp):
    """Set the ACL for the sysvol share and the subfolders

    :param samdb: An LDB object on the SAM db
    :param netlogon: Physical path for the netlogon folder
    :param sysvol: Physical path for the sysvol folder
    :param gid: The GID of the "Domain adminstrators" group
    :param domainsid: The SID of the domain
    :param dnsdomain: The DNS name of the domain
    :param domaindn: The DN of the domain (ie. DC=...)
    """

    try:
        os.chown(sysvol, -1, gid)
    except OSError:
        canchown = False
    else:
        canchown = True

    # Set the SYSVOL_ACL on the sysvol folder and subfolder (first level)
    setntacl(lp,sysvol, SYSVOL_ACL, str(domainsid))
    for root, dirs, files in os.walk(sysvol, topdown=False):
        for name in files:
            if canchown:
                os.chown(os.path.join(root, name), -1, gid)
            setntacl(lp, os.path.join(root, name), SYSVOL_ACL, str(domainsid))
        for name in dirs:
            if canchown:
                os.chown(os.path.join(root, name), -1, gid)
            setntacl(lp, os.path.join(root, name), SYSVOL_ACL, str(domainsid))

    # Set acls on Policy folder and policies folders
    set_gpos_acl(sysvol, dnsdomain, domainsid, domaindn, samdb, lp)


def interface_ips_v4(lp):
    '''return only IPv4 IPs'''
    ips = samba.interface_ips(lp, False)
    ret = []
    for i in ips:
        if i.find(':') == -1:
            ret.append(i)
    return ret

def interface_ips_v6(lp, linklocal=False):
    '''return only IPv6 IPs'''
    ips = samba.interface_ips(lp, False)
    ret = []
    for i in ips:
        if i.find(':') != -1 and (linklocal or i.find('%') == -1):
            ret.append(i)
    return ret


def provision_fill(samdb, secrets_ldb, logger, names, paths,
                   domainsid, schema=None,
                   targetdir=None, samdb_fill=FILL_FULL,
                   hostip=None, hostip6=None,
                   next_rid=1000, dc_rid=None, adminpass=None, krbtgtpass=None,
                   domainguid=None, policyguid=None, policyguid_dc=None,
                   invocationid=None, machinepass=None, ntdsguid=None,
                   dns_backend=None, dnspass=None,
                   serverrole=None, dom_for_fun_level=None,
                   am_rodc=False, lp=None):
    # create/adapt the group policy GUIDs
    # Default GUID for default policy are described at
    # "How Core Group Policy Works"
    # http://technet.microsoft.com/en-us/library/cc784268%28WS.10%29.aspx
    if policyguid is None:
        policyguid = DEFAULT_POLICY_GUID
    policyguid = policyguid.upper()
    if policyguid_dc is None:
        policyguid_dc = DEFAULT_DC_POLICY_GUID
    policyguid_dc = policyguid_dc.upper()

    if invocationid is None:
        invocationid = str(uuid.uuid4())

    if adminpass is None:
        adminpass = samba.generate_random_password(12, 32)
    if krbtgtpass is None:
        krbtgtpass = samba.generate_random_password(128, 255)
    if machinepass is None:
        machinepass  = samba.generate_random_password(128, 255)
    if dnspass is None:
        dnspass = samba.generate_random_password(128, 255)

    samdb = fill_samdb(samdb, lp, names, logger=logger,
                       domainsid=domainsid, schema=schema, domainguid=domainguid,
                       policyguid=policyguid, policyguid_dc=policyguid_dc,
                       fill=samdb_fill, adminpass=adminpass, krbtgtpass=krbtgtpass,
                       invocationid=invocationid, machinepass=machinepass,
                       dnspass=dnspass, ntdsguid=ntdsguid, serverrole=serverrole,
                       dom_for_fun_level=dom_for_fun_level, am_rodc=am_rodc,
                       next_rid=next_rid, dc_rid=dc_rid)

    if serverrole == "domain controller":
        # Set up group policies (domain policy and domain controller
        # policy)
        create_default_gpo(paths.sysvol, names.dnsdomain, policyguid,
                           policyguid_dc)
        setsysvolacl(samdb, paths.netlogon, paths.sysvol, paths.wheel_gid,
                     domainsid, names.dnsdomain, names.domaindn, lp)

        secretsdb_self_join(secrets_ldb, domain=names.domain,
                            realm=names.realm, dnsdomain=names.dnsdomain,
                            netbiosname=names.netbiosname, domainsid=domainsid,
                            machinepass=machinepass, secure_channel_type=SEC_CHAN_BDC)

        # Now set up the right msDS-SupportedEncryptionTypes into the DB
        # In future, this might be determined from some configuration
        kerberos_enctypes = str(ENC_ALL_TYPES)

        try:
            msg = ldb.Message(ldb.Dn(samdb,
                                     samdb.searchone("distinguishedName",
                                                     expression="samAccountName=%s$" % names.netbiosname,
                                                     scope=ldb.SCOPE_SUBTREE)))
            msg["msDS-SupportedEncryptionTypes"] = ldb.MessageElement(
                elements=kerberos_enctypes, flags=ldb.FLAG_MOD_REPLACE,
                name="msDS-SupportedEncryptionTypes")
            samdb.modify(msg)
        except ldb.LdbError, (enum, estr):
            if enum != ldb.ERR_NO_SUCH_ATTRIBUTE:
                # It might be that this attribute does not exist in this schema
                raise

        setup_ad_dns(samdb, secrets_ldb, domainsid, names, paths, lp, logger,
                     hostip=hostip, hostip6=hostip6, dns_backend=dns_backend,
                     dnspass=dnspass, os_level=dom_for_fun_level,
                     targetdir=targetdir, site=DEFAULTSITE)

        domainguid = samdb.searchone(basedn=samdb.get_default_basedn(),
                                     attribute="objectGUID")
        assert isinstance(domainguid, str)

    lastProvisionUSNs = get_last_provision_usn(samdb)
    maxUSN = get_max_usn(samdb, str(names.rootdn))
    if lastProvisionUSNs is not None:
        update_provision_usn(samdb, 0, maxUSN, invocationid, 1)
    else:
        set_provision_usn(samdb, 0, maxUSN, invocationid)

    logger.info("Setting up sam.ldb rootDSE marking as synchronized")
    setup_modify_ldif(samdb, setup_path("provision_rootdse_modify.ldif"),
                      { 'NTDSGUID' : names.ntdsguid })

    # fix any dangling GUIDs from the provision
    logger.info("Fixing provision GUIDs")
    chk = dbcheck(samdb, samdb_schema=samdb,  verbose=False, fix=True, yes=True, quiet=True)
    samdb.transaction_start()
    # a small number of GUIDs are missing because of ordering issues in the
    # provision code
    for schema_obj in ['CN=Domain', 'CN=Organizational-Person', 'CN=Contact', 'CN=inetOrgPerson']:
        chk.check_database(DN="%s,%s" % (schema_obj, names.schemadn),
                           scope=ldb.SCOPE_BASE, attrs=['defaultObjectCategory'])
    chk.check_database(DN="CN=IP Security,CN=System,%s" % names.domaindn,
                       scope=ldb.SCOPE_ONELEVEL,
                       attrs=['ipsecOwnersReference',
                              'ipsecFilterReference',
                              'ipsecISAKMPReference',
                              'ipsecNegotiationPolicyReference',
                              'ipsecNFAReference'])
    samdb.transaction_commit()


def provision(logger, session_info, credentials, smbconf=None,
        targetdir=None, samdb_fill=FILL_FULL, realm=None, rootdn=None,
        domaindn=None, schemadn=None, configdn=None, serverdn=None,
        domain=None, hostname=None, hostip=None, hostip6=None, domainsid=None,
        next_rid=1000, dc_rid=None, adminpass=None, ldapadminpass=None, krbtgtpass=None,
        domainguid=None, policyguid=None, policyguid_dc=None,
        dns_backend=None, dnspass=None,
        invocationid=None, machinepass=None, ntdsguid=None,
        root=None, nobody=None, users=None, wheel=None, backup=None, aci=None,
        serverrole=None, dom_for_fun_level=None, 
        backend_type=None, sitename=None,
        ol_mmr_urls=None, ol_olc=None, slapd_path=None,
        useeadb=False, am_rodc=False,
        lp=None):
    """Provision samba4

    :note: caution, this wipes all existing data!
    """

    roles = {}
    roles["ROLE_STANDALONE"] = "standalone"
    roles["ROLE_DOMAIN_MEMBER"] = "member server"
    roles["ROLE_DOMAIN_BDC"] = "domain controller"
    roles["ROLE_DOMAIN_PDC"] = "domain controller"
    roles["dc"] = "domain controller"
    roles["member"] = "member server"
    roles["domain controller"] = "domain controller"
    roles["member server"] = "member server"
    roles["standalone"] = "standalone"

    try:
        serverrole = roles[serverrole]
    except KeyError:
        raise ProvisioningError('server role (%s) should be one of "domain controller", "member server", "standalone"' % serverrole)

    if ldapadminpass is None:
        # Make a new, random password between Samba and it's LDAP server
        ldapadminpass=samba.generate_random_password(128, 255)

    if backend_type is None:
        backend_type = "ldb"

    if domainsid is None:
        domainsid = security.random_sid()
    else:
        domainsid = security.dom_sid(domainsid)

    sid_generator = "internal"
    if backend_type == "fedora-ds":
        sid_generator = "backend"

    root_uid = findnss_uid([root or "root"])
    nobody_uid = findnss_uid([nobody or "nobody"])
    users_gid = findnss_gid([users or "users", 'users', 'other', 'staff'])
    if wheel is None:
        wheel_gid = findnss_gid(["wheel", "adm"])
    else:
        wheel_gid = findnss_gid([wheel])
    try:
        bind_gid = findnss_gid(["bind", "named"])
    except KeyError:
        bind_gid = None

    if targetdir is not None:
        smbconf = os.path.join(targetdir, "etc", "smb.conf")
    elif smbconf is None:
        smbconf = samba.param.default_path()
    if not os.path.exists(os.path.dirname(smbconf)):
        os.makedirs(os.path.dirname(smbconf))

    # only install a new smb.conf if there isn't one there already
    if os.path.exists(smbconf):
        # if Samba Team members can't figure out the weird errors
        # loading an empty smb.conf gives, then we need to be smarter.
        # Pretend it just didn't exist --abartlet
        data = open(smbconf, 'r').read()
        data = data.lstrip()
        if data is None or data == "":
            make_smbconf(smbconf, hostname, domain, realm,
                         serverrole, targetdir, sid_generator, useeadb,
                         lp=lp)
    else:
        make_smbconf(smbconf, hostname, domain, realm, serverrole,
                     targetdir, sid_generator, useeadb, lp=lp)

    if lp is None:
        lp = samba.param.LoadParm()
    lp.load(smbconf)
    names = guess_names(lp=lp, hostname=hostname, domain=domain,
        dnsdomain=realm, serverrole=serverrole, domaindn=domaindn,
        configdn=configdn, schemadn=schemadn, serverdn=serverdn,
        sitename=sitename, rootdn=rootdn)
    paths = provision_paths_from_lp(lp, names.dnsdomain)

    paths.bind_gid = bind_gid
    paths.wheel_gid = wheel_gid

    if hostip is None:
        logger.info("Looking up IPv4 addresses")
        hostips = interface_ips_v4(lp)
        if len(hostips) > 0:
            hostip = hostips[0]
            if len(hostips) > 1:
                logger.warning("More than one IPv4 address found. Using %s",
                    hostip)
    if hostip == "127.0.0.1":
        hostip = None
    if hostip is None:
        logger.warning("No IPv4 address will be assigned")

    if hostip6 is None:
        logger.info("Looking up IPv6 addresses")
        hostips = interface_ips_v6(lp, linklocal=False)
        if hostips:
            hostip6 = hostips[0]
        if len(hostips) > 1:
            logger.warning("More than one IPv6 address found. Using %s", hostip6)
    if hostip6 is None:
        logger.warning("No IPv6 address will be assigned")

    names.hostip = hostip
    names.hostip6 = hostip6

    if serverrole is None:
        serverrole = lp.get("server role")

    if not os.path.exists(paths.private_dir):
        os.mkdir(paths.private_dir)
    if not os.path.exists(os.path.join(paths.private_dir, "tls")):
        os.mkdir(os.path.join(paths.private_dir, "tls"))

    ldapi_url = "ldapi://%s" % urllib.quote(paths.s4_ldapi_path, safe="")

    schema = Schema(domainsid, invocationid=invocationid,
        schemadn=names.schemadn)

    if backend_type == "ldb":
        provision_backend = LDBBackend(backend_type, paths=paths,
            lp=lp, credentials=credentials,
            names=names, logger=logger)
    elif backend_type == "existing":
        # If support for this is ever added back, then the URI will need to be specified again
        provision_backend = ExistingBackend(backend_type, paths=paths,
            lp=lp, credentials=credentials,
            names=names, logger=logger,
            ldap_backend_forced_uri=None)
    elif backend_type == "fedora-ds":
        provision_backend = FDSBackend(backend_type, paths=paths,
            lp=lp, credentials=credentials,
            names=names, logger=logger, domainsid=domainsid,
            schema=schema, hostname=hostname, ldapadminpass=ldapadminpass,
            slapd_path=slapd_path,
            root=root)
    elif backend_type == "openldap":
        provision_backend = OpenLDAPBackend(backend_type, paths=paths,
            lp=lp, credentials=credentials,
            names=names, logger=logger, domainsid=domainsid,
            schema=schema, hostname=hostname, ldapadminpass=ldapadminpass,
            slapd_path=slapd_path, ol_mmr_urls=ol_mmr_urls)
    else:
        raise ValueError("Unknown LDAP backend type selected")

    provision_backend.init()
    provision_backend.start()

    # only install a new shares config db if there is none
    if not os.path.exists(paths.shareconf):
        logger.info("Setting up share.ldb")
        share_ldb = Ldb(paths.shareconf, session_info=session_info,
                        lp=lp)
        share_ldb.load_ldif_file_add(setup_path("share.ldif"))

    logger.info("Setting up secrets.ldb")
    secrets_ldb = setup_secretsdb(paths,
        session_info=session_info,
        backend_credentials=provision_backend.secrets_credentials, lp=lp)

    try:
        logger.info("Setting up the registry")
        setup_registry(paths.hklm, session_info,
                       lp=lp)

        logger.info("Setting up the privileges database")
        setup_privileges(paths.privilege, session_info, lp=lp)

        logger.info("Setting up idmap db")
        idmap = setup_idmapdb(paths.idmapdb,
            session_info=session_info, lp=lp)

        setup_name_mappings(idmap, sid=str(domainsid),
                            root_uid=root_uid, nobody_uid=nobody_uid,
                            users_gid=users_gid, wheel_gid=wheel_gid)

        logger.info("Setting up SAM db")
        samdb = setup_samdb(paths.samdb, session_info,
                            provision_backend, lp, names, logger=logger,
                            serverrole=serverrole,
                            schema=schema, fill=samdb_fill, am_rodc=am_rodc)

        if serverrole == "domain controller":
            if paths.netlogon is None:
                logger.info("Existing smb.conf does not have a [netlogon] share, but you are configuring a DC.")
                logger.info("Please either remove %s or see the template at %s" %
                        (paths.smbconf, setup_path("provision.smb.conf.dc")))
                assert paths.netlogon is not None

            if paths.sysvol is None:
                logger.info("Existing smb.conf does not have a [sysvol] share, but you"
                        " are configuring a DC.")
                logger.info("Please either remove %s or see the template at %s" %
                        (paths.smbconf, setup_path("provision.smb.conf.dc")))
                assert paths.sysvol is not None

            if not os.path.isdir(paths.netlogon):
                os.makedirs(paths.netlogon, 0755)

        if samdb_fill == FILL_FULL:
            provision_fill(samdb, secrets_ldb, logger,
                           names, paths, schema=schema, targetdir=targetdir,
                           samdb_fill=samdb_fill, hostip=hostip, hostip6=hostip6, domainsid=domainsid,
                           next_rid=next_rid, dc_rid=dc_rid, adminpass=adminpass,
                           krbtgtpass=krbtgtpass, domainguid=domainguid,
                           policyguid=policyguid, policyguid_dc=policyguid_dc,
                           invocationid=invocationid, machinepass=machinepass,
                           ntdsguid=ntdsguid, dns_backend=dns_backend, dnspass=dnspass,
                           serverrole=serverrole, dom_for_fun_level=dom_for_fun_level,
                           am_rodc=am_rodc, lp=lp)

        create_krb5_conf(paths.krb5conf,
                         dnsdomain=names.dnsdomain, hostname=names.hostname,
                         realm=names.realm)
        logger.info("A Kerberos configuration suitable for Samba 4 has been "
                    "generated at %s", paths.krb5conf)

        if serverrole == "domain controller":
            create_dns_update_list(lp, logger, paths)

        provision_backend.post_setup()
        provision_backend.shutdown()

        create_phpldapadmin_config(paths.phpldapadminconfig,
                                   ldapi_url)
    except Exception:
        secrets_ldb.transaction_cancel()
        raise

    # Now commit the secrets.ldb to disk
    secrets_ldb.transaction_commit()

    # the commit creates the dns.keytab, now chown it
    dns_keytab_path = os.path.join(paths.private_dir, paths.dns_keytab)
    if os.path.isfile(dns_keytab_path) and paths.bind_gid is not None:
        try:
            os.chmod(dns_keytab_path, 0640)
            os.chown(dns_keytab_path, -1, paths.bind_gid)
        except OSError:
            if not os.environ.has_key('SAMBA_SELFTEST'):
                logger.info("Failed to chown %s to bind gid %u",
                            dns_keytab_path, paths.bind_gid)

    logger.info("Please install the phpLDAPadmin configuration located at %s into /etc/phpldapadmin/config.php",
            paths.phpldapadminconfig)

    logger.info("Once the above files are installed, your Samba4 server will be ready to use")
    logger.info("Server Role:           %s" % serverrole)
    logger.info("Hostname:              %s" % names.hostname)
    logger.info("NetBIOS Domain:        %s" % names.domain)
    logger.info("DNS Domain:            %s" % names.dnsdomain)
    logger.info("DOMAIN SID:            %s" % str(domainsid))
    if samdb_fill == FILL_FULL:
        logger.info("Admin password:        %s" % adminpass)
    if provision_backend.type is not "ldb":
        if provision_backend.credentials.get_bind_dn() is not None:
            logger.info("LDAP Backend Admin DN: %s" %
                provision_backend.credentials.get_bind_dn())
        else:
            logger.info("LDAP Admin User:       %s" %
                provision_backend.credentials.get_username())

        logger.info("LDAP Admin Password:   %s" %
            provision_backend.credentials.get_password())

        if provision_backend.slapd_command_escaped is not None:
            # now display slapd_command_file.txt to show how slapd must be
            # started next time
            logger.info("Use later the following commandline to start slapd, then Samba:")
            logger.info(provision_backend.slapd_command_escaped)
            logger.info("This slapd-Commandline is also stored under: %s/ldap_backend_startup.sh",
                    provision_backend.ldapdir)

    result = ProvisionResult()
    result.domaindn = domaindn
    result.paths = paths
    result.names = names
    result.lp = lp
    result.samdb = samdb
    result.idmap = idmap
    return result


def provision_become_dc(smbconf=None, targetdir=None,
        realm=None, rootdn=None, domaindn=None, schemadn=None, configdn=None,
        serverdn=None, domain=None, hostname=None, domainsid=None,
        adminpass=None, krbtgtpass=None, domainguid=None, policyguid=None,
        policyguid_dc=None, invocationid=None, machinepass=None, dnspass=None,
        dns_backend=None, root=None, nobody=None, users=None, wheel=None, backup=None,
        serverrole=None, ldap_backend=None, ldap_backend_type=None,
        sitename=None, debuglevel=1):

    logger = logging.getLogger("provision")
    samba.set_debug_level(debuglevel)

    res = provision(logger, system_session(), None,
        smbconf=smbconf, targetdir=targetdir, samdb_fill=FILL_DRS,
        realm=realm, rootdn=rootdn, domaindn=domaindn, schemadn=schemadn,
        configdn=configdn, serverdn=serverdn, domain=domain,
        hostname=hostname, hostip=None, domainsid=domainsid,
        machinepass=machinepass, serverrole="domain controller",
        sitename=sitename, dns_backend=dns_backend, dnspass=dnspass)
    res.lp.set("debuglevel", str(debuglevel))
    return res


def create_phpldapadmin_config(path, ldapi_uri):
    """Create a PHP LDAP admin configuration file.

    :param path: Path to write the configuration to.
    """
    setup_file(setup_path("phpldapadmin-config.php"), path,
            {"S4_LDAPI_URI": ldapi_uri})


def create_krb5_conf(path, dnsdomain, hostname, realm):
    """Write out a file containing zone statements suitable for inclusion in a
    named.conf file (including GSS-TSIG configuration).

    :param path: Path of the new named.conf file.
    :param dnsdomain: DNS Domain name
    :param hostname: Local hostname
    :param realm: Realm name
    """
    setup_file(setup_path("krb5.conf"), path, {
            "DNSDOMAIN": dnsdomain,
            "HOSTNAME": hostname,
            "REALM": realm,
        })


class ProvisioningError(Exception):
    """A generic provision error."""

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return "ProvisioningError: " + self.value


class InvalidNetbiosName(Exception):
    """A specified name was not a valid NetBIOS name."""
    def __init__(self, name):
        super(InvalidNetbiosName, self).__init__(
            "The name '%r' is not a valid NetBIOS name" % name)
