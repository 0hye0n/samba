/*
  header for ads (active directory) library routines

  basically this is a wrapper around ldap
*/

typedef struct {
	void *ld; /* the active ldap structure */
	struct in_addr ldap_ip; /* the ip of the active connection, if any */
	time_t last_attempt; /* last attempt to reconnect */
	int ldap_port;
	
	/* info needed to find the server */
	struct {
		char *realm;
		char *workgroup;
		char *ldap_server;
		int foreign; /* set to 1 if connecting to a foreign realm */
	} server;

	/* info needed to authenticate */
	struct {
		char *realm;
		char *password;
		char *user_name;
		char *kdc_server;
		int no_bind;
	} auth;

	/* info derived from the servers config */
	struct {
		char *realm;
		char *bind_path;
		char *ldap_server_name;
	} config;
} ADS_STRUCT;

typedef struct {
	char *printerName;
	char *serverName;
	char *shortServerName;
	char *versionNumber;
	char *uNCName;
	char **description;
	char *assetNumber;
	char *bytesPerMinute;
	char *defaultPriority;
	char *driverName;
	char *driverVersion;
	char *location;
	char *operatingSystem;
	char *operatingSystemHotfix;
	char *operatingSystemServicePack;
	char *operatingSystemVersion;
	char *physicalLocationObject;
	char **portName;
	char *printAttributes;
	char **printBinNames;
	char *printCollate;
	char *printColor;
	char *printDuplexSupported;
	char *printEndTime;
	char *printFOrmName;
	char *printKeepPrintedJobs;
	char **printLanguage;
	char *printMACAddress;
	char *printMaxCopies;
	char *printMaxResolutionSupported;
	char *printMaxXExtent;
	char *printMaxYExtent;
	char **printMediaReady;
	char **printMediaSupported;
	char *printMemory;
	char *printMinXExtent;
	char *printMinYExtent;
	char *printNetworkAddress;
	char *printNotify;
	char *printNumberUp;
	char **printOrientationsSupported;
	char *printOwner;
	char *printPagesPerMinute;
	char *printRate;
	char *printRateUnit;
	char *printSeparatorFile;
	char **printShareName;
	char *printSpooling;
	char *printStaplingSupported;
	char *printStartTime;
	char *printStatus;
	char *priority;
} ADS_PRINTER_ENTRY;

/* there are 4 possible types of errors the ads subsystem can produce */
enum ads_error_type {ADS_ERROR_KRB5, ADS_ERROR_GSS, 
		     ADS_ERROR_LDAP, ADS_ERROR_SYSTEM};

typedef struct {
	enum ads_error_type error_type;
	int rc;
	/* For error_type = ADS_ERROR_GSS minor_status describe GSS API error */
	/* Where rc represents major_status of GSS API error */
	int minor_status;
} ADS_STATUS;

#ifdef HAVE_ADS
typedef LDAPMod **ADS_MODLIST;
#else
typedef void **ADS_MODLIST;
#endif

/* macros to simplify error returning */
#define ADS_ERROR(rc) ads_build_error(ADS_ERROR_LDAP, rc, 0)
#define ADS_ERROR_SYSTEM(rc) ads_build_error(ADS_ERROR_SYSTEM, rc?rc:EINVAL, 0)
#define ADS_ERROR_KRB5(rc) ads_build_error(ADS_ERROR_KRB5, rc, 0)
#define ADS_ERROR_GSS(rc, minor) ads_build_error(ADS_ERROR_GSS, rc, minor)

#define ADS_ERR_OK(status) ((status).rc == 0)
#define ADS_SUCCESS ADS_ERROR(0)

/* time between reconnect attempts */
#define ADS_RECONNECT_TIME 5

/* timeout on searches */
#define ADS_SEARCH_TIMEOUT 10

/* ldap control oids */
#define ADS_PAGE_CTL_OID "1.2.840.113556.1.4.319"
#define ADS_NO_REFERRALS_OID "1.2.840.113556.1.4.1339"
#define ADS_SERVER_SORT_OID "1.2.840.113556.1.4.473"

#define UF_DONT_EXPIRE_PASSWD           0x10000
#define UF_MNS_LOGON_ACCOUNT            0x20000
#define UF_SMARTCARD_REQUIRED           0x40000
#define UF_TRUSTED_FOR_DELEGATION       0x80000
#define UF_NOT_DELEGATED               0x100000
#define UF_USE_DES_KEY_ONLY            0x200000
#define UF_DONT_REQUIRE_PREAUTH        0x400000

#define UF_TEMP_DUPLICATE_ACCOUNT       0x0100
#define UF_NORMAL_ACCOUNT               0x0200
#define UF_INTERDOMAIN_TRUST_ACCOUNT    0x0800
#define UF_WORKSTATION_TRUST_ACCOUNT    0x1000
#define UF_SERVER_TRUST_ACCOUNT         0x2000

/* account types */
#define ATYPE_GROUP               0x10000000
#define ATYPE_USER                0x30000000

/* Mailslot or cldap getdcname response flags */
#define ADS_PDC            0x00000001  /* DC is PDC */
#define ADS_GC             0x00000004  /* DC is a GC of forest */
#define ADS_LDAP           0x00000008  /* DC is an LDAP server */
#define ADS_DS             0x00000010  /* DC supports DS */
#define ADS_KDC            0x00000020  /* DC is running KDC */
#define ADS_TIMESERV       0x00000040  /* DC is running time services */
#define ADS_CLOSEST        0x00000080  /* DC is closest to client */
#define ADS_WRITABLE       0x00000100  /* DC has writable DS */
#define ADS_GOOD_TIMESERV  0x00000200  /* DC has hardware clock
	  				 (and running time) */
#define ADS_NDNC           0x00000400  /* DomainName is non-domain NC serviced
	  				 by LDAP server */
#define ADS_PINGS          0x0000FFFF  /* Ping response */
#define ADS_DNS_CONTROLLER 0x20000000  /* DomainControllerName is a DNS name*/
#define ADS_DNS_DOMAIN     0x40000000  /* DomainName is a DNS name */
#define ADS_DNS_FOREST     0x80000000  /* DnsForestName is a DNS name */

/* DomainCntrollerAddressType */
#define ADS_INET_ADDRESS      0x00000001
#define ADS_NETBIOS_ADDRESS   0x00000002
