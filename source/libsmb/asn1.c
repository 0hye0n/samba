/* 
   Unix SMB/CIFS implementation.
   simple SPNEGO routines
   Copyright (C) Andrew Tridgell 2001
   
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

/* free an asn1 structure */
void asn1_free(ASN1_DATA *data)
{
	SAFE_FREE(data->data);
}

/* write to the ASN1 buffer, advancing the buffer pointer */
BOOL asn1_write(ASN1_DATA *data, const void *p, int len)
{
	if (data->has_error) return False;
	if (data->length < data->ofs+len) {
		uint8 *newp;
		newp = Realloc(data->data, data->ofs+len);
		if (!newp) {
			SAFE_FREE(data->data);
			data->has_error = True;
			return False;
		}
		data->data = newp;
		data->length = data->ofs+len;
	}
	memcpy(data->data + data->ofs, p, len);
	data->ofs += len;
	return True;
}

/* useful fn for writing a uint8 */
BOOL asn1_write_uint8(ASN1_DATA *data, uint8 v)
{
	return asn1_write(data, &v, 1);
}

/* push a tag onto the asn1 data buffer. Used for nested structures */
BOOL asn1_push_tag(ASN1_DATA *data, uint8 tag)
{
	struct nesting *nesting;

	asn1_write_uint8(data, tag);
	nesting = (struct nesting *)malloc(sizeof(struct nesting));
	if (!nesting) {
		data->has_error = True;
		return False;
	}

	nesting->start = data->ofs;
	nesting->next = data->nesting;
	data->nesting = nesting;
	return asn1_write_uint8(data, 0xff);
}

/* pop a tag */
BOOL asn1_pop_tag(ASN1_DATA *data)
{
	struct nesting *nesting;
	size_t len;

	nesting = data->nesting;

	if (!nesting) {
		data->has_error = True;
		return False;
	}
	len = data->ofs - (nesting->start+1);
	/* yes, this is ugly. We don't know in advance how many bytes the length
	   of a tag will take, so we assumed 1 byte. If we were wrong then we 
	   need to correct our mistake */
	if (len > 255) {
		data->data[nesting->start] = 0x82;
		if (!asn1_write_uint8(data, 0)) return False;
		if (!asn1_write_uint8(data, 0)) return False;
		memmove(data->data+nesting->start+3, data->data+nesting->start+1, len);
		data->data[nesting->start+1] = len>>8;
		data->data[nesting->start+2] = len&0xff;
	} else if (len > 127) {
		data->data[nesting->start] = 0x81;
		if (!asn1_write_uint8(data, 0)) return False;
		memmove(data->data+nesting->start+2, data->data+nesting->start+1, len);
		data->data[nesting->start+1] = len;
	} else {
		data->data[nesting->start] = len;
	}

	data->nesting = nesting->next;
	free(nesting);
	return True;
}

/* "i" is the one's complement representation, as is the normal result of an
 * implicit signed->unsigned conversion */

static void push_int_bigendian(ASN1_DATA *data, unsigned int i, BOOL negative)
{
	uint8 lowest = i & 0xFF;

	i = i >> 8;
	if (i != 0)
		push_int_bigendian(data, i, negative);

	if (data->nesting->start+1 == data->ofs) {

		/* We did not write anything yet, looking at the highest
		 * valued byte */

		if (negative) {
			/* Don't write leading 0xff's */
			if (lowest == 0xFF)
				return;

			if ((lowest & 0x80) == 0) {
				/* The only exception for a leading 0xff is if
				 * the highest bit is 0, which would indicate
				 * a positive value */
				asn1_write_uint8(data, 0xff);
			}
		} else {
			if (lowest & 0x80) {
				/* The highest bit of a positive integer is 1,
				 * this would indicate a negative number. Push
				 * a 0 to indicate a positive one */
				asn1_write_uint8(data, 0);
			}
		}
	}

	asn1_write_uint8(data, lowest);
}

/* write an integer */
BOOL asn1_write_Integer(ASN1_DATA *data, int i)
{
	if (!asn1_push_tag(data, ASN1_INTEGER)) return False;
	if (i == -1) {
		/* -1 is special as it consists of all-0xff bytes. In
                    push_int_bigendian this is the only case that is not
                    properly handled, as all 0xff bytes would be handled as
                    leading ones to be ignored. */
		asn1_write_uint8(data, 0xff);
	} else {
		push_int_bigendian(data, i, i<0);
	}
	return asn1_pop_tag(data);
}

/* write an object ID to a ASN1 buffer */
BOOL asn1_write_OID(ASN1_DATA *data, const char *OID)
{
	unsigned v, v2;
	const char *p = (const char *)OID;
	char *newp;

	if (!asn1_push_tag(data, ASN1_OID))
		return False;
	v = strtol(p, &newp, 10);
	p = newp;
	v2 = strtol(p, &newp, 10);
	p = newp;
	if (!asn1_write_uint8(data, 40*v + v2))
		return False;

	while (*p) {
		v = strtol(p, &newp, 10);
		p = newp;
		if (v >= (1<<28)) asn1_write_uint8(data, 0x80 | ((v>>28)&0xff));
		if (v >= (1<<21)) asn1_write_uint8(data, 0x80 | ((v>>21)&0xff));
		if (v >= (1<<14)) asn1_write_uint8(data, 0x80 | ((v>>14)&0xff));
		if (v >= (1<<7)) asn1_write_uint8(data, 0x80 | ((v>>7)&0xff));
		if (!asn1_write_uint8(data, v&0x7f))
			return False;
	}
	return asn1_pop_tag(data);
}

/* write an octet string */
BOOL asn1_write_OctetString(ASN1_DATA *data, const void *p, size_t length)
{
	asn1_push_tag(data, ASN1_OCTET_STRING);
	asn1_write(data, p, length);
	asn1_pop_tag(data);
	return !data->has_error;
}

/* write a general string */
BOOL asn1_write_GeneralString(ASN1_DATA *data, const char *s)
{
	asn1_push_tag(data, ASN1_GENERAL_STRING);
	asn1_write(data, s, strlen(s));
	asn1_pop_tag(data);
	return !data->has_error;
}

BOOL asn1_write_ContextSimple(ASN1_DATA *data, uint8 num, DATA_BLOB *blob)
{
	asn1_push_tag(data, ASN1_CONTEXT_SIMPLE(num));
	asn1_write(data, blob->data, blob->length);
	asn1_pop_tag(data);
	return !data->has_error;
}

/* write a BOOLEAN */
BOOL asn1_write_BOOLEAN(ASN1_DATA *data, BOOL v)
{
	asn1_push_tag(data, ASN1_BOOLEAN);
	asn1_write_uint8(data, v ? 0xFF : 0);
	asn1_pop_tag(data);
	return !data->has_error;
}

BOOL asn1_read_BOOLEAN(ASN1_DATA *data, BOOL *v)
{
	asn1_start_tag(data, ASN1_BOOLEAN);
	asn1_read_uint8(data, (uint8 *)v);
	asn1_end_tag(data);
	return !data->has_error;
}

/* check a BOOLEAN */
BOOL asn1_check_BOOLEAN(ASN1_DATA *data, BOOL v)
{
	uint8 b = 0;

	asn1_read_uint8(data, &b);
	if (b != ASN1_BOOLEAN) {
		data->has_error = True;
		return False;
	}
	asn1_read_uint8(data, &b);
	if (b != v) {
		data->has_error = True;
		return False;
	}
	return !data->has_error;
}


/* load a ASN1_DATA structure with a lump of data, ready to be parsed */
BOOL asn1_load(ASN1_DATA *data, DATA_BLOB blob)
{
	ZERO_STRUCTP(data);
	data->data = memdup(blob.data, blob.length);
	if (!data->data) {
		data->has_error = True;
		return False;
	}
	data->length = blob.length;
	return True;
}

/* Peek into an ASN1 buffer, not advancing the pointer */
BOOL asn1_peek(ASN1_DATA *data, void *p, int len)
{
	if (len < 0 || data->ofs + len < data->ofs || data->ofs + len < len)
		return False;

	if (data->ofs + len > data->length)
		return False;

	memcpy(p, data->data + data->ofs, len);
	return True;
}

/* read from a ASN1 buffer, advancing the buffer pointer */
BOOL asn1_read(ASN1_DATA *data, void *p, int len)
{
	if (!asn1_peek(data, p, len)) {
		data->has_error = True;
		return False;
	}

	data->ofs += len;
	return True;
}

/* read a uint8 from a ASN1 buffer */
BOOL asn1_read_uint8(ASN1_DATA *data, uint8 *v)
{
	return asn1_read(data, v, 1);
}

BOOL asn1_peek_uint8(ASN1_DATA *data, uint8 *v)
{
	return asn1_peek(data, v, 1);
}

BOOL asn1_peek_tag(ASN1_DATA *data, uint8 tag)
{
	uint8 b;

	if (asn1_tag_remaining(data) <= 0) {
		return False;
	}

	if (!asn1_peek(data, &b, sizeof(b)))
		return False;

	return (b == tag);
}

/* start reading a nested asn1 structure */
BOOL asn1_start_tag(ASN1_DATA *data, uint8 tag)
{
	uint8 b;
	struct nesting *nesting;
	
	if (!asn1_read_uint8(data, &b))
		return False;

	if (b != tag) {
		data->has_error = True;
		return False;
	}
	nesting = (struct nesting *)malloc(sizeof(struct nesting));
	if (!nesting) {
		data->has_error = True;
		return False;
	}

	if (!asn1_read_uint8(data, &b)) {
		return False;
	}

	if (b & 0x80) {
		int n = b & 0x7f;
		if (!asn1_read_uint8(data, &b))
			return False;
		nesting->taglen = b;
		while (n > 1) {
			if (!asn1_read_uint8(data, &b)) 
				return False;
			nesting->taglen = (nesting->taglen << 8) | b;
			n--;
		}
	} else {
		nesting->taglen = b;
	}
	nesting->start = data->ofs;
	nesting->next = data->nesting;
	data->nesting = nesting;
	return !data->has_error;
}

static BOOL read_one_uint8(int sock, uint8 *result, ASN1_DATA *data,
			   const struct timeval *endtime)
{
	if (read_data_until(sock, result, 1, endtime) != 1)
		return False;

	return asn1_write(data, result, 1);
}

/* Read a complete ASN sequence (ie LDAP result) from a socket */
BOOL asn1_read_sequence_until(int sock, ASN1_DATA *data,
			      const struct timeval *endtime)
{
	uint8 b;
	size_t len;
	char *buf;

	ZERO_STRUCTP(data);

	if (!read_one_uint8(sock, &b, data, endtime))
		return False;

	if (b != 0x30) {
		data->has_error = True;
		return False;
	}

	if (!read_one_uint8(sock, &b, data, endtime))
		return False;

	if (b & 0x80) {
		int n = b & 0x7f;
		if (!read_one_uint8(sock, &b, data, endtime))
			return False;
		len = b;
		while (n > 1) {
			if (!read_one_uint8(sock, &b, data, endtime))
				return False;
			len = (len<<8) | b;
			n--;
		}
	} else {
		len = b;
	}

	buf = malloc(len);
	if (buf == NULL)
		return False;

	if (read_data_until(sock, buf, len, endtime) != len)
		return False;

	if (!asn1_write(data, buf, len))
		return False;

	free(buf);

	data->ofs = 0;
	
	return True;
}

/* Get the length to be expected in buf */
BOOL asn1_object_length(uint8_t *buf, size_t buf_length,
			uint8 tag, size_t *result)
{
	ASN1_DATA data;

	/* Fake the asn1_load to avoid the memdup, this is just to be able to
	 * re-use the length-reading in asn1_start_tag */
	ZERO_STRUCT(data);
	data.data = buf;
	data.length = buf_length;
	if (!asn1_start_tag(&data, tag))
		return False;
	*result = asn1_tag_remaining(&data)+data.ofs;
	/* We can't use asn1_end_tag here, as we did not consume the complete
	 * tag, so asn1_end_tag would flag an error and not free nesting */
	free(data.nesting);
	return True;
}

/* stop reading a tag */
BOOL asn1_end_tag(ASN1_DATA *data)
{
	struct nesting *nesting;

	/* make sure we read it all */
	if (asn1_tag_remaining(data) != 0) {
		data->has_error = True;
		return False;
	}

	nesting = data->nesting;

	if (!nesting) {
		data->has_error = True;
		return False;
	}

	data->nesting = nesting->next;
	free(nesting);
	return True;
}

/* work out how many bytes are left in this nested tag */
int asn1_tag_remaining(ASN1_DATA *data)
{
	if (!data->nesting) {
		data->has_error = True;
		return -1;
	}
	return data->nesting->taglen - (data->ofs - data->nesting->start);
}

/* read an object ID from a ASN1 buffer */
BOOL asn1_read_OID(ASN1_DATA *data, char **OID)
{
	uint8 b;
	pstring oid;
	fstring el;

	if (!asn1_start_tag(data, ASN1_OID)) return False;
	asn1_read_uint8(data, &b);

	oid[0] = 0;
	fstr_sprintf(el, "%u",  b/40);
	pstrcat(oid, el);
	fstr_sprintf(el, " %u",  b%40);
	pstrcat(oid, el);

	while (!data->has_error && asn1_tag_remaining(data) > 0) {
		unsigned v = 0;
		do {
			asn1_read_uint8(data, &b);
			v = (v<<7) | (b&0x7f);
		} while (!data->has_error && b & 0x80);
		fstr_sprintf(el, " %u",  v);
		pstrcat(oid, el);
	}

	asn1_end_tag(data);

	*OID = strdup(oid);

	return (*OID && !data->has_error);
}

/* check that the next object ID is correct */
BOOL asn1_check_OID(ASN1_DATA *data, const char *OID)
{
	char *id;

	if (!asn1_read_OID(data, &id)) return False;

	if (strcmp(id, OID) != 0) {
		data->has_error = True;
		return False;
	}
	free(id);
	return True;
}

/* read a GeneralString from a ASN1 buffer */
BOOL asn1_read_GeneralString(ASN1_DATA *data, char **s)
{
	int len;
	if (!asn1_start_tag(data, ASN1_GENERAL_STRING)) return False;
	len = asn1_tag_remaining(data);
	if (len < 0) {
		data->has_error = True;
		return False;
	}
	*s = malloc(len+1);
	if (! *s) {
		data->has_error = True;
		return False;
	}
	asn1_read(data, *s, len);
	(*s)[len] = 0;
	asn1_end_tag(data);
	return !data->has_error;
}

/* read a octet string blob */
BOOL asn1_read_OctetString(ASN1_DATA *data, DATA_BLOB *blob)
{
	int len;
	ZERO_STRUCTP(blob);
	if (!asn1_start_tag(data, ASN1_OCTET_STRING)) return False;
	len = asn1_tag_remaining(data);
	if (len < 0) {
		data->has_error = True;
		return False;
	}
	*blob = data_blob(NULL, len);
	asn1_read(data, blob->data, len);
	asn1_end_tag(data);
	return !data->has_error;
}

BOOL asn1_read_ContextSimple(ASN1_DATA *data, uint8 num, DATA_BLOB *blob)
{
	int len;
	ZERO_STRUCTP(blob);
	if (!asn1_start_tag(data, ASN1_CONTEXT_SIMPLE(num))) return False;
	len = asn1_tag_remaining(data);
	if (len < 0) {
		data->has_error = True;
		return False;
	}
	*blob = data_blob(NULL, len);
	asn1_read(data, blob->data, len);
	asn1_end_tag(data);
	return !data->has_error;
}

/* read an interger */
BOOL asn1_read_Integer(ASN1_DATA *data, int *i)
{
	uint8 b;
	*i = 0;
	
	if (!asn1_start_tag(data, ASN1_INTEGER)) return False;
	if (!asn1_peek_uint8(data, &b)) return False;
	if (b & 0x80)
		*i = -1;
	while (asn1_tag_remaining(data)>0) {
		asn1_read_uint8(data, &b);
		*i = (*i << 8) + b;
	}
	return asn1_end_tag(data);	
	
}

/* read an interger */
BOOL asn1_read_enumerated(ASN1_DATA *data, int *v)
{
	*v = 0;
	
	if (!asn1_start_tag(data, ASN1_ENUMERATED)) return False;
	while (asn1_tag_remaining(data)>0) {
		uint8 b;
		asn1_read_uint8(data, &b);
		*v = (*v << 8) + b;
	}
	return asn1_end_tag(data);	
}

/* check a enumarted value is correct */
BOOL asn1_check_enumerated(ASN1_DATA *data, int v)
{
	uint8 b;
	if (!asn1_start_tag(data, ASN1_ENUMERATED)) return False;
	asn1_read_uint8(data, &b);
	asn1_end_tag(data);

	if (v != b)
		data->has_error = False;

	return !data->has_error;
}

/* write an enumarted value to the stream */
BOOL asn1_write_enumerated(ASN1_DATA *data, uint8 v)
{
	if (!asn1_push_tag(data, ASN1_ENUMERATED)) return False;
	asn1_write_uint8(data, v);
	asn1_pop_tag(data);
	return !data->has_error;
}
