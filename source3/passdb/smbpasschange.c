/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   change a password in a local smbpasswd file
   Copyright (C) Andrew Tridgell 1998
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "includes.h"


/*************************************************************
add a new user to the local smbpasswd file
*************************************************************/
static BOOL add_new_user(char *user_name, uid_t uid, BOOL trust_account, 
			 BOOL disable_user, BOOL set_no_password,
			 uchar *new_p16, uchar *new_nt_p16)
{
	struct smb_passwd new_smb_pwent;

	/* Create a new smb passwd entry and set it to the given password. */
	new_smb_pwent.smb_userid = uid;
	new_smb_pwent.smb_name = user_name; 
	new_smb_pwent.smb_passwd = NULL;
	new_smb_pwent.smb_nt_passwd = NULL;
	new_smb_pwent.acct_ctrl = (trust_account ? ACB_WSTRUST : ACB_NORMAL);
	
	if(disable_user) {
		new_smb_pwent.acct_ctrl |= ACB_DISABLED;
	} else if (set_no_password) {
		new_smb_pwent.acct_ctrl |= ACB_PWNOTREQ;
	} else {
		new_smb_pwent.smb_passwd = new_p16;
		new_smb_pwent.smb_nt_passwd = new_nt_p16;
	}
	
	return add_smbpwd_entry(&new_smb_pwent);
}


/*************************************************************
change a password entry in the local smbpasswd file
*************************************************************/
BOOL local_password_change(char *user_name, BOOL trust_account, BOOL add_user,
			   BOOL enable_user, BOOL disable_user, BOOL set_no_password,
			   char *new_passwd, 
			   char *err_str, size_t err_str_len,
			   char *msg_str, size_t msg_str_len)
{
	struct passwd  *pwd;
	void *vp;
	struct smb_passwd *smb_pwent;
	uchar           new_p16[16];
	uchar           new_nt_p16[16];

	*err_str = '\0';
	*msg_str = '\0';

	pwd = getpwnam(user_name);
	
	/*
	 * Check for a machine account.
	 */
	
	if(trust_account && !pwd) {
		slprintf(err_str, err_str_len - 1, "User %s does not \
exist in system password file (usually /etc/passwd). Cannot add machine \
account without a valid system user.\n", user_name);
		return False;
	}

	/* Calculate the MD4 hash (NT compatible) of the new password. */
	nt_lm_owf_gen(new_passwd, new_nt_p16, new_p16);

	/*
	 * Open the smbpaswd file.
	 */
	vp = startsmbpwent(True);
	if (!vp && errno == ENOENT) {
		FILE *fp;
		slprintf(msg_str,msg_str_len-1,
			"smbpasswd file did not exist - attempting to create it.\n");
		fp = fopen(lp_smb_passwd_file(), "w");
		if (fp) {
			fprintf(fp, "# Samba SMB password file\n");
			fclose(fp);
			vp = startsmbpwent(True);
		}
	}

	if (!vp) {
		slprintf(err_str, err_str_len-1, "Cannot open file %s. Error was %s\n",
			lp_smb_passwd_file(), strerror(errno) );
		return False;
	}
  
	/* Get the smb passwd entry for this user */
	smb_pwent = getsmbpwnam(user_name);
	if (smb_pwent == NULL) {
		if(add_user == False) {
			slprintf(err_str, err_str_len-1,
				"Failed to find entry for user %s.\n", pwd->pw_name);
			endsmbpwent(vp);
			return False;
		}

		if (add_new_user(user_name, pwd->pw_uid, trust_account, disable_user,
				 set_no_password, new_p16, new_nt_p16)) {
			slprintf(msg_str, msg_str_len-1, "Added user %s.\n", user_name);
			endsmbpwent(vp);
			return True;
		} else {
			slprintf(err_str, err_str_len-1, "Failed to add entry for user %s.\n", user_name);
			endsmbpwent(vp);
			return False;
		}
	} else {
		/* the entry already existed */
		add_user = False;
	}

	/*
	 * We are root - just write the new password
	 * and the valid last change time.
	 */

	if(disable_user) {
		smb_pwent->acct_ctrl |= ACB_DISABLED;
	} else if (enable_user) {
		if(smb_pwent->smb_passwd == NULL) {
			smb_pwent->smb_passwd = new_p16;
			smb_pwent->smb_nt_passwd = new_nt_p16;
		}
		smb_pwent->acct_ctrl &= ~ACB_DISABLED;
	} else if (set_no_password) {
		smb_pwent->acct_ctrl |= ACB_PWNOTREQ;
		/* This is needed to preserve ACB_PWNOTREQ in mod_smbfilepwd_entry */
		smb_pwent->smb_passwd = NULL;
		smb_pwent->smb_nt_passwd = NULL;
	} else {
		smb_pwent->acct_ctrl &= ~ACB_PWNOTREQ;
		smb_pwent->smb_passwd = new_p16;
		smb_pwent->smb_nt_passwd = new_nt_p16;
	}
	
	if(mod_smbpwd_entry(smb_pwent,True) == False) {
		slprintf(err_str, err_str_len-1, "Failed to modify entry for user %s.\n",
			pwd->pw_name);
		endsmbpwent(vp);
		return False;
	}

	endsmbpwent(vp);

	return True;
}
