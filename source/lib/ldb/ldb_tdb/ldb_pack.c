/* 
   ldb database library

   Copyright (C) Andrew Tridgell  2004

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
 *  Component: ldb pack/unpack
 *
 *  Description: pack/unpack routines for ldb messages as key/value blobs
 *
 *  Author: Andrew Tridgell
 */

#include "includes.h"
#include "ldb_tdb.h"

/* change this if the data format ever changes */
#define LTDB_PACKING_FORMAT 0x26011966

/*
  pack a ldb message into a linear buffer in a TDB_DATA

  note that this routine avoids saving elements with zero values,
  as these are equivalent to having no element

  caller frees the data buffer after use
*/
int ltdb_pack_data(struct ldb_context *ctx,
		   const struct ldb_message *message,
		   struct TDB_DATA *data)
{
	int i, j;
	size_t size;
	char *p;

	/* work out how big it needs to be */
	size = 8;

	for (i=0;i<message->num_elements;i++) {
		if (message->elements[i].num_values == 0) {
			continue;
		}
		size += 1 + strlen(message->elements[i].name) + 4;
		for (j=0;j<message->elements[i].num_values;j++) {
			size += 4 + message->elements[i].values[j].length + 1;
		}
	}

	/* allocate it */
	data->dptr = malloc(size);
	if (!data->dptr) {
		errno = ENOMEM;
		return -1;
	}
	data->dsize = size;

	p = data->dptr;
	SIVAL(p, 0, LTDB_PACKING_FORMAT); 
	SIVAL(p, 4, message->num_elements); 
	p += 8;
	
	for (i=0;i<message->num_elements;i++) {
		size_t len;
		if (message->elements[i].num_values == 0) {
			continue;
		}
		len = strlen(message->elements[i].name);
		memcpy(p, message->elements[i].name, len+1);
		p += len + 1;
		SIVAL(p, 0, message->elements[i].num_values);
		p += 4;
		for (j=0;j<message->elements[i].num_values;j++) {
			SIVAL(p, 0, message->elements[i].values[j].length);
			memcpy(p+4, message->elements[i].values[j].data, 
			       message->elements[i].values[j].length);
			p[4+message->elements[i].values[j].length] = 0;
			p += 4 + message->elements[i].values[j].length + 1;
		}
	}

	return 0;
}

/*
  free the memory allocated from a ltdb_unpack_data()
*/
void ltdb_unpack_data_free(struct ldb_message *message)
{
	int i;

	for (i=0;i<message->num_elements;i++) {
		if (message->elements[i].values) free(message->elements[i].values);
	}
	if (message->elements) free(message->elements);
}


/*
  unpack a ldb message from a linear buffer in TDB_DATA

  note that this does not fill in the class and key elements

  caller frees. Memory for the elements[] and values[] arrays are
  malloced, but the memory for the elements is re-used from the
  TDB_DATA data. This means the caller only has to free the elements
  and values arrays. This can be done with ltdb_unpack_data_free()
*/
int ltdb_unpack_data(struct ldb_context *ctx,
		     const struct TDB_DATA *data,
		     struct ldb_message *message)
{
	char *p;
	unsigned int remaining;
	int i, j;

	message->elements = NULL;

	p = data->dptr;
	if (data->dsize < 4) {
		errno = EIO;
		goto failed;
	}

	if (IVAL(p, 0) != LTDB_PACKING_FORMAT) {
		/* this is where we will cope with upgrading the
		   format if/when the format is ever changed */
		errno = EIO;
		goto failed;
	}

	message->num_elements = IVAL(p, 4);
	p += 8;

	if (message->num_elements == 0) {
		message->elements = NULL;
		return 0;
	}
	
	/* basic sanity check */
	remaining = data->dsize - 8;

	if (message->num_elements > remaining / 6) {
		errno = EIO;
		goto failed;
	}

	message->elements = malloc_array_p(struct ldb_message_element,
					   message->num_elements);
				     
	if (!message->elements) {
		errno = ENOMEM;
		goto failed;
	}

	for (i=0;i<message->num_elements;i++) {
		size_t len;
		if (remaining < 10) {
			errno = EIO;
			goto failed;
		}
		len = strnlen(p, remaining-6);
		message->elements[i].flags = 0;
		message->elements[i].name = p;
		remaining -= len + 1;
		p += len + 1;
		message->elements[i].num_values = IVAL(p, 0);
		message->elements[i].values = NULL;
		if (message->elements[i].num_values != 0) {
			message->elements[i].values = malloc_array_p(struct ldb_val, 
								     message->elements[i].num_values);
			if (!message->elements[i].values) {
				errno = ENOMEM;
				goto failed;
			}
		}
		p += 4;
		for (j=0;j<message->elements[i].num_values;j++) {
			len = IVAL(p, 0);
			if (len > remaining-5) {
				errno = EIO;
				goto failed;
			}

			message->elements[i].values[j].length = len;
			message->elements[i].values[j].data = p+4;
			remaining -= len+4+1;
			p += len+4+1;
		}
	}

	return 0;

failed:
	ltdb_unpack_data_free(message);

	return -1;
}
