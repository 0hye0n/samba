/*
 * Copyright (c) 2004 - 2006 Kungliga Tekniska H�gskolan
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
 * 3. Neither the name of the Institute nor the names of its contributors 
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

#include "hx_locl.h"
RCSID("$Id$");

/* 
 * name parsing from rfc2253
 * fix so parsing rfc1779 works too 
 * rfc3280
 */

static const struct {
    char *n;
    const heim_oid *(*o)(void);
} no[] = {
    { "C", oid_id_at_countryName },
    { "CN", oid_id_at_commonName },
    { "O", oid_id_at_organizationName },
    { "L", oid_id_at_localityName },
    { "Email", oid_id_pkcs9_emailAddress },
    { "UID", oid_id_Userid },
    { "DC", oid_id_domainComponent }
};

static char *
quote_string(const char *f, size_t len, size_t *rlen)
{
    size_t i, j, tolen;
    const char *from = f;
    char *to;

    tolen = len * 3 + 1;
    to = malloc(tolen);
    if (to == NULL)
	return NULL;

    for (i = 0, j = 0; i < len; i++) {
	if (from[i] == ' ' && i + 1 < len)
	    to[j++] = from[i];
	else if (from[i] == ',' || from[i] == '=' || from[i] == '+' ||
		 from[i] == '<' || from[i] == '>' || from[i] == '#' ||
		 from[i] == ';' || from[i] == ' ')
	{
	    to[j++] = '\\';
	    to[j++] = from[i];
	} else if (((unsigned char)from[i]) >= 32 && ((unsigned char)from[i]) <= 127) {
	    to[j++] = from[i];
	} else {
	    int l = snprintf(&to[j], tolen - j - 1,
			     "#%02x", (unsigned int)from[i]);
	    j += l;
	}
    }
    to[j] = '\0';
    *rlen = j;
    return to;
}


static int
append_string(char **str, size_t *total_len, char *ss, size_t len, int quote)
{
    char *s, *qs;

    if (quote)
	qs = quote_string(ss, len, &len);
    else
	qs = ss;

    s = realloc(*str, len + *total_len + 1);
    if (s == NULL)
	_hx509_abort("allocation failure"); /* XXX */
    memcpy(s + *total_len, qs, len);
    if (qs != ss)
	free(qs);
    s[*total_len + len] = '\0';
    *str = s;
    *total_len += len;
    return 0;
}

static char *
oidtostring(const heim_oid *type)
{
    char *s = NULL, *ss;
    size_t i, total_len = 0;
    
    for (i = 0; i < sizeof(no)/sizeof(no[0]); i++) {
	if (heim_oid_cmp((*no[i].o)(), type) == 0)
	    return strdup(no[i].n);
    }

    for (i = 0; i < type->length; i++) {
	asprintf(&ss, "%u", type->components[i]);
	append_string(&s, &total_len, ss, strlen(ss), 0);
	free(ss);
	if (i + 1 < type->length)
	    append_string(&s, &total_len, ".", 1, 0);
    }
    return s;
}

static const heim_oid *
stringtooid(const char *name, size_t len)
{
    int i;
    
    for (i = 0; i < sizeof(no)/sizeof(no[0]); i++) {
	if (strncasecmp(no[i].n, name, len) == 0)
	    return (*no[i].o)();
    }
    return NULL;
}


int
hx509_name_to_string(const hx509_name name, char **str)
{
    const Name *n = &name->der_name;
    size_t total_len = 0;
    int i, j;

    *str = strdup("");
    if (*str == NULL)
	return ENOMEM;

    for (i = n->u.rdnSequence.len - 1 ; i >= 0 ; i--) {
	int len;

	for (j = 0; j < n->u.rdnSequence.val[i].len; j++) {
	    DirectoryString *ds = &n->u.rdnSequence.val[i].val[j].value;
	    char *oidname;
	    char *ss;
	    
	    oidname = oidtostring(&n->u.rdnSequence.val[i].val[j].type);

	    switch(ds->element) {
	    case choice_DirectoryString_ia5String:
		ss = ds->u.ia5String;
		break;
	    case choice_DirectoryString_printableString:
		ss = ds->u.ia5String;
		break;
	    case choice_DirectoryString_utf8String:
		ss = ds->u.ia5String;
		break;
	    case choice_DirectoryString_bmpString: {
		u_int16_t *bmp = ds->u.bmpString.data;
		size_t len = ds->u.bmpString.length;
		int k;

		ss = malloc(len + 1);
		if (ss == NULL)
		    _hx509_abort("allocation failure"); /* XXX */
		for (k = 0; k < len; k++)
		    ss[k] = bmp[k] & 0xff; /* XXX */
		ss[k] = '\0';
		break;
	    }
	    case choice_DirectoryString_teletexString:
		ss = "teletex-string"; /* XXX */
		break;
	    case choice_DirectoryString_universalString:
		ss = "universalString"; /* XXX */
		break;
	    default:
		_hx509_abort("unknown directory type: %d", ds->element);
		exit(1);
	    }
	    append_string(str, &total_len, oidname, strlen(oidname), 0);
	    free(oidname);
	    append_string(str, &total_len, "=", 1, 0);
	    len = strlen(ss);
	    append_string(str, &total_len, ss, len, 1);
	    if (ds->element == choice_DirectoryString_bmpString)
		free(ss);
	    if (j + 1 < n->u.rdnSequence.val[i].len)
		append_string(str, &total_len, "+", 1, 0);
	}

	if (i > 0)
	    append_string(str, &total_len, ",", 1, 0);
    }
    return 0;
}

/*
 * XXX this function is broken, it needs to compare code points, not
 * bytes.
 */

int
_hx509_name_ds_cmp(const DirectoryString *ds1, const DirectoryString *ds2)
{
    int c;

    c = ds1->element - ds2->element;
    if (c)
	return c;

    switch(ds1->element) {
    case choice_DirectoryString_ia5String:
	c = strcmp(ds1->u.ia5String, ds2->u.ia5String);
	break;
    case choice_DirectoryString_teletexString:
	c = heim_octet_string_cmp(&ds1->u.teletexString,
				  &ds2->u.teletexString);
	break;
    case choice_DirectoryString_printableString:
	c = strcasecmp(ds1->u.printableString, ds2->u.printableString);
	break;
    case choice_DirectoryString_utf8String:
	c = strcmp(ds1->u.utf8String, ds2->u.utf8String);
	break;
    case choice_DirectoryString_universalString:
	c = heim_universal_string_cmp(&ds1->u.universalString,
				      &ds2->u.universalString);
	break;
    case choice_DirectoryString_bmpString:
	c = heim_bmp_string_cmp(&ds1->u.bmpString,
				  &ds2->u.bmpString);
	break;
    default:
	c = 1;
	break;
    }
    return c;
}

int
_hx509_name_cmp(const Name *n1, const Name *n2)
{
    int i, j, c;

    c = n1->u.rdnSequence.len - n2->u.rdnSequence.len;
    if (c)
	return c;

    for (i = 0 ; i < n1->u.rdnSequence.len; i++) {
	c = n1->u.rdnSequence.val[i].len - n2->u.rdnSequence.val[i].len;
	if (c)
	    return c;

	for (j = 0; j < n1->u.rdnSequence.val[i].len; j++) {
	    c = heim_oid_cmp(&n1->u.rdnSequence.val[i].val[j].type,
			     &n1->u.rdnSequence.val[i].val[j].type);
	    if (c)
		return c;
			     
	    c = _hx509_name_ds_cmp(&n1->u.rdnSequence.val[i].val[j].value,
				   &n2->u.rdnSequence.val[i].val[j].value);
	    if (c)
		return c;
	}
    }
    return 0;
}

int
_hx509_name_from_Name(const Name *n, hx509_name *name)
{
    int ret;
    *name = calloc(1, sizeof(**name));
    if (*name == NULL)
	return ENOMEM;
    ret = copy_Name(n, &(*name)->der_name);
    if (ret) {
	free(*name);
	*name = NULL;
    }
    return ret;
}

static int
hx509_der_parse_name(const void *data, size_t length, hx509_name *name)
{
    int ret;
    Name n;

    *name = NULL;
    ret = decode_Name(data, length, &n, NULL);
    if (ret)
	return ret;
    return _hx509_name_from_Name(&n, name);
}

int
hx509_parse_name(const char *str, hx509_name *name)
{
    const char *p, *q;
    char *r;
    size_t len;
    hx509_name n;
    void *ptr;

    *name = NULL;

    n = calloc(1, sizeof(*n));
    if (n == NULL)
	return ENOMEM;

    n->der_name.element = choice_Name_rdnSequence;

    p = str;

    while (p != NULL && *p != '\0') {
	RelativeDistinguishedName *rdn;
	const heim_oid *oid;
	int last;

	q = strchr(p, ',');
	if (q) {
	    len = (q - p);
	    last = 1;
	} else {
	    len = strlen(p);
	    last = 0;
	}

	q = strchr(p, '=');
	if (q == NULL) {
	    /* _hx509_abort("missing = in %s", p); */
	    goto out;
	}
	if (q == p) {
	    /* _hx509_abort("missing name before = in %s", p); */
	    goto out;
	}
	
	if ((q - p) > len) {
	    /* _hx509_abort(" = after , in %s", p); */
	    goto out;
	}

	oid = stringtooid(p, q - p);
	if (oid == NULL) {
	    /* _hx509_abort("unknown type: %.*s", (int)(q - p), p); */
	    goto out;
	}
	
	ptr = realloc(n->der_name.u.rdnSequence.val, 
		      sizeof(n->der_name.u.rdnSequence.val[0]) *
		      (n->der_name.u.rdnSequence.len + 1));
	if (ptr == NULL) {
	    /* _hx509_abort("realloc"); */
	    goto out;
	}
	n->der_name.u.rdnSequence.val = ptr;

	memmove(&n->der_name.u.rdnSequence.val[1],
		&n->der_name.u.rdnSequence.val[0],
		n->der_name.u.rdnSequence.len * 
		sizeof(n->der_name.u.rdnSequence.val[0]));
	
	rdn = &n->der_name.u.rdnSequence.val[0];

	rdn->val = malloc(sizeof(rdn->val[0]));
	if (rdn->val == NULL) {
	    /* _hx509_abort("malloc"); */
	    goto out;
	}
	rdn->len = 1;


	if (copy_oid(oid, &rdn->val[0].type) != 0) {
	    /* _hx509_abort("copy_oid"); */
	    goto out;
	}

	{
	    size_t strlen = len - (q - p) - 1;
	    const char *str = p + (q - p) + 1;
	    
	    r = malloc(strlen + 1);
	    if (r == NULL) {
		/* _hx509_abort("malloc"); */
		goto out;
	    }
	    memcpy(r, str, strlen);
	    r[strlen] = '\0';
	    
	    rdn->val[0].value.element = choice_DirectoryString_printableString;
	    rdn->val[0].value.u.printableString = r;
	}

	p += len + last;
	n->der_name.u.rdnSequence.len += 1;
    }

    *name = n;

    return 0;
out:
    hx509_name_free(&n);
    return HX509_NAME_MALFORMED;
}

int
hx509_name_copy(hx509_context context, const hx509_name from, hx509_name *to)
{
    int ret;

    *to = calloc(1, sizeof(**to));
    if (*to == NULL)
	return ENOMEM;
    ret = copy_Name(&from->der_name, &(*to)->der_name);
    if (ret) {
	free(*to);
	*to = NULL;
	return ENOMEM;
    }
    return 0;
}

int
hx509_name_to_Name(const hx509_name from, Name *to)
{
    return copy_Name(&from->der_name, to);
}


void
hx509_name_free(hx509_name *name)
{
    free_Name(&(*name)->der_name);
    memset(*name, 0, sizeof(**name));
    free(*name);
    *name = NULL;
}

int
hx509_unparse_der_name(const void *data, size_t length, char **str)
{
    hx509_name name;
    int ret;

    ret = hx509_der_parse_name(data, length, &name);
    if (ret)
	return ret;
    
    ret = hx509_name_to_string(name, str);
    hx509_name_free(&name);
    return ret;
}

int
hx509_name_to_der_name(const hx509_name name, void **data, size_t *length)
{
    size_t size;
    int ret;

    ASN1_MALLOC_ENCODE(Name, *data, *length, &name->der_name, &size, ret);
    if (ret)
	return ret;
    if (*length != size)
	_hx509_abort("internal ASN.1 encoder error");

    return 0;
}


int
_hx509_unparse_Name(const Name *aname, char **str)
{
    hx509_name name;
    int ret;

    ret = _hx509_name_from_Name(aname, &name);
    if (ret)
	return ret;

    ret = hx509_name_to_string(name, str);
    hx509_name_free(&name);
    return ret;
}

int
hx509_name_is_null_p(const hx509_name name)
{
    return name->der_name.u.rdnSequence.len == 0;
}
