/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "admin_locl.h"

RCSID("$Id$");

struct entry{
    char *principal;
    char *key;
    char *max_life;
    char *max_renew;
    char *created;
    char *modified;
    char *valid_start;
    char *valid_end;
    char *pw_end;
    char *flags;
};

static char *
skip_next(char *p)
{
    while(*p && !isspace(*p)) 
	p++;
    *p++ = 0;
    while(*p && isspace(*p)) p++;
    return p;
}

static time_t*
parse_time_string(time_t *t, char *s)
{
    int year, month, date, hour, minute, second;
    struct tm tm;
    if(strcmp(s, "-") == 0)
	return NULL;
    if(t == NULL)
	t = malloc(sizeof(*t));
    sscanf(s, "%04d%02d%02d%02d%02d%02d", 
	   &year, &month, &date, &hour, &minute, &second);
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = date;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;
    *t = timegm(&tm);
    return t;
}

unsigned*
parse_integer(unsigned *u, char *s)
{
    if(strcmp(s, "-") == 0)
	return NULL;
    if(u == NULL)
	u = malloc(sizeof(*u));
    sscanf(s, "%u", u);
    return u;
}

static void
parse_keys(hdb_entry *ent, char *str)
{
    int tmp;
    char *p;
    int i;
    
    p = strsep(&str, ":");
    sscanf(p, "%d", &tmp);
    ent->kvno = tmp;
    p = strsep(&str, ":");
    while(p){
	Key *key;
	key = realloc(ent->keys.val, 
		      (ent->keys.len + 1) * sizeof(*ent->keys.val));
	if(key == NULL)
	    abort();
	ent->keys.val = key;
	key = ent->keys.val + ent->keys.len;
	ent->keys.len++;
	memset(key, 0, sizeof(*key));
	sscanf(p, "%d", &tmp);
	key->mkvno = tmp;
	p = strsep(&str, ":");
	sscanf(p, "%d", &tmp);
	key->key.keytype = tmp;
	p = strsep(&str, ":");
	krb5_data_alloc(&key->key.keyvalue, (strlen(p) - 1) / 2 + 1);
	for(i = 0; i < strlen(p); i += 2){
	    sscanf(p + i, "%02x", &tmp);
	    ((u_char*)key->key.keyvalue.data)[i / 2] = tmp;
	}
	p = strsep(&str, ":");
	if (p == NULL) {
	    key->salt = malloc(sizeof(*key->salt));
	    krb5_data_zero (key->salt);
	} else {
	    if(strcmp(p, "-") != 0){
		size_t p_len = strlen(p);

		key->salt = malloc(sizeof(*key->salt));
		if (p_len) {
		    krb5_data_alloc(key->salt, (p_len - 1) / 2 + 1);
		    for(i = 0; i < p_len; i += 2){
			sscanf(p + i, "%02x", &tmp);
			((u_char*)key->salt->data)[i / 2] = tmp;
		    }
		} else
		    krb5_data_zero (key->salt);
	    }
	    p = strsep(&str, ":");
	}
    }
}

static Event*
parse_event(Event *ev, char *str)
{
    char *p;
    if(strcmp(str, "-") == 0)
	return NULL;
    if(ev == NULL)
	ev = malloc(sizeof(*ev));
    memset(ev, 0, sizeof(*ev));
    p = strsep(&str, ":");
    parse_time_string(&ev->time, p);
    p = strsep(&str, ":");
    krb5_parse_name(context, p, &ev->principal);
    return ev;
}

static HDBFlags
parse_hdbflags2int(char *str)
{
    unsigned i;
    parse_integer(&i, str);

    return int2HDBFlags(i);
}

static void
doit(char *filename, int merge)
{
    FILE *f;
    HDB *db;
    char s[1024];
    char *p;
    int line;
    int err;
    int flags = O_RDWR;

    struct entry e;
    hdb_entry ent;

    f = fopen(filename, "r");
    if(f == NULL){
	krb5_warn(context, errno, "%s: %s", filename);
	return;
    }
    if(!merge)
	flags |= O_CREAT | O_TRUNC;
    err = hdb_open(context, &db, database, flags, 0600);
    if(err){
	krb5_warn(context, err, "hdb_open");
	fclose(f);
	return;
    }
    line = 0;
    while(fgets(s, sizeof(s), f)){
	line++;
	e.principal = s;
	for(p = s; *p; p++){
	    if(*p == '\\')
		p++;
	    else if(isspace(*p)) {
		*p = 0;
		break;
	    }
	}
	p = skip_next(p);
	
	e.key = p;
	p = skip_next(p);

	e.created = p;
	p = skip_next(p);

	e.modified = p;
	p = skip_next(p);

	e.valid_start = p;
	p = skip_next(p);

	e.valid_end = p;
	p = skip_next(p);

	e.pw_end = p;
	p = skip_next(p);

	e.max_life = p;
	p = skip_next(p);

	e.max_renew = p;
	p = skip_next(p);

	e.flags = p;
	p = skip_next(p);

	memset(&ent, 0, sizeof(ent));
	err = krb5_parse_name(context, e.principal, &ent.principal);
	if(err){
	    fprintf(stderr, "%s:%s:%s (%s)\n", 
		    filename, 
		    line,
		    krb5_get_err_text(context, err),
		    e.principal);
	    continue;
	}
	
	parse_keys(&ent, e.key);
	
	parse_event(&ent.created_by, e.created);
	ent.modified_by = parse_event(NULL, e.modified);
	ent.valid_start = parse_time_string(NULL, e.valid_start);
	ent.valid_end = parse_time_string(NULL, e.valid_end);
	ent.pw_end = parse_time_string(NULL, e.pw_end);
	ent.max_life = parse_integer(NULL, e.max_life);
	ent.max_renew = parse_integer(NULL, e.max_renew);
	
	ent.flags = parse_hdbflags2int(e.flags);
	db->store(context, db, 1, &ent);
	hdb_free_entry (context, &ent);
    }
    db->close(context, db);
    fclose(f);
}

int
load(int argc, char **argv)
{
    if(argc < 2){
	krb5_warnx(context, "Usage: load filename");
	return 0;
    }
    doit(argv[1], 0);
    return 0;
}

int
merge(int argc, char **argv)
{
    if(argc < 2){
	krb5_warnx(context, "Usage: merge filename");
	return 0;
    }
    doit(argv[1], 1);
    return 0;
}
