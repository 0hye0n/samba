/* 
   ldb database library

   Copyright (C) Simo Sorce 2005

     ** NOTE! The following LGPL license applies to the ldb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 *  Name: ldb
 *
 *  Component: ldb dn explode and utility functions
 *
 *  Description: - explode a dn into it's own basic elements
 *                 and put them in a structure
 *               - manipulate ldb_dn structures
 *
 *  Author: Simo Sorce
 */

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"
#include <ctype.h>

#define LDB_DN_NULL_FAILED(x) if (!(x)) goto failed

#define LDB_SPECIAL "@SPECIAL"

int ldb_dn_is_special(const struct ldb_dn *dn)
{
	if (dn == NULL || dn->comp_num != 1) return 0;

	return ! strcmp(dn->components[0].name, LDB_SPECIAL);
}

int ldb_dn_check_special(const struct ldb_dn *dn, const char *check)
{
	if (dn == NULL || dn->comp_num != 1) return 0;

	return ! strcmp((char *)dn->components[0].value.data, check);
}

static int ldb_dn_is_valid_attribute_name(const char *name)
{
	if (name == NULL) return 0;

	while (*name) {
		if (! isascii(*name)) {
			return 0;
		}
		if (! (isalnum((unsigned char)*name) || *name == '-')) {
			return 0;
		}
		name++;
	}

	return 1;
}

char *ldb_dn_escape_value(void *mem_ctx, struct ldb_val value)
{
	const char *p, *s, *src;
	char *d, *dst;
	int len;

	if (!value.length)
		return NULL;

	p = s = src = (const char *)value.data;
	len = value.length;

	/* allocate destination string, it will be at most 3 times the source */
	dst = d = talloc_array(mem_ctx, char, len * 3 + 1);
	LDB_DN_NULL_FAILED(dst);

	while (p - src < len) {

		p += strcspn(p, ",=\n+<>#;\\\"");

		if (p - src == len) /* found no escapable chars */
			break;

		memcpy(d, s, p - s); /* copy the part of the string before the stop */
		d += (p - s); /* move to current position */

		if (*p) { /* it is a normal escapable character */
			*d++ = '\\';
			*d++ = *p++;
		} else { /* we have a zero byte in the string */
			strncpy(d, "\00", 3); /* escape the zero */
			d = d + 3;
			p++; /* skip the zero */
		}
		s = p; /* move forward */
	}

	/* copy the last part (with zero) and return */
	memcpy(d, s, &src[len] - s + 1);

	return dst;

failed:
	talloc_free(dst);
	return NULL;
}

static struct ldb_val ldb_dn_unescape_value(void *mem_ctx, const char *src)
{
	struct ldb_val value;
	unsigned x;
	char *p, *dst = NULL, *end;

	value.length = 0;

	LDB_DN_NULL_FAILED(src);

	dst = p = talloc_memdup(mem_ctx, src, strlen(src) + 1);
	LDB_DN_NULL_FAILED(dst);

	end = &dst[strlen(dst)];

	while (*p) {
		p += strcspn(p, ",=\n+<>#;\\\"");

		if (*p == '\\') {
			if (strchr(",=\n+<>#;\\\"", p[1])) {
				memmove(p, p + 1, end - (p + 1) + 1);
				end--;
				p++;
				continue;
			}

			if (sscanf(p + 1, "%02x", &x) == 1) {
				*p = (unsigned char)x;
				memmove(p + 1, p + 3, end - (p + 3) + 1);
				end -= 2;
				p++;
				continue;
			}
		}

		/* a string with not escaped specials is invalid (tested) */
		if (*p != '\0') {
			goto failed;
		}
	}

	value.length = end - dst;
	value.data = (uint8_t *)dst;
	return value;

failed:
	talloc_free(dst);
	return value;
}

/* check if the string contains quotes
 * skips leading and trailing spaces
 * - returns 0 if no quotes found
 * - returns 1 if quotes are found and put their position
 *   in *quote_start and *quote_end parameters
 * - return -1 if there are open quotes
 */

static int get_quotes_position(const char *source, int *quote_start, int *quote_end)
{
	const char *p;

	if (source == NULL || quote_start == NULL || quote_end == NULL) return -1;

	p = source;

	/* check if there are quotes surrounding the value */
	p += strspn(p, " \n"); /* skip white spaces */

	if (*p == '\"') {
		*quote_start = p - source;

		p++;
		while (*p != '\"') {
			p = strchr(p, '\"');
			LDB_DN_NULL_FAILED(p);

			if (*(p - 1) == '\\')
				p++;
		}

		*quote_end = p - source;
		return 1;
	}

	return 0;

failed:
	return -1;
}

static char *seek_to_separator(char *string, const char *separators)
{
	char *p;
	int ret, qs, qe;

	if (string == NULL || separators == NULL) return NULL;

	p = strchr(string, '=');
	LDB_DN_NULL_FAILED(p);

	p++;

	/* check if there are quotes surrounding the value */

	ret = get_quotes_position(p, &qs, &qe);
	if (ret == -1)
		return NULL;

	if (ret == 1) { /* quotes found */

		p += qe; /* positioning after quotes */
		p += strspn(p, " \n"); /* skip white spaces after the quote */

		if (strcspn(p, separators) != 0) /* if there are characters between quotes */
			return NULL;	    /* and separators, the dn is invalid */

		return p; /* return on the separator */
	}

	/* no quotes found seek to separators */
	ret = strcspn(p, separators);
	if (ret == 0) /* no separators ?! bail out */
		return NULL;

	return p + ret;

failed:
	return NULL;
}

static char *ldb_dn_trim_string(char *string, const char *edge)
{
	char *s, *p;

	/* seek out edge from start of string */
	s = string + strspn(string, edge);

	/* backwards skip from end of string */
	p = &s[strlen(s) - 1];
	while (p > s && strchr(edge, *p)) {
		*p = '\0';
		p--;
	}

	return s;
}

/* we choosed to not support multpile valued components */
static struct ldb_dn_component ldb_dn_explode_component(void *mem_ctx, char *raw_component)
{
	struct ldb_dn_component dc;
	char *p;
	int ret, qs, qe;

	if (raw_component == NULL) {
		dc.name = NULL;
		return dc;
	}

	/* find attribute type/value separator */
	p = strchr(raw_component, '=');
	LDB_DN_NULL_FAILED(p);

	*p++ = '\0'; /* terminate name and point to value */

	/* copy and trim name in the component */
	dc.name = talloc_strdup(mem_ctx, ldb_dn_trim_string(raw_component, " \n"));
	if (!dc.name)
		return dc;

	if (! ldb_dn_is_valid_attribute_name(dc.name)) {
		goto failed;
	}

	ret = get_quotes_position(p, &qs, &qe);

	switch (ret) {
	case 0: /* no quotes trim the string */
		p = ldb_dn_trim_string(p, " \n");
		dc.value = ldb_dn_unescape_value(mem_ctx, p);
		break;

	case 1: /* quotes found get the unquoted string */
		p[qe] = '\0';
		p = p + qs + 1;
		dc.value.length = strlen(p);
		dc.value.data = talloc_memdup(mem_ctx, p, dc.value.length + 1);
		break;

	default: /* mismatched quotes ot other error, bail out */
		goto failed;
	}

	if (dc.value.length == 0) {
		goto failed;
	}

	return dc;

failed:
	talloc_free(dc.name);
	dc.name = NULL;
	return dc;
}

struct ldb_dn *ldb_dn_new(void *mem_ctx)
{
	struct ldb_dn *edn;

	edn = talloc(mem_ctx, struct ldb_dn);
	LDB_DN_NULL_FAILED(edn);

	/* Initially there are no components */
	edn->comp_num = 0;
	edn->components = NULL;

	return edn;

failed:
	return NULL;
}

struct ldb_dn *ldb_dn_explode(void *mem_ctx, const char *dn)
{
	struct ldb_dn *edn; /* the exploded dn */
	char *pdn, *p;

	if (dn == NULL) return NULL;

	/* Allocate a structure to hold the exploded DN */
	edn = ldb_dn_new(mem_ctx);
	pdn = NULL;

	/* Empty DNs */
	if (dn[0] == '\0') {
		return edn;
	}

	/* Special DNs case */
	if (dn[0] == '@') {
		edn->comp_num = 1;
		edn->components = talloc(edn, struct ldb_dn_component);
		if (edn->components == NULL) goto failed;
		edn->components[0].name = talloc_strdup(edn->components, LDB_SPECIAL);
		if (edn->components[0].name == NULL) goto failed;
		edn->components[0].value.data = (uint8_t *)talloc_strdup(edn->components, dn);
		if (edn->components[0].value.data== NULL) goto failed;
		edn->components[0].value.length = strlen(dn);
		return edn;
	}

	pdn = p = talloc_strdup(edn, dn);
	LDB_DN_NULL_FAILED(pdn);

	/* get the components */
	do {
		char *t;

		/* terminate the current component and return pointer to the next one */
		t = seek_to_separator(p, ",;");
		LDB_DN_NULL_FAILED(t);

		if (*t) { /* here there is a separator */
			*t = '\0'; /*terminate */
			t++; /* a separtor means another component follows */
		}

		/* allocate space to hold the dn component */
		edn->components = talloc_realloc(edn, edn->components,
						 struct ldb_dn_component,
						 edn->comp_num + 1);
		if (edn->components == NULL)
			goto failed;

		/* store the exploded component in the main structure */
		edn->components[edn->comp_num] = ldb_dn_explode_component(edn, p);
		LDB_DN_NULL_FAILED(edn->components[edn->comp_num].name);

		edn->comp_num++;

		/* jump to the next component if any */
		p = t;

	} while(*p);

	talloc_free(pdn);
	return edn;

failed:
	talloc_free(pdn);
	talloc_free(edn);
	return NULL;
}

char *ldb_dn_linearize(void *mem_ctx, const struct ldb_dn *edn)
{
	char *dn, *value;
	int i;

	if (edn == NULL) return NULL;

	/* Special DNs */
	if (ldb_dn_is_special(edn)) {
		dn = talloc_strdup(mem_ctx, (char *)edn->components[0].value.data);
		return dn;
	}

	dn = talloc_strdup(mem_ctx, "");
	LDB_DN_NULL_FAILED(dn);

	for (i = 0; i < edn->comp_num; i++) {
		value = ldb_dn_escape_value(dn, edn->components[i].value);
		LDB_DN_NULL_FAILED(value);

		if (i == 0) {
			dn = talloc_asprintf_append(dn, "%s=%s", edn->components[i].name, value);
		} else {
			dn = talloc_asprintf_append(dn, ",%s=%s", edn->components[i].name, value);
		}
		LDB_DN_NULL_FAILED(dn);

		talloc_free(value);
	}

	return dn;

failed:
	talloc_free(dn);
	return NULL;
}

/* compare DNs using casefolding compare functions */

int ldb_dn_compare_base(struct ldb_context *ldb,
		   const struct ldb_dn *base,
		   const struct ldb_dn *dn)
{
	int ret;
	int n0, n1;

	if (base->comp_num > dn->comp_num) {
		return (dn->comp_num - base->comp_num);
	}

	if (base == NULL || base->comp_num == 0) return 0;
	if (dn == NULL || dn->comp_num == 0) return -1;
	if (base->comp_num > dn->comp_num) return -1;

	/* if the number of components doesn't match they differ */
	n0 = base->comp_num - 1;
	n1 = dn->comp_num - 1;
	while (n0 >= 0 && n1 >= 0) {
		const struct ldb_attrib_handler *h;

		/* compare names (attribute names are guaranteed to be ASCII only) */
		ret = ldb_caseless_cmp(base->components[n0].name,
				       dn->components[n1].name);
		if (ret) {
			return ret;
		}

		/* names match, compare values */
		h = ldb_attrib_handler(ldb, base->components[n0].name);
		ret = h->comparison_fn(ldb, ldb, &(base->components[n0].value),
						  &(dn->components[n1].value));
		if (ret) {
			return ret;
		}
		n1--;
		n0--;
	}

	return 0;
}

int ldb_dn_compare(struct ldb_context *ldb,
		   const struct ldb_dn *edn0,
		   const struct ldb_dn *edn1)
{
	if (edn0 == NULL || edn1 == NULL) return edn1 - edn0;

	if (edn0->comp_num != edn1->comp_num)
		return (edn1->comp_num - edn0->comp_num);

	return ldb_dn_compare_base(ldb, edn0, edn1);
}

int ldb_dn_cmp(struct ldb_context *ldb, const char *dn0, const char *dn1)
{
	struct ldb_dn *edn0;
	struct ldb_dn *edn1;
	int ret;

	if (dn0 == NULL || dn1 == NULL) return dn1 - dn0;

	edn0 = ldb_dn_explode_casefold(ldb, dn0);
	if (edn0 == NULL) return 1;

	edn1 = ldb_dn_explode_casefold(ldb, dn1);
	if (edn1 == NULL) {
		talloc_free(edn0);
		return -1;
	}

	ret = ldb_dn_compare(ldb, edn0, edn1);

	talloc_free(edn0);
	talloc_free(edn1);

	return ret;
}

/*
  casefold a dn. We need to casefold the attribute names, and canonicalize 
  attribute values of case insensitive attributes.
*/
struct ldb_dn *ldb_dn_casefold(struct ldb_context *ldb, const struct ldb_dn *edn)
{
	struct ldb_dn *cedn;
	int i;

	if (edn == NULL) return NULL;

	cedn = ldb_dn_new(ldb);
	LDB_DN_NULL_FAILED(cedn);

	cedn->comp_num = edn->comp_num;
	cedn->components = talloc_array(cedn, struct ldb_dn_component, edn->comp_num);
	LDB_DN_NULL_FAILED(cedn->components);

	for (i = 0; i < edn->comp_num; i++) {
		struct ldb_dn_component dc;
		const struct ldb_attrib_handler *h;

		dc.name = ldb_casefold(cedn, edn->components[i].name);
		LDB_DN_NULL_FAILED(dc.name);

		h = ldb_attrib_handler(ldb, dc.name);
		if (h->canonicalise_fn(ldb, cedn, &(edn->components[i].value), &(dc.value)) != 0) {
			goto failed;
		}

		cedn->components[i] = dc;
	}

	return cedn;

failed:
	talloc_free(cedn);
	return NULL;
}

struct ldb_dn *ldb_dn_explode_casefold(struct ldb_context *ldb, const char *dn)
{
	struct ldb_dn *edn, *cdn;

	if (dn == NULL) return NULL;

	edn = ldb_dn_explode(ldb, dn);
	if (edn == NULL) return NULL;

	cdn = ldb_dn_casefold(ldb, edn);
	
	talloc_free(edn);
	return cdn;
}

char *ldb_dn_linearize_casefold(struct ldb_context *ldb, const struct ldb_dn *edn)
{
	struct ldb_dn *cdn;
	char *dn;

	if (edn == NULL) return NULL;

	/* Special DNs */
	if (ldb_dn_is_special(edn)) {
		dn = talloc_strdup(ldb, (char *)edn->components[0].value.data);
		return dn;
	}

	cdn = ldb_dn_casefold(ldb, edn);
	if (cdn == NULL) return NULL;

	dn = ldb_dn_linearize(ldb, cdn);
	if (dn == NULL) {
		talloc_free(cdn);
		return NULL;
	}

	talloc_free(cdn);
	return dn;
}

static struct ldb_dn_component ldb_dn_copy_component(void *mem_ctx, struct ldb_dn_component *src)
{
	struct ldb_dn_component dst;
	
	dst.name = NULL;

	if (src == NULL) {
		return dst;
	}

	dst.value = ldb_val_dup(mem_ctx, &(src->value));
	if (dst.value.data == NULL) {
		return dst;
	}

	dst.name = talloc_strdup(mem_ctx, src->name);
	if (dst.name == NULL) {
		talloc_free(dst.value.data);
	}

	return dst;
}

/* copy specified number of elements of a dn into a new one
   element are copied from top level up to the unique rdn
   num_el may be greater then dn->comp_num (see ldb_dn_make_child)
*/
struct ldb_dn *ldb_dn_copy_partial(void *mem_ctx, const struct ldb_dn *dn, int num_el)
{
	struct ldb_dn *new;
	int i, n, e;

	if (dn == NULL) return NULL;
	if (num_el <= 0) return NULL;

	new = ldb_dn_new(mem_ctx);
	LDB_DN_NULL_FAILED(new);

	new->comp_num = num_el;
	n = new->comp_num - 1;
	new->components = talloc_array(new, struct ldb_dn_component, new->comp_num);

	if (dn->comp_num == 0) return new;
	e = dn->comp_num - 1;

	for (i = 0; i < new->comp_num; i++) {
		new->components[n - i] = ldb_dn_copy_component(new->components,
								&(dn->components[e - i]));
		if ((e - i) == 0) {
			return new;
		}
	}

	return new;

failed:
	talloc_free(new);
	return NULL;
}

struct ldb_dn *ldb_dn_copy(void *mem_ctx, const struct ldb_dn *dn)
{
	if (dn == NULL) return NULL;
	return ldb_dn_copy_partial(mem_ctx, dn, dn->comp_num);
}

struct ldb_dn *ldb_dn_get_parent(void *mem_ctx, const struct ldb_dn *dn)
{
	if (dn == NULL) return NULL;
	return ldb_dn_copy_partial(mem_ctx, dn, dn->comp_num - 1);
}

struct ldb_dn_component *ldb_dn_build_component(void *mem_ctx, const char *attr,
						const char *val)
{
	struct ldb_dn_component *dc;

	if (attr == NULL || val == NULL) return NULL;

	dc = talloc(mem_ctx, struct ldb_dn_component);
	if (dc == NULL) return NULL;

	dc->name = talloc_strdup(dc, attr);
	if (dc->name ==  NULL) {
		talloc_free(dc);
		return NULL;
	}

	dc->value.data = (uint8_t *)talloc_strdup(dc, val);
	if (dc->value.data ==  NULL) {
		talloc_free(dc);
		return NULL;
	}

	dc->value.length = strlen(val);

	return dc;
}

struct ldb_dn *ldb_dn_build_child(void *mem_ctx, const char *attr,
						 const char * value,
						 const struct ldb_dn *base)
{
	struct ldb_dn *new;
	if (! ldb_dn_is_valid_attribute_name(attr)) return NULL;
	if (value == NULL || value == '\0') return NULL; 

	if (base != NULL) {
		new = ldb_dn_copy_partial(mem_ctx, base, base->comp_num + 1);
		LDB_DN_NULL_FAILED(new);
	} else {
		new = ldb_dn_new(mem_ctx);
		LDB_DN_NULL_FAILED(new);

		new->comp_num = 1;
		new->components = talloc_array(new, struct ldb_dn_component, new->comp_num);
	}

	new->components[0].name = talloc_strdup(new->components, attr);
	LDB_DN_NULL_FAILED(new->components[0].name);

	new->components[0].value.data = (uint8_t *)talloc_strdup(new->components, value);
	LDB_DN_NULL_FAILED(new->components[0].value.data);
	new->components[0].value.length = strlen((char *)new->components[0].value.data);

	return new;

failed:
	talloc_free(new);
	return NULL;

}

struct ldb_dn *ldb_dn_make_child(void *mem_ctx, const struct ldb_dn_component *component,
						const struct ldb_dn *base)
{
	if (component == NULL) return NULL;

	return ldb_dn_build_child(mem_ctx, component->name, 
				  (char *)component->value.data, base);
}

struct ldb_dn *ldb_dn_compose(void *mem_ctx, const struct ldb_dn *dn1, const struct ldb_dn *dn2)
{
	int i;
	struct ldb_dn *new;

	if (dn2 == NULL && dn1 == NULL) {
		return NULL;
	}

	if (dn2 == NULL) {
		new = ldb_dn_new(mem_ctx);
		LDB_DN_NULL_FAILED(new);

		new->comp_num = dn1->comp_num;
		new->components = talloc_array(new, struct ldb_dn_component, new->comp_num);
	} else {
		int comp_num = dn2->comp_num;
		if (dn1 != NULL) comp_num += dn1->comp_num;
		new = ldb_dn_copy_partial(mem_ctx, dn2, comp_num);
	}

	if (dn1 == NULL) {
		return new;
	}

	for (i = 0; i < dn1->comp_num; i++) {
		new->components[i] = ldb_dn_copy_component(new->components,
							   &(dn1->components[i]));
	}

	return new;

failed:
	talloc_free(new);
	return NULL;
}

struct ldb_dn *ldb_dn_string_compose(void *mem_ctx, const struct ldb_dn *base, const char *child_fmt, ...)
{
	struct ldb_dn *dn;
	char *child_str;
	va_list ap;
	int ret;
	
	if (child_fmt == NULL) return NULL;

	va_start(ap, child_fmt);
	ret = vasprintf(&child_str, child_fmt, ap);
	va_end(ap);

	if (ret <= 0) return NULL;

	dn = ldb_dn_compose(mem_ctx, ldb_dn_explode(mem_ctx, child_str), base);

	free(child_str);

	return dn;
}

struct ldb_dn_component *ldb_dn_get_rdn(void *mem_ctx, const struct ldb_dn *dn)
{
	struct ldb_dn_component *rdn;

	if (dn == NULL) return NULL;

	if (dn->comp_num < 1) {
		return NULL;
	}

	rdn = talloc(mem_ctx, struct ldb_dn_component);
	if (rdn == NULL) return NULL;

	*rdn = ldb_dn_copy_component(mem_ctx, &dn->components[0]);
	if (rdn->name == NULL) {
		talloc_free(rdn);
		return NULL;
	}

	return rdn;
}

