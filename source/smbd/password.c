#define OLD_NTDOMAIN 1

/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Password and authentication handling
   Copyright (C) Andrew Tridgell 1992-1998
   
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

extern int DEBUGLEVEL;
extern int Protocol;
extern struct in_addr ipzero;

/* users from session setup */
static pstring session_users="";

extern pstring global_myname;
extern fstring global_myworkgroup;

/* Data to do lanman1/2 password challenge. */
static unsigned char saved_challenge[8];
static BOOL challenge_sent=False;

/*******************************************************************
Get the next challenge value - no repeats.
********************************************************************/
void generate_next_challenge(char *challenge)
{
#if 0
        /* 
         * Leave this ifdef'd out while we test
         * the new crypto random number generator.
         * JRA.
         */
	unsigned char buf[16];
	static int counter = 0;
	struct timeval tval;
	int v1,v2;

	/* get a sort-of random number */
	GetTimeOfDay(&tval);
	v1 = (counter++) + sys_getpid() + tval.tv_sec;
	v2 = (counter++) * sys_getpid() + tval.tv_usec;
	SIVAL(challenge,0,v1);
	SIVAL(challenge,4,v2);

	/* mash it up with md4 */
	mdfour(buf, (unsigned char *)challenge, 8);
#else
        unsigned char buf[8];

        generate_random_buffer(buf,8,False);
#endif 
	memcpy(saved_challenge, buf, 8);
	memcpy(challenge,buf,8);
	challenge_sent = True;
}

/*******************************************************************
set the last challenge sent, usually from a password server
********************************************************************/
BOOL set_challenge(unsigned char *challenge)
{
  memcpy(saved_challenge,challenge,8);
  challenge_sent = True;
  return(True);
}

/*******************************************************************
get the last challenge sent
********************************************************************/
static BOOL last_challenge(unsigned char *challenge)
{
  if (!challenge_sent) return(False);
  memcpy(challenge,saved_challenge,8);
  return(True);
}

/* this holds info on user ids that are already validated for this VC */
static user_struct *validated_users;
static int next_vuid = VUID_OFFSET;
static int num_validated_vuids;

/****************************************************************************
check if a uid has been validated, and return an pointer to the user_struct
if it has. NULL if not. vuid is biased by an offset. This allows us to
tell random client vuid's (normally zero) from valid vuids.
****************************************************************************/
user_struct *get_valid_user_struct(uint16 vuid)
{
	user_struct *usp;
	int count=0;

	if (vuid == UID_FIELD_INVALID)
		return NULL;

	for (usp=validated_users;usp;usp=usp->next,count++) {
		if (vuid == usp->vuid) {
			if (count > 10)
                DLIST_PROMOTE(validated_users, usp);
			return usp;
		}
	}

	return NULL;
}

/****************************************************************************
invalidate a uid
****************************************************************************/
void invalidate_vuid(uint16 vuid)
{
	user_struct *vuser = get_valid_user_struct(vuid);

	if (vuser == NULL)
		return;

	DLIST_REMOVE(validated_users, vuser);

	safe_free(vuser->groups);
	delete_nt_token(&vuser->nt_user_token);
	safe_free(vuser);
	num_validated_vuids--;
}

/****************************************************************************
return a validated username
****************************************************************************/
char *validated_username(uint16 vuid)
{
	user_struct *vuser = get_valid_user_struct(vuid);
	if (vuser == NULL)
		return 0;
	return(vuser->user.unix_name);
}

/****************************************************************************
return a validated domain
****************************************************************************/
char *validated_domain(uint16 vuid)
{
	user_struct *vuser = get_valid_user_struct(vuid);
	if (vuser == NULL)
		return 0;
	return(vuser->user.domain);
}


/****************************************************************************
 Create the SID list for this user.
****************************************************************************/

NT_USER_TOKEN *create_nt_token(uid_t uid, gid_t gid, int ngroups, gid_t
                               *groups, BOOL is_guest, NT_USER_TOKEN *sup_tok)
{
	extern DOM_SID global_sid_World;
	extern DOM_SID global_sid_Network;
	extern DOM_SID global_sid_Builtin_Guests;
	extern DOM_SID global_sid_Authenticated_Users;
	NT_USER_TOKEN *token;
	DOM_SID *psids;
	int i, psid_ndx = 0;
	size_t num_sids = 0;
	fstring sid_str;

	if ((token = (NT_USER_TOKEN *)malloc( sizeof(NT_USER_TOKEN) ) ) == NULL)
		return NULL;

	ZERO_STRUCTP(token);

	/* We always have uid/gid plus World and Network and Authenticated Users or Guest SIDs. */
	num_sids = 5 + ngroups;

	if (sup_tok && sup_tok->num_sids)
		num_sids += sup_tok->num_sids;

	if ((token->user_sids = (DOM_SID *)malloc( num_sids*sizeof(DOM_SID))) == NULL) {
		free(token);
		return NULL;
	}

	psids = token->user_sids;

	/*
	 * Note - user SID *MUST* be first in token !
	 * se_access_check depends on this.
	 */

	uid_to_sid( &psids[PRIMARY_USER_SID_INDEX], uid);
	psid_ndx++;

	/*
	 * Primary group SID is second in token. Convention.
	 */

	gid_to_sid( &psids[PRIMARY_GROUP_SID_INDEX], gid);
	psid_ndx++;

	/* Now add the group SIDs. */

	for (i = 0; i < ngroups; i++) {
		if (groups[i] != gid) {
			gid_to_sid( &psids[psid_ndx++], groups[i]);
		}
	}

	/* Now add the additional SIDs from the supplementary token. */
	if (sup_tok) {
		for (i = 0; i < sup_tok->num_sids; i++)
			sid_copy( &psids[psid_ndx++], &sup_tok->user_sids[i] );
	}

	/*
	 * Finally add the "standard" SIDs.
	 * The only difference between guest and "anonymous" (which we
	 * don't really support) is the addition of Authenticated_Users.
	 */

	sid_copy( &psids[psid_ndx++], &global_sid_World);
	sid_copy( &psids[psid_ndx++], &global_sid_Network);

	if (is_guest)
		sid_copy( &psids[psid_ndx++], &global_sid_Builtin_Guests);
	else
		sid_copy( &psids[psid_ndx++], &global_sid_Authenticated_Users);

	token->num_sids = psid_ndx;

	/* Dump list of sids in token */

	for (i = 0; i < token->num_sids; i++) {
		DEBUG(5, ("user token sid %s\n", 
			  sid_to_string(sid_str, &token->user_sids[i])));
	}

	return token;
}

/****************************************************************************
register a uid/name pair as being valid and that a valid password
has been given. vuid is biased by an offset. This allows us to
tell random client vuid's (normally zero) from valid vuids.
****************************************************************************/

uint16 register_vuid(uid_t uid,gid_t gid, char *unix_name, 
                     char *requested_name, char *domain,BOOL guest, NT_USER_TOKEN **pptok)
{
	user_struct *vuser = NULL;
	struct passwd *pwfile; /* for getting real name from passwd file */

	/* Ensure no vuid gets registered in share level security. */
	if(lp_security() == SEC_SHARE)
		return UID_FIELD_INVALID;

	/* Limit allowed vuids to 16bits - VUID_OFFSET. */
	if (num_validated_vuids >= 0xFFFF-VUID_OFFSET)
		return UID_FIELD_INVALID;

	if((vuser = (user_struct *)malloc( sizeof(user_struct) )) == NULL) {
		DEBUG(0,("Failed to malloc users struct!\n"));
		return UID_FIELD_INVALID;
	}

	ZERO_STRUCTP(vuser);

	DEBUG(10,("register_vuid: (%u,%u) %s %s %s guest=%d\n", (unsigned int)uid, (unsigned int)gid,
				unix_name, requested_name, domain, guest ));

	/* Allocate a free vuid. Yes this is a linear search... :-) */
	while( get_valid_user_struct(next_vuid) != NULL ) {
		next_vuid++;
		/* Check for vuid wrap. */
		if (next_vuid == UID_FIELD_INVALID)
			next_vuid = VUID_OFFSET;
	}

	DEBUG(10,("register_vuid: allocated vuid = %u\n", (unsigned int)next_vuid ));

	vuser->vuid = next_vuid;
	vuser->uid = uid;
	vuser->gid = gid;
	vuser->guest = guest;
	fstrcpy(vuser->user.unix_name,unix_name);
	fstrcpy(vuser->user.smb_name,requested_name);
	fstrcpy(vuser->user.domain,domain);

	vuser->n_groups = 0;
	vuser->groups  = NULL;

        vuser->printer_admin = 
                user_in_list(unix_name, lp_printer_admin()) || (uid == 0);

	/* Find all the groups this uid is in and store them. 
		Used by become_user() */
	initialise_groups(unix_name, uid, gid);
	get_current_groups( &vuser->n_groups, &vuser->groups);

	if (*pptok)
		add_supplementary_nt_login_groups(&vuser->n_groups, &vuser->groups, pptok);

	/* Create an NT_USER_TOKEN struct for this user. */
	vuser->nt_user_token = 
                create_nt_token(uid,gid, vuser->n_groups, vuser->groups, guest, *pptok);

	next_vuid++;
	num_validated_vuids++;

	DLIST_ADD(validated_users, vuser);

	DEBUG(3,("uid %d registered to name %s\n",(int)uid,unix_name));

	DEBUG(3, ("Clearing default real name\n"));
	fstrcpy(vuser->user.full_name, "<Full Name>");
	if (lp_unix_realname()) {
		if ((pwfile=sys_getpwnam(vuser->user.unix_name))!= NULL) {
			DEBUG(3, ("User name: %s\tReal name: %s\n",vuser->user.unix_name,pwfile->pw_gecos));
			fstrcpy(vuser->user.full_name, pwfile->pw_gecos);
		}
	}

	memset(&vuser->dc, '\0', sizeof(vuser->dc));

	return vuser->vuid;
}


/****************************************************************************
add a name to the session users list
****************************************************************************/
void add_session_user(char *user)
{
  fstring suser;
  StrnCpy(suser,user,sizeof(suser)-1);

  if (!Get_Pwnam(suser,True)) return;

  if (suser && *suser && !in_list(suser,session_users,False))
    {
      if (strlen(suser) + strlen(session_users) + 2 >= sizeof(pstring))
	DEBUG(1,("Too many session users??\n"));
      else
	{
	  pstrcat(session_users," ");
	  pstrcat(session_users,suser);
	}
    }
}


/****************************************************************************
update the encrypted smbpasswd file from the plaintext username and password
*****************************************************************************/
static BOOL update_smbpassword_file(char *user, char *password)
{
	struct smb_passwd *smbpw;
	BOOL ret;
	
	become_root();
	smbpw = getsmbpwnam(user);
	unbecome_root();

	if(smbpw == NULL) {
		DEBUG(0,("getsmbpwnam returned NULL\n"));
		return False;
	}

	/*
	 * Remove the account disabled flag - we are updating the
	 * users password from a login.
	 */
	smbpw->acct_ctrl &= ~ACB_DISABLED;

	/* Here, the flag is one, because we want to ignore the
           XXXXXXX'd out password */
	ret = change_oem_password( smbpw, password, True);
	if (ret == False) {
		DEBUG(3,("change_oem_password returned False\n"));
	}

	return ret;
}





/****************************************************************************
core of smb password checking routine.
****************************************************************************/
BOOL smb_password_check(char *password, unsigned char *part_passwd, unsigned char *c8)
{
  /* Finish the encryption of part_passwd. */
  unsigned char p21[21];
  unsigned char p24[24];

  if (part_passwd == NULL)
    DEBUG(10,("No password set - allowing access\n"));
  /* No password set - always true ! */
  if (part_passwd == NULL)
    return 1;

  memset(p21,'\0',21);
  memcpy(p21,part_passwd,16);
  E_P24(p21, c8, p24);
#if DEBUG_PASSWORD
  {
    int i;
    DEBUG(100,("Part password (P16) was |"));
    for(i = 0; i < 16; i++)
      DEBUG(100,("%X ", (unsigned char)part_passwd[i]));
    DEBUG(100,("|\n"));
    DEBUG(100,("Password from client was |"));
    for(i = 0; i < 24; i++)
      DEBUG(100,("%X ", (unsigned char)password[i]));
    DEBUG(100,("|\n"));
    DEBUG(100,("Given challenge was |"));
    for(i = 0; i < 8; i++)
      DEBUG(100,("%X ", (unsigned char)c8[i]));
    DEBUG(100,("|\n"));
    DEBUG(100,("Value from encryption was |"));
    for(i = 0; i < 24; i++)
      DEBUG(100,("%X ", (unsigned char)p24[i]));
    DEBUG(100,("|\n"));
  }
#endif
  return (memcmp(p24, password, 24) == 0);
}

/****************************************************************************
 Do a specific test for an smb password being correct, given a smb_password and
 the lanman and NT responses.
****************************************************************************/
BOOL smb_password_ok(struct smb_passwd *smb_pass, uchar chal[8],
                     uchar lm_pass[24], uchar nt_pass[24])
{
	uchar challenge[8];

	if (!lm_pass || !smb_pass) return(False);

	DEBUG(4,("Checking SMB password for user %s\n", 
		 smb_pass->smb_name));

	if(smb_pass->acct_ctrl & ACB_DISABLED) {
		DEBUG(1,("account for user %s was disabled.\n", 
			 smb_pass->smb_name));
		return(False);
	}

	if (chal == NULL)
	{
		DEBUG(5,("use last SMBnegprot challenge\n"));
		if (!last_challenge(challenge))
		{
			DEBUG(1,("no challenge done - password failed\n"));
			return False;
		}
	}
	else
	{
		DEBUG(5,("challenge received\n"));
		memcpy(challenge, chal, 8);
	}

	if ((Protocol >= PROTOCOL_NT1) && (smb_pass->smb_nt_passwd != NULL)) {
		/* We have the NT MD4 hash challenge available - see if we can
		   use it (ie. does it exist in the smbpasswd file).
		*/
		DEBUG(4,("smb_password_ok: Checking NT MD4 password\n"));
		if (smb_password_check((char *)nt_pass, 
				       (uchar *)smb_pass->smb_nt_passwd, 
				       challenge)) {
			DEBUG(4,("NT MD4 password check succeeded\n"));
			return(True);
		}
		DEBUG(4,("NT MD4 password check failed\n"));
	}

	/* Try against the lanman password. smb_pass->smb_passwd == NULL means
	   no password, allow access. */

	DEBUG(4,("Checking LM MD4 password\n"));

	if((smb_pass->smb_passwd == NULL) && 
	   (smb_pass->acct_ctrl & ACB_PWNOTREQ)) {
		DEBUG(4,("no password required for user %s\n",
			 smb_pass->smb_name));
		return True;
	}

	if((smb_pass->smb_passwd != NULL) && 
	   smb_password_check((char *)lm_pass, 
			      (uchar *)smb_pass->smb_passwd, challenge)) {
		DEBUG(4,("LM MD4 password check succeeded\n"));
		return(True);
	}

	DEBUG(4,("LM MD4 password check failed\n"));

	return False;
}


/****************************************************************************
check if a username/password is OK assuming the password is a 24 byte
SMB hash
return True if the password is correct, False otherwise
****************************************************************************/

BOOL pass_check_smb(char *user, char *domain,
		uchar *chal, uchar *lm_pwd, uchar *nt_pwd,
		struct passwd *pwd)
{
	struct passwd *pass;
	struct smb_passwd *smb_pass;

	if (!lm_pwd || !nt_pwd)
	{
		return(False);
	}

	if (pwd != NULL && user == NULL)
	{
		pass = (struct passwd *) pwd;
		user = pass->pw_name;
	}
	else
	{
		pass = smb_getpwnam(user,True);
	}

	if (pass == NULL)
	{
		DEBUG(1,("Couldn't find user '%s' in UNIX password database.\n",user));
		return(False);
	}

	smb_pass = getsmbpwnam(user);

	if (smb_pass == NULL)
	{
		DEBUG(1,("Couldn't find user '%s' in smb_passwd file.\n", user));
		return(False);
	}

	/* Quit if the account was disabled. */
	if(smb_pass->acct_ctrl & ACB_DISABLED) {
		DEBUG(1,("Account for user '%s' was disabled.\n", user));
		return(False);
	}

	/* Ensure the uid's match */
	if (smb_pass->smb_userid != pass->pw_uid)
	{
		DEBUG(0,("Error : UNIX and SMB uids in password files do not match for user '%s'!\n", user));
		return(False);
	}

	if (smb_pass->acct_ctrl & ACB_PWNOTREQ) {
		if (lp_null_passwords()) {
			DEBUG(3,("Account for user '%s' has no password and null passwords are allowed.\n", smb_pass->smb_name));
			return(True);
		} else {
			DEBUG(3,("Account for user '%s' has no password and null passwords are NOT allowed.\n", smb_pass->smb_name));
			return(False);
		}		
	}

	if (smb_password_ok(smb_pass, chal, lm_pwd, nt_pwd))
	{
		return(True);
	}
	
	DEBUG(2,("pass_check_smb failed - invalid password for user [%s]\n", user));
	return False;
}

/****************************************************************************
check if a username/password pair is OK either via the system password
database or the encrypted SMB password database
return True if the password is correct, False otherwise
****************************************************************************/
BOOL password_ok(char *user, char *password, int pwlen, struct passwd *pwd)
{
	BOOL ret;
	if (pwlen == 24 || (lp_encrypted_passwords() && (pwlen == 0) && lp_null_passwords()))
	{
		/* if 24 bytes long assume it is an encrypted password */
		uchar challenge[8];

		if (!last_challenge(challenge))
		{
			DEBUG(0,("Error: challenge not done for user=%s\n", user));
			return False;
		}

		ret = pass_check_smb(user, global_myworkgroup,
		                      challenge, (uchar *)password, (uchar *)password, pwd);
		if (!ret)
		{
			/* BEGIN_ADMIN_LOG */
			if (lp_security() == SEC_DOMAIN)
				sys_adminlog( LOG_ERR, (char *) gettext( "Authentication failed-- user authentication via Microsoft networking was unsuccessful. User name: %s."), user);
			/* END_ADMIN_LOG */
		}
		return ret;
	} 

	return pass_check(user, password, pwlen, pwd, 
			  lp_update_encrypted() ? 
			  update_smbpassword_file : NULL);
}

/****************************************************************************
check if a username is valid
****************************************************************************/
BOOL user_ok(char *user,int snum)
{
	pstring valid, invalid;
	BOOL ret;

	StrnCpy(valid, lp_valid_users(snum), sizeof(pstring)-1);
	StrnCpy(invalid, lp_invalid_users(snum), sizeof(pstring)-1);

	pstring_sub(valid,"%S",lp_servicename(snum));
	pstring_sub(invalid,"%S",lp_servicename(snum));
	
	ret = !user_in_list(user,invalid);
	
	if (ret && valid && *valid) {
		ret = user_in_list(user,valid);
	}

	if (ret && lp_onlyuser(snum)) {
		char *user_list = lp_username(snum);
		pstring_sub(user_list,"%S",lp_servicename(snum));
		ret = user_in_list(user,user_list);
	}

	return(ret);
}




/****************************************************************************
validate a group username entry. Return the username or NULL
****************************************************************************/
static char *validate_group(char *group,char *password,int pwlen,int snum)
{
#ifdef HAVE_NETGROUP
	{
		char *host, *user, *domain;
		setnetgrent(group);
		while (getnetgrent(&host, &user, &domain)) {
			if (user) {
				if (user_ok(user, snum) && 
				    password_ok(user,password,pwlen,NULL)) {
					endnetgrent();
					return(user);
				}
			}
		}
		endnetgrent();
	}
#endif
  
#ifdef HAVE_GETGRENT
	{
		struct group *gptr;
		setgrent();
		while ((gptr = (struct group *)getgrent())) {
			if (strequal(gptr->gr_name,group))
				break;
		}

		/*
		 * As user_ok can recurse doing a getgrent(), we must
		 * copy the member list into a pstring on the stack before
		 * use. Bug pointed out by leon@eatworms.swmed.edu.
		 */

		if (gptr) {
			pstring member_list;
			char *member;
			size_t copied_len = 0;
			int i;

			*member_list = '\0';
			member = member_list;

			for(i = 0; gptr->gr_mem && gptr->gr_mem[i]; i++) {
				size_t member_len = strlen(gptr->gr_mem[i]) + 1;
				if( copied_len + member_len < sizeof(pstring)) { 

					DEBUG(10,("validate_group: = gr_mem = %s\n", gptr->gr_mem[i]));

					safe_strcpy(member, gptr->gr_mem[i], sizeof(pstring) - copied_len - 1);
					copied_len += member_len;
					member += copied_len;
				} else {
					*member = '\0';
				}
			}

			endgrent();

			member = member_list;
			while (*member) {
				static fstring name;
				fstrcpy(name,member);
				if (user_ok(name,snum) &&
				    password_ok(name,password,pwlen,NULL)) {
					endgrent();
					return(&name[0]);
				}

				DEBUG(10,("validate_group = member = %s\n", member));

				member += strlen(member) + 1;
			}
		} else {
			endgrent();
			return NULL;
		}
	}
#endif
	return(NULL);
}



/****************************************************************************
check for authority to login to a service with a given username/password
****************************************************************************/
BOOL authorise_login(int snum,char *user,char *password, int pwlen, 
		     BOOL *guest,BOOL *force,uint16 vuid)
{
  BOOL ok = False;
  
  *guest = False;
  
#if DEBUG_PASSWORD
  DEBUG(100,("checking authorisation on user=%s pass=%s\n",user,password));
#endif

  /* there are several possibilities:
     1) login as the given user with given password
     2) login as a previously registered username with the given password
     3) login as a session list username with the given password
     4) login as a previously validated user/password pair
     5) login as the "user =" user with given password
     6) login as the "user =" user with no password (guest connection)
     7) login as guest user with no password

     if the service is guest_only then steps 1 to 5 are skipped
  */

  if (GUEST_ONLY(snum)) *force = True;

  if (!(GUEST_ONLY(snum) && GUEST_OK(snum)))
    {

      user_struct *vuser = get_valid_user_struct(vuid);

      /* check the given username and password */
      if (!ok && (*user) && user_ok(user,snum)) {
	ok = password_ok(user,password, pwlen, NULL);
	if (ok) DEBUG(3,("ACCEPTED: given username password ok\n"));
      }

      /* check for a previously registered guest username */
      if (!ok && (vuser != 0) && vuser->guest) {	  
	if (user_ok(vuser->user.unix_name,snum) &&
	    password_ok(vuser->user.unix_name, password, pwlen, NULL)) {
	  fstrcpy(user, vuser->user.unix_name);
	  vuser->guest = False;
	  DEBUG(3,("ACCEPTED: given password with registered user %s\n", user));
	  ok = True;
	}
      }


      /* now check the list of session users */
    if (!ok)
    {
      char *auser;
      char *user_list = strdup(session_users);
      if (!user_list) return(False);

      for (auser=strtok(user_list,LIST_SEP); 
           !ok && auser; 
           auser = strtok(NULL,LIST_SEP))
      {
        fstring user2;
        fstrcpy(user2,auser);
        if (!user_ok(user2,snum)) continue;
		  
        if (password_ok(user2,password, pwlen, NULL)) {
          ok = True;
          fstrcpy(user,user2);
          DEBUG(3,("ACCEPTED: session list username and given password ok\n"));
        }
      }
      free(user_list);
    }

    /* check for a previously validated username/password pair */
    if (!ok && (lp_security() > SEC_SHARE) &&
        (vuser != 0) && !vuser->guest &&
        user_ok(vuser->user.unix_name,snum)) {
      fstrcpy(user,vuser->user.unix_name);
      *guest = False;
      DEBUG(3,("ACCEPTED: validated uid ok as non-guest\n"));
      ok = True;
    }

      /* check for a rhosts entry */
      if (!ok && user_ok(user,snum) && check_hosts_equiv(user)) {
	ok = True;
	DEBUG(3,("ACCEPTED: hosts equiv or rhosts entry\n"));
      }

      /* check the user= fields and the given password */
      if (!ok && lp_username(snum)) {
	char *auser;
	pstring user_list;
	StrnCpy(user_list,lp_username(snum),sizeof(pstring));

	pstring_sub(user_list,"%S",lp_servicename(snum));
	  
	for (auser=strtok(user_list,LIST_SEP);
	     auser && !ok;
	     auser = strtok(NULL,LIST_SEP))
	  {
	    if (*auser == '@')
	      {
		auser = validate_group(auser+1,password,pwlen,snum);
		if (auser)
		  {
		    ok = True;
		    fstrcpy(user,auser);
		    DEBUG(3,("ACCEPTED: group username and given password ok\n"));
		  }
	      }
	    else
	      {
		fstring user2;
		fstrcpy(user2,auser);
		if (user_ok(user2,snum) && 
		    password_ok(user2,password,pwlen,NULL))
		  {
		    ok = True;
		    fstrcpy(user,user2);
		    DEBUG(3,("ACCEPTED: user list username and given password ok\n"));
		  }
	      }
	  }
      }      
    } /* not guest only */

  /* check for a normal guest connection */
  if (!ok && GUEST_OK(snum))
    {
      fstring guestname;
      StrnCpy(guestname,lp_guestaccount(snum),sizeof(guestname)-1);
      if (Get_Pwnam(guestname,True))
	{
	  fstrcpy(user,guestname);
	  ok = True;
	  DEBUG(3,("ACCEPTED: guest account and guest ok\n"));
	}
      else
	DEBUG(0,("Invalid guest account %s??\n",guestname));
      *guest = True;
    }

  if (ok && !user_ok(user,snum))
    {
      DEBUG(0,("rejected invalid user %s\n",user));
      ok = False;
    }

  return(ok);
}


/****************************************************************************
read the a hosts.equiv or .rhosts file and check if it
allows this user from this machine
****************************************************************************/
static BOOL check_user_equiv(char *user, char *remote, char *equiv_file)
{
  int plus_allowed = 1;
  char *file_host;
  char *file_user;
  char **lines = file_lines_load(equiv_file, NULL, False);
  int i;

  DEBUG(5, ("check_user_equiv %s %s %s\n", user, remote, equiv_file));
  if (! lines) return False;
  for (i=0; lines[i]; i++) {
    char *buf = lines[i];
    trim_string(buf," "," ");

    if (buf[0] != '#' && buf[0] != '\n') 
    {
      BOOL is_group = False;
      int plus = 1;
      char *bp = buf;
      if (strcmp(buf, "NO_PLUS\n") == 0)
      {
	DEBUG(6, ("check_user_equiv NO_PLUS\n"));
	plus_allowed = 0;
      }
      else {
	if (buf[0] == '+') 
	{
	  bp++;
	  if (*bp == '\n' && plus_allowed) 
	  {
	    /* a bare plus means everbody allowed */
	    DEBUG(6, ("check_user_equiv everybody allowed\n"));
	    file_lines_free(lines);
	    return True;
	  }
	}
	else if (buf[0] == '-')
	{
	  bp++;
	  plus = 0;
	}
	if (*bp == '@') 
	{
	  is_group = True;
	  bp++;
	}
	file_host = strtok(bp, " \t\n");
	file_user = strtok(NULL, " \t\n");
	DEBUG(7, ("check_user_equiv %s %s\n", file_host ? file_host : "(null)", 
                 file_user ? file_user : "(null)" ));
	if (file_host && *file_host) 
	{
	  BOOL host_ok = False;

#if defined(HAVE_NETGROUP) && defined(HAVE_YP_GET_DEFAULT_DOMAIN)
	  if (is_group)
	    {
	      static char *mydomain = NULL;
	      if (!mydomain)
		yp_get_default_domain(&mydomain);
	      if (mydomain && innetgr(file_host,remote,user,mydomain))
		host_ok = True;
	    }
#else
	  if (is_group)
	    {
	      DEBUG(1,("Netgroups not configured\n"));
	      continue;
	    }
#endif

	  /* is it this host */
	  /* the fact that remote has come from a call of gethostbyaddr
	   * means that it may have the fully qualified domain name
	   * so we could look up the file version to get it into
	   * a canonical form, but I would rather just type it
	   * in full in the equiv file
	   */
	  if (!host_ok && !is_group && strequal(remote, file_host))
	    host_ok = True;

	  if (!host_ok)
	    continue;

	  /* is it this user */
	  if (file_user == 0 || strequal(user, file_user)) 
	    {
	      DEBUG(5, ("check_user_equiv matched %s%s %s\n",
			(plus ? "+" : "-"), file_host,
			(file_user ? file_user : "")));
	      file_lines_free(lines);
	      return (plus ? True : False);
	    }
	}
      }
    }
  }
  file_lines_free(lines);
  return False;
}


/****************************************************************************
check for a possible hosts equiv or rhosts entry for the user
****************************************************************************/
BOOL check_hosts_equiv(char *user)
{
  char *fname = NULL;
  pstring rhostsfile;
  struct passwd *pass = Get_Pwnam(user,True);

  if (!pass) 
    return(False);

  fname = lp_hosts_equiv();

  /* note: don't allow hosts.equiv on root */
  if (fname && *fname && (pass->pw_uid != 0)) {
	  if (check_user_equiv(user,client_name(),fname))
		  return(True);
  }
  
  if (lp_use_rhosts())
    {
      char *home = get_user_home_dir(user);
      if (home) {
	      slprintf(rhostsfile, sizeof(rhostsfile)-1, "%s/.rhosts", home);
	      if (check_user_equiv(user,client_name(),rhostsfile))
		      return(True);
      }
    }

  return(False);
}


/****************************************************************************
 Return the client state structure.
****************************************************************************/

struct cli_state *server_client(void)
{
	static struct cli_state pw_cli;
	return &pw_cli;
}

/****************************************************************************
 Support for server level security.
****************************************************************************/

struct cli_state *server_cryptkey(void)
{
	struct cli_state *cli;
	fstring desthost;
	struct in_addr dest_ip;
	char *p, *pserver;
	BOOL connected_ok = False;

	cli = server_client();

	if (!cli_initialise(cli))
		return NULL;

        pserver = strdup(lp_passwordserver());
	p = pserver;

        while(next_token( &p, desthost, LIST_SEP, sizeof(desthost))) {
		standard_sub_basic(desthost);
		strupper(desthost);

		if(!resolve_name( desthost, &dest_ip, 0x20)) {
			DEBUG(1,("server_cryptkey: Can't resolve address for %s\n",desthost));
			continue;
		}

		if (ismyip(dest_ip)) {
			DEBUG(1,("Password server loop - disabling password server %s\n",desthost));
			continue;
		}

		if (cli_connect(cli, desthost, &dest_ip)) {
			DEBUG(3,("connected to password server %s\n",desthost));
			connected_ok = True;
			break;
		}
	}

	free(pserver);

	if (!connected_ok) {
		DEBUG(0,("password server not available\n"));
		cli_shutdown(cli);
		return NULL;
	}

	if (!attempt_netbios_session_request(cli, global_myname, desthost, &dest_ip))
		return NULL;

	DEBUG(3,("got session\n"));

	if (!cli_negprot(cli)) {
		DEBUG(1,("%s rejected the negprot\n",desthost));
		cli_shutdown(cli);
		return NULL;
	}

	if (cli->protocol < PROTOCOL_LANMAN2 ||
	    !(cli->sec_mode & 1)) {
		DEBUG(1,("%s isn't in user level security mode\n",desthost));
		cli_shutdown(cli);
		return NULL;
	}

	DEBUG(3,("password server OK\n"));

	return cli;
}

/****************************************************************************
 Validate a password with the password server.
****************************************************************************/

BOOL server_validate(char *user, char *domain, 
		     char *pass, int passlen,
		     char *ntpass, int ntpasslen)
{
  struct cli_state *cli;
  static unsigned char badpass[24];
  static BOOL tested_password_server = False;
  static BOOL bad_password_server = False;

  cli = server_client();

  if (!cli->initialised) {
    DEBUG(1,("password server %s is not connected\n", cli->desthost));
    return(False);
  }  

  if(badpass[0] == 0)
    memset(badpass, 0x1f, sizeof(badpass));

  if((passlen == sizeof(badpass)) && !memcmp(badpass, pass, passlen)) {
    /* 
     * Very unlikely, our random bad password is the same as the users
     * password. */
    memset(badpass, badpass[0]+1, sizeof(badpass));
  }

  /*
   * Attempt a session setup with a totally incorrect password.
   * If this succeeds with the guest bit *NOT* set then the password
   * server is broken and is not correctly setting the guest bit. We
   * need to detect this as some versions of NT4.x are broken. JRA.
   */

  if(!tested_password_server) {
    if (cli_session_setup(cli, user, (char *)badpass, sizeof(badpass), 
                              (char *)badpass, sizeof(badpass), domain)) {

      /*
       * We connected to the password server so we
       * can say we've tested it.
       */
      tested_password_server = True;

      if ((SVAL(cli->inbuf,smb_vwv2) & 1) == 0) {
        DEBUG(0,("server_validate: password server %s allows users as non-guest \
with a bad password.\n", cli->desthost));
        DEBUG(0,("server_validate: This is broken (and insecure) behaviour. Please do not \
use this machine as the password server.\n"));
        cli_ulogoff(cli);

        /*
         * Password server has the bug.
         */
        bad_password_server = True;
        return False;
      }
      cli_ulogoff(cli);
    }
  } else {

    /*
     * We have already tested the password server.
     * Fail immediately if it has the bug.
     */

    if(bad_password_server) {
      DEBUG(0,("server_validate: [1] password server %s allows users as non-guest \
with a bad password.\n", cli->desthost));
      DEBUG(0,("server_validate: [1] This is broken (and insecure) behaviour. Please do not \
use this machine as the password server.\n"));
      return False;
    }
  }

  /*
   * Now we know the password server will correctly set the guest bit, or is
   * not guest enabled, we can try with the real password.
   */

  if (!cli_session_setup(cli, user, pass, passlen, ntpass, ntpasslen, domain)) {
    DEBUG(1,("password server %s rejected the password\n", cli->desthost));
    return False;
  }

  /* if logged in as guest then reject */
  if ((SVAL(cli->inbuf,smb_vwv2) & 1) != 0) {
    DEBUG(1,("password server %s gave us guest only\n", cli->desthost));
    cli_ulogoff(cli);
    return(False);
  }

  cli_ulogoff(cli);

  return(True);
}

/***********************************************************************
 Connect to a remote machine for domain security authentication
 given a name or IP address.
************************************************************************/

static BOOL connect_to_domain_password_server(struct cli_state *pcli, 
					      char *server,
                                              unsigned char *trust_passwd)
{
  struct in_addr dest_ip;
  fstring remote_machine;

  if(cli_initialise(pcli) == False) {
    DEBUG(0,("connect_to_domain_password_server: unable to initialize client connection.\n"));
    return False;
  }

  if (is_ipaddress(server)) {
	  struct in_addr to_ip;

	  if (!inet_aton(server, &to_ip) ||
	      !name_status_find(0x20, to_ip, remote_machine)) {
		  DEBUG(1, ("connect_to_domain_password_server: Can't "
			    "resolve name for IP %s\n", server));
		  return False;
	  }
  } else {
	  fstrcpy(remote_machine, server);
  }

  standard_sub_basic(remote_machine);
  strupper(remote_machine);

  if(!resolve_name( remote_machine, &dest_ip, 0x20)) {
    DEBUG(1,("connect_to_domain_password_server: Can't resolve address for %s\n", remote_machine));
    cli_shutdown(pcli);
    return False;
  }
  
  if (ismyip(dest_ip)) {
    DEBUG(1,("connect_to_domain_password_server: Password server loop - not using password server %s\n",
         remote_machine));
    cli_shutdown(pcli);
    return False;
  }
  
  if (!cli_connect(pcli, remote_machine, &dest_ip)) {
    DEBUG(0,("connect_to_domain_password_server: unable to connect to SMB server on \
machine %s. Error was : %s.\n", remote_machine, cli_errstr(pcli) ));
    cli_shutdown(pcli);
    return False;
  }
  
  if (!attempt_netbios_session_request(pcli, global_myname, remote_machine, &dest_ip)) {
    DEBUG(0,("connect_to_password_server: machine %s rejected the NetBIOS \
session request. Error was : %s.\n", remote_machine, cli_errstr(pcli) ));
    return False;
  }
  
  pcli->protocol = PROTOCOL_NT1;

  if (!cli_negprot(pcli)) {
    DEBUG(0,("connect_to_domain_password_server: machine %s rejected the negotiate protocol. \
Error was : %s.\n", remote_machine, cli_errstr(pcli) ));
    cli_shutdown(pcli);
    return False;
  }

  if (pcli->protocol != PROTOCOL_NT1) {
    DEBUG(0,("connect_to_domain_password_server: machine %s didn't negotiate NT protocol.\n",
                   remote_machine));
    cli_shutdown(pcli);
    return False;
  }

  /*
   * Do an anonymous session setup.
   */

  if (!cli_session_setup(pcli, "", "", 0, "", 0, "")) {
    DEBUG(0,("connect_to_domain_password_server: machine %s rejected the session setup. \
Error was : %s.\n", remote_machine, cli_errstr(pcli) ));
    cli_shutdown(pcli);
    return False;
  }

  if (!(pcli->sec_mode & 1)) {
    DEBUG(1,("connect_to_domain_password_server: machine %s isn't in user level security mode\n",
               remote_machine));
    cli_shutdown(pcli);
    return False;
  }

  if (!cli_send_tconX(pcli, "IPC$", "IPC", "", 1)) {
    DEBUG(0,("connect_to_domain_password_server: machine %s rejected the tconX on the IPC$ share. \
Error was : %s.\n", remote_machine, cli_errstr(pcli) ));
    cli_shutdown(pcli);
    return False;
  }

  /*
   * We now have an anonymous connection to IPC$ on the domain password server.
   */

  /*
   * Even if the connect succeeds we need to setup the netlogon
   * pipe here. We do this as we may just have changed the domain
   * account password on the PDC and yet we may be talking to
   * a BDC that doesn't have this replicated yet. In this case
   * a successful connect to a DC needs to take the netlogon connect
   * into account also. This patch from "Bjart Kvarme" <bjart.kvarme@usit.uio.no>.
   */

  if(cli_nt_session_open(pcli, PIPE_NETLOGON) == False) {
    DEBUG(0,("connect_to_domain_password_server: unable to open the domain client session to \
machine %s. Error was : %s.\n", remote_machine, cli_errstr(pcli)));
    cli_nt_session_close(pcli);
    cli_ulogoff(pcli);
    cli_shutdown(pcli);
    return False;
  }

  if (cli_nt_setup_creds(pcli, trust_passwd) == False) {
    DEBUG(0,("connect_to_domain_password_server: unable to setup the PDC credentials to machine \
%s. Error was : %s.\n", remote_machine, cli_errstr(pcli)));
    cli_nt_session_close(pcli);
    cli_ulogoff(pcli);
    cli_shutdown(pcli);
    return(False);
  }

  return True;
}

/***********************************************************************
 Utility function to attempt a connection to an IP address of a DC.
************************************************************************/

static BOOL attempt_connect_to_dc(struct cli_state *pcli, struct in_addr *ip, unsigned char *trust_passwd)
{
  fstring dc_name;

  /*
   * Ignore addresses we have already tried.
   */

  if (ip_equal(ipzero, *ip))
	  return False;

  if (!lookup_pdc_name(global_myname, lp_workgroup(), ip, dc_name))
	  return False;

  return connect_to_domain_password_server(pcli, dc_name, trust_passwd);
}



/***********************************************************************
 We have been asked to dynamcially determine the IP addresses of
 the PDC and BDC's for this DOMAIN, and query them in turn.
************************************************************************/
static BOOL find_connect_pdc(struct cli_state *pcli, unsigned char *trust_passwd, time_t last_change_time)
{
	struct in_addr *ip_list = NULL;
	int count = 0;
	int i;
	BOOL connected_ok = False;
	time_t time_now = time(NULL);
	BOOL use_pdc_only = False;

	/*
	 * If the time the machine password has changed
	 * was less than an hour ago then we need to contact
	 * the PDC only, as we cannot be sure domain replication
	 * has yet taken place. Bug found by Gerald (way to go
	 * Gerald !). JRA.
	 */

	if (time_now - last_change_time < 3600) {
		DEBUG(3, ("WARNING: machine password changed < 3600s ago\n"));
		DEBUG(3, ("contacting PDC only.\n"));
		use_pdc_only = True;
	}

	if (!get_dc_list(use_pdc_only, lp_workgroup(), &ip_list, &count))
		return False;

	/*
	 * Firstly try and contact a PDC/BDC who has the same
	 * network address as any of our interfaces.
	 */
	for(i = 0; i < count; i++) {
		if(!is_local_net(ip_list[i]))
			continue;

		if((connected_ok = attempt_connect_to_dc(pcli, &ip_list[i], trust_passwd))) 
			break;
		
		ip_list[i] = ipzero; /* Tried and failed. */
	}

	/*
	 * Secondly try and contact a random PDC/BDC.
	 */
	if(!connected_ok) {
		i = (sys_random() % count);

		if (!(connected_ok = attempt_connect_to_dc(pcli, &ip_list[i], trust_passwd)))
			ip_list[i] = ipzero; /* Tried and failed. */
	}

	/*
	 * Finally go through the IP list in turn, ignoring any addresses
	 * we have already tried.
	 */
	if(!connected_ok) {
		/*
		 * Try and connect to any of the other IP addresses in the PDC/BDC list.
		 * Note that from a WINS server the #1 IP address is the PDC.
		 */
		for(i = 0; i < count; i++) {
			if((connected_ok = attempt_connect_to_dc(pcli, &ip_list[i], trust_passwd)))
				break;
		}
	}

	if(ip_list != NULL)
		free((char *)ip_list);


	return connected_ok;
}



/***********************************************************************
 Do the same as security=server, but using NT Domain calls and a session
 key from the machine password.
************************************************************************/

BOOL domain_client_validate( char *user, char *domain, 
                             char *smb_apasswd, int smb_apasslen, 
                             char *smb_ntpasswd, int smb_ntpasslen,
                             BOOL *user_exists, NT_USER_TOKEN **pptoken)
{
  unsigned char local_challenge[8];
  unsigned char local_lm_response[24];
  unsigned char local_nt_response[24];
  unsigned char trust_passwd[16];
  fstring remote_machine;
  char *p, *pserver;
  NET_ID_INFO_CTR ctr;
  NET_USER_INFO_3 info3;
  struct cli_state cli;
  uint32 smb_uid_low;
  BOOL connected_ok = False;
  time_t last_change_time;

  if (pptoken)
          *pptoken = NULL;

  if(user_exists != NULL)
    *user_exists = True; /* Only set false on a very specific error. */
 
  /* 
   * Check that the requested domain is not our own machine name.
   * If it is, we should never check the PDC here, we use our own local
   * password file.
   */

  if(strequal( domain, global_myname)) {
    DEBUG(3,("domain_client_validate: Requested domain was for this machine.\n"));
    return False;
  }

  /*
   * Next, check that the passwords given were encrypted.
   */

  if(((smb_apasslen != 24) && (smb_apasslen != 0)) || 
     ((smb_ntpasslen != 24) && (smb_ntpasslen != 0))) {

    /*
     * Not encrypted - do so.
     */

    DEBUG(3,("domain_client_validate: User passwords not in encrypted format.\n"));
    generate_random_buffer( local_challenge, 8, False);
    SMBencrypt( (uchar *)smb_apasswd, local_challenge, local_lm_response);
    SMBNTencrypt((uchar *)smb_ntpasswd, local_challenge, local_nt_response);
    smb_apasslen = 24;
    smb_ntpasslen = 24;
    smb_apasswd = (char *)local_lm_response;
    smb_ntpasswd = (char *)local_nt_response;
  } else {

    /*
     * Encrypted - get the challenge we sent for these
     * responses.
     */

    if (!last_challenge(local_challenge)) {
      DEBUG(0,("domain_client_validate: no challenge done - password failed\n"));
      return False;
    }
  }

  /*
   * Get the machine account password for our primary domain
   */
  if (!secrets_fetch_trust_account_password(lp_workgroup(), trust_passwd, &last_change_time))
  {
	  DEBUG(0, ("domain_client_validate: could not fetch trust account password for domain %s\n", lp_workgroup()));
	  return False;
  }

  /*
   * At this point, smb_apasswd points to the lanman response to
   * the challenge in local_challenge, and smb_ntpasswd points to
   * the NT response to the challenge in local_challenge. Ship
   * these over the secure channel to a domain controller and
   * see if they were valid.
   */

  ZERO_STRUCT(cli);

  /*
   * Treat each name in the 'password server =' line as a potential
   * PDC/BDC. Contact each in turn and try and authenticate.
   */

  pserver = lp_passwordserver();
  if (! *pserver) pserver = "*";
  p = pserver;

 retry:

  while (!connected_ok &&
	 next_token(&p,remote_machine,LIST_SEP,sizeof(remote_machine))) {
	  if(strequal(remote_machine, "*")) {
		  connected_ok = find_connect_pdc(&cli, trust_passwd, last_change_time);
	  } else {
		  connected_ok = connect_to_domain_password_server(&cli, remote_machine, trust_passwd);

		  if (!connected_ok) {
			  
			  /* We have not connected to any dc's that are
			     listed.  Why not try connecting to ones that
			     aren't explicitly listed in 'password server'? */

			  DEBUG(3, ("no listed DC's available - trying "
				    "password server = *\n"));

			  pserver = "*";
			  p = pserver;
			  goto retry;
		  }
	  }
  }

  if (!connected_ok) {
    DEBUG(0,("domain_client_validate: Domain password server not available.\n"));
    cli_shutdown(&cli);
    return False;
  }

  /* We really don't care what LUID we give the user. */
  generate_random_buffer( (unsigned char *)&smb_uid_low, 4, False);

  ZERO_STRUCT(info3);

  if(cli_nt_login_network(&cli, domain, user, smb_uid_low, (char *)local_challenge,
                          ((smb_apasslen != 0) ? smb_apasswd : NULL),
                          ((smb_ntpasslen != 0) ? smb_ntpasswd : NULL),
                          &ctr, &info3) == False) {
    uint32 nt_rpc_err;

    cli_error(&cli, NULL, NULL, &nt_rpc_err);
    DEBUG(0,("domain_client_validate: unable to validate password for user %s in domain \
%s to Domain controller %s. Error was %s.\n", user, domain, remote_machine, cli_errstr(&cli)));
    cli_nt_session_close(&cli);
    cli_ulogoff(&cli);
    cli_shutdown(&cli);

    if((nt_rpc_err == NT_STATUS_NO_SUCH_USER) && (user_exists != NULL))
      *user_exists = False;

    return False;
  }

  /*
   * Here, if we really want it, we have lots of info about the user in info3.
   */

  /* Return group membership as returned by NT.  This contains group
     membership in nested groups which doesn't seem to be accessible by any
     other means.  We merge this into the NT_USER_TOKEN associated with the vuid
     later on. */

  if (pptoken && info3.num_groups2 > 0) {
    NT_USER_TOKEN *ptok;
    int i;
    DOM_SID domain_sid;

    if ((ptok = (NT_USER_TOKEN *)malloc( sizeof(NT_USER_TOKEN) ) ) == NULL) {
      DEBUG(0, ("domain_client_validate: Out of memory allocating NT_USER_TOKEN\n"));
      return False;
    }
 
      ptok->num_sids = (size_t)info3.num_groups2;
      if ((ptok->user_sids = (DOM_SID *)malloc( sizeof(DOM_SID) * ptok->num_sids )) == NULL) {
        DEBUG(0, ("domain_client_validate: Out of memory allocating group SIDS\n"));
        free(ptok);
        return False;
      }

      if (!secrets_fetch_domain_sid(lp_workgroup(), &domain_sid)) {
        DEBUG(0, ("domain_client_validate: unable to fetch domain sid.\n"));
        delete_nt_token(&ptok);
        return False;
      }

      for (i = 0; i < ptok->num_sids; i++) {
        sid_copy(&ptok->user_sids[i], &domain_sid);
        sid_append_rid(&ptok->user_sids[i], info3.gids[i].g_rid);
      }
      *pptoken = ptok;
  }

#if 0
  /* 
   * We don't actually need to do this - plus it fails currently with
   * NT_STATUS_INVALID_INFO_CLASS - we need to know *exactly* what to
   * send here. JRA.
   */

  if(cli_nt_logoff(&cli, &ctr) == False) {
    DEBUG(0,("domain_client_validate: unable to log off user %s in domain \
%s to Domain controller %s. Error was %s.\n", user, domain, remote_machine, cli_errstr(&cli)));        
    cli_nt_session_close(&cli);
    cli_ulogoff(&cli);
    cli_shutdown(&cli);
    return False;
  }
#endif /* 0 */

  /* Note - once the cli stream is shutdown the mem_ctx used
   to allocate the other_sids and gids structures has been deleted - so
   these pointers are no longer valid..... */

  cli_nt_session_close(&cli);
  cli_ulogoff(&cli);
  cli_shutdown(&cli);
  return True;
}

#undef OLD_NTDOMAIN
