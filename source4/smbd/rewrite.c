#include "includes.h"

/*

 this is a set of temporary stub functions used during the core smbd rewrite.
 This file will need to go away before the rewrite is complete
*/

void mangle_reset_cache(void) 
{}

void reset_stat_cache(void)
{}


BOOL set_current_service(void *conn, BOOL x)
{ return True; }

void change_to_root_user(void)
{}

void load_printers(void)
{}

void file_init(void)
{}

BOOL init_oplocks(void)
{ return True; }

BOOL init_change_notify(void)
{ return True; }


BOOL pcap_printername_ok(const char *service, char *foo)
{ return True; }

void become_root(void)
{}

void unbecome_root(void)
{}

BOOL namecache_enable(void)
{ return True; }

BOOL locking_init(int read_only)
{ return True; }

BOOL share_info_db_init(void)
{ return True; }

BOOL init_registry(void)
{ return True; }

BOOL share_access_check(struct request_context *req, struct tcon_context *conn, int snum, uint32 desired_access)
{ return True; }

BOOL init_names(void)
{ return True; }

BOOL uid_to_sid(DOM_SID *sid, uid_t uid)
{
	ZERO_STRUCTP(sid);
	return True;
}

BOOL gid_to_sid(DOM_SID *sid, gid_t gid)
{
	ZERO_STRUCTP(sid);
	return True;
}


BOOL become_user_permanently(uid_t uid, gid_t gid)
{ return True; }


int sys_getgrouplist(const char *user, gid_t gid, gid_t *groups, int *ngroups)
{
	return 0;
}
