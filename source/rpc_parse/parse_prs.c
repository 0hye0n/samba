/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Samba memory buffer functions
   Copyright (C) Andrew Tridgell              1992-1997
   Copyright (C) Luke Kenneth Casson Leighton 1996-1997
   Copyright (C) Jeremy Allison 1999.
   
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

extern int DEBUGLEVEL;

#include "includes.h"


/*******************************************************************
dump a prs to a file
 ********************************************************************/
void prs_dump(char *name, int v, prs_struct *ps)
{
	int fd, i;
	pstring fname;
	if (DEBUGLEVEL < 50) return;
	for (i=1;i<100;i++) {
		if (v != -1) {
			slprintf(fname,sizeof(fname), "/tmp/%s_%d.%d.prs", name, v, i);
		} else {
			slprintf(fname,sizeof(fname), "/tmp/%s.%d.prs", name, i);
		}
		fd = open(fname, O_WRONLY|O_CREAT|O_EXCL, 0644);
		if (fd != -1 || errno != EEXIST) break;
	}
	if (fd != -1) {
		write(fd, ps->data_p + ps->data_offset, ps->buffer_size - ps->data_offset);
		close(fd);
		DEBUG(0,("created %s\n", fname));
	}
}



/*******************************************************************
 debug output for parsing info.

 XXXX side-effect of this function is to increase the debug depth XXXX

 ********************************************************************/
void prs_debug(prs_struct *ps, int depth, char *desc, char *fn_name)
{
	DEBUG(5+depth, ("%s%06x %s %s\n", tab_depth(depth), ps->data_offset, fn_name, desc));
}


/*******************************************************************
 Initialise a parse structure - malloc the data if requested.
 ********************************************************************/
BOOL prs_init(prs_struct *ps, uint32 size, uint8 align, TALLOC_CTX *ctx, BOOL io)
{
	ZERO_STRUCTP(ps);
	ps->io = io;
	ps->bigendian_data = False;
	ps->align = align;
	ps->is_dynamic = False;
	ps->data_offset = 0;
	ps->buffer_size = 0;
	ps->data_p = NULL;
	ps->mem_ctx = ctx;

	if (size != 0) {
		ps->buffer_size = size;
		if((ps->data_p = (char *)malloc((size_t)size)) == NULL) {
			DEBUG(0,("prs_init: malloc fail for %u bytes.\n", (unsigned int)size));
			return False;
		}
		ps->is_dynamic = True; /* We own this memory. */
	}

	return True;
}

/*******************************************************************
 read from a socket into memory.
 ********************************************************************/
BOOL prs_read(prs_struct *ps, int fd, size_t len, int timeout)
{
	BOOL ok;
	size_t prev_size = ps->buffer_size;
	if (!prs_grow(ps, len))
		return False;

	if (timeout > 0) {
		ok = (read_with_timeout(fd, &ps->data_p[prev_size],
		                            len, len,timeout) == len);
	} else {
		ok = (read_data(fd, &ps->data_p[prev_size], len) == len);
	}
	return ok;
}

/*******************************************************************
 Delete the memory in a parse structure - if we own it.
 ********************************************************************/

void prs_mem_free(prs_struct *ps)
{
	if(ps->is_dynamic && (ps->data_p != NULL))
		free(ps->data_p);
	ps->is_dynamic = False;
	ps->data_p = NULL;
	ps->buffer_size = 0;
	ps->data_offset = 0;
}

/*******************************************************************
 Allocate memory when unmarshalling...
 ********************************************************************/

char *prs_alloc_mem(prs_struct *ps, size_t size)
{
	return talloc(ps->mem_ctx, size);
}

/*******************************************************************
 Return the current talloc context we're using.
 ********************************************************************/

TALLOC_CTX *prs_get_mem_context(prs_struct *ps)
{
	return ps->mem_ctx;
}

/*******************************************************************
 Hand some already allocated memory to a prs_struct.
 ********************************************************************/

void prs_give_memory(prs_struct *ps, char *buf, uint32 size, BOOL is_dynamic)
{
	ps->is_dynamic = is_dynamic;
	ps->data_p = buf;
	ps->buffer_size = size;
}

/*******************************************************************
 Take some memory back from a prs_struct.
 ********************************************************************/

char *prs_take_memory(prs_struct *ps, uint32 *psize)
{
	char *ret = ps->data_p;
	if(psize)
		*psize = ps->buffer_size;
	ps->is_dynamic = False;
	prs_mem_free(ps);
	return ret;
}

/*******************************************************************
 Set a prs_struct to exactly a given size. Will grow or tuncate if neccessary.
 ********************************************************************/

BOOL prs_set_buffer_size(prs_struct *ps, uint32 newsize)
{
	if (newsize > ps->buffer_size)
		return prs_force_grow(ps, newsize - ps->buffer_size);

	if (newsize < ps->buffer_size) {
		char *new_data_p = Realloc(ps->data_p, newsize);
		/* if newsize is zero, Realloc acts like free() & returns NULL*/
		if (new_data_p == NULL && newsize != 0) {
			DEBUG(0,("prs_set_buffer_size: Realloc failure for size %u.\n",
				(unsigned int)newsize));
			DEBUG(0,("prs_set_buffer_size: Reason %s\n",strerror(errno)));
			return False;
		}
		ps->data_p = new_data_p;
		ps->buffer_size = newsize;
	}

	return True;
}

/*******************************************************************
 Attempt, if needed, to grow a data buffer.
 Also depends on the data stream mode (io).
 ********************************************************************/

BOOL prs_grow(prs_struct *ps, uint32 extra_space)
{
	uint32 new_size;
	char *new_data;

	ps->grow_size = MAX(ps->grow_size, ps->data_offset + extra_space);

	if(ps->data_offset + extra_space <= ps->buffer_size)
		return True;

	/*
	 * We cannot grow the buffer if we're not reading
	 * into the prs_struct, or if we don't own the memory.
	 */

	if(UNMARSHALLING(ps) || !ps->is_dynamic) {
		DEBUG(0,("prs_grow: Buffer overflow - unable to expand buffer by %u bytes.\n",
				(unsigned int)extra_space));
		return False;
	}
	
	/*
	 * Decide how much extra space we really need.
	 */

	extra_space -= (ps->buffer_size - ps->data_offset);
	if(ps->buffer_size == 0) {
		/*
		 * Ensure we have at least a PDU's length, or extra_space, whichever
		 * is greater.
		 */

		new_size = MAX(MAX_PDU_FRAG_LEN,extra_space);

		if((new_data = malloc(new_size)) == NULL) {
			DEBUG(0,("prs_grow: Malloc failure for size %u.\n", (unsigned int)new_size));
			return False;
		}
		memset(new_data, '\0', new_size );
	} else {
		/*
		 * If the current buffer size is bigger than the space needed, just 
		 * double it, else add extra_space.
		 */
		new_size = MAX(ps->buffer_size*2, ps->buffer_size + extra_space);		

		if ((new_data = Realloc(ps->data_p, new_size)) == NULL) {
			DEBUG(0,("prs_grow: Realloc failure for size %u.\n",
				(unsigned int)new_size));
			return False;
		}

		memset(&new_data[ps->buffer_size], '\0', new_size - ps->buffer_size);
	}
	ps->buffer_size = new_size;
	ps->data_p = new_data;

	return True;
}

/*******************************************************************
 Attempt to force a data buffer to grow by len bytes.
 This is only used when appending more data onto a prs_struct
 when reading an rpc reply, before unmarshalling it.
 ********************************************************************/

BOOL prs_force_grow(prs_struct *ps, uint32 extra_space)
{
	uint32 new_size = ps->buffer_size + extra_space;
	char *new_data;

	if(!UNMARSHALLING(ps) || !ps->is_dynamic) {
		DEBUG(0,("prs_force_grow: Buffer overflow - unable to expand buffer by %u bytes.\n",
				(unsigned int)extra_space));
		return False;
	}

	if((new_data = Realloc(ps->data_p, new_size)) == NULL) {
		DEBUG(0,("prs_force_grow: Realloc failure for size %u.\n",
			(unsigned int)new_size));
		return False;
	}

	memset(&new_data[ps->buffer_size], '\0', new_size - ps->buffer_size);

	ps->buffer_size = new_size;
	ps->data_p = new_data;

	return True;
}

/*******************************************************************
 Get the data pointer (external interface).
 ********************************************************************/

char *prs_data_p(prs_struct *ps)
{
	return ps->data_p;
}

/*******************************************************************
 Get the current data size (external interface).
 ********************************************************************/

uint32 prs_data_size(prs_struct *ps)
{
	return ps->buffer_size;
}

/*******************************************************************
 Fetch the current offset (external interface).
 ********************************************************************/

uint32 prs_offset(prs_struct *ps)
{
	return ps->data_offset;
}

/*******************************************************************
 Set the current offset (external interface).
 ********************************************************************/

BOOL prs_set_offset(prs_struct *ps, uint32 offset)
{
	if(offset <= ps->data_offset) {
		ps->data_offset = offset;
		return True;
	}

	if(!prs_grow(ps, offset - ps->data_offset))
		return False;

	ps->data_offset = offset;
	return True;
}

/*******************************************************************
 Append the data from one parse_struct into another.
 ********************************************************************/

BOOL prs_append_prs_data(prs_struct *dst, prs_struct *src)
{
	if(!prs_grow(dst, prs_offset(src)))
		return False;

	memcpy(&dst->data_p[dst->data_offset], prs_data_p(src), (size_t)prs_offset(src));
	dst->data_offset += prs_offset(src);

	return True;
}

/*******************************************************************
 Append some data from one parse_struct into another.
 ********************************************************************/

BOOL prs_append_some_prs_data(prs_struct *dst, prs_struct *src, int32 start, uint32 len)
{	
	if (len == 0)
		return True;

	if(!prs_grow(dst, len))
		return False;
	
	memcpy(&dst->data_p[dst->data_offset], prs_data_p(src)+start, (size_t)len);
	dst->data_offset += len;

	return True;
}

/*******************************************************************
 Append the data from a buffer into a parse_struct.
 ********************************************************************/

BOOL prs_append_data(prs_struct *dst, char *src, uint32 len)
{
	if(!prs_grow(dst, len))
		return False;

	memcpy(&dst->data_p[dst->data_offset], src, (size_t)len);
	dst->data_offset += len;

	return True;
}

/*******************************************************************
 Set the data as big-endian (external interface).
 ********************************************************************/

void prs_set_bigendian_data(prs_struct *ps)
{
	ps->bigendian_data = True;
}

/*******************************************************************
 Align a the data_len to a multiple of align bytes - filling with
 zeros.
 ********************************************************************/

BOOL prs_align(prs_struct *ps)
{
	uint32 mod = ps->data_offset & (ps->align-1);

	if (ps->align != 0 && mod != 0) {
		uint32 extra_space = (ps->align - mod);
		if(!prs_grow(ps, extra_space))
			return False;
		memset(&ps->data_p[ps->data_offset], '\0', (size_t)extra_space);
		ps->data_offset += extra_space;
	}

	return True;
}

/*******************************************************************
 Align only if required (for the unistr2 string mainly)
 ********************************************************************/

BOOL prs_align_needed(prs_struct *ps, uint32 needed)
{
	if (needed==0)
		return True;
	else
		return prs_align(ps);
}

/*******************************************************************
 Ensure we can read/write to a given offset.
 ********************************************************************/

char *prs_mem_get(prs_struct *ps, uint32 extra_size)
{
	if(UNMARSHALLING(ps)) {
		/*
		 * If reading, ensure that we can read the requested size item.
		 */
		if (ps->data_offset + extra_size > ps->buffer_size) {
			DEBUG(0,("prs_mem_get: reading data of size %u would overrun buffer.\n",
					(unsigned int)extra_size ));
			return NULL;
		}
	} else {
		/*
		 * Writing - grow the buffer if needed.
		 */
		if(!prs_grow(ps, extra_size))
			return False;
	}
	return &ps->data_p[ps->data_offset];
}

/*******************************************************************
 Change the struct type.
 ********************************************************************/

void prs_switch_type(prs_struct *ps, BOOL io)
{
	if ((ps->io ^ io) == True)
		ps->io=io;
}

/*******************************************************************
 Force a prs_struct to be dynamic even when it's size is 0.
 ********************************************************************/

void prs_force_dynamic(prs_struct *ps)
{
	ps->is_dynamic=True;
}

/*******************************************************************
 Stream a uint8.
 ********************************************************************/

BOOL prs_uint8(char *name, prs_struct *ps, int depth, uint8 *data8)
{
	char *q = prs_mem_get(ps, sizeof(uint8));
	if (q == NULL)
		return False;

	DBG_RW_CVAL(name, depth, ps->data_offset, ps->io, q, *data8)
	ps->data_offset += sizeof(uint8);

	return True;
}

/*******************************************************************
 Stream a uint16.
 ********************************************************************/

BOOL prs_uint16(char *name, prs_struct *ps, int depth, uint16 *data16)
{
	char *q = prs_mem_get(ps, sizeof(uint16));
	if (q == NULL)
		return False;

	DBG_RW_SVAL(name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, *data16)
	ps->data_offset += sizeof(uint16);

	return True;
}

/*******************************************************************
 Stream a uint32.
 ********************************************************************/

BOOL prs_uint32(char *name, prs_struct *ps, int depth, uint32 *data32)
{
	char *q = prs_mem_get(ps, sizeof(uint32));
	if (q == NULL)
		return False;

	DBG_RW_IVAL(name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, *data32)
	ps->data_offset += sizeof(uint32);

	return True;
}

/******************************************************************
 Stream an array of uint8s. Length is number of uint8s.
 ********************************************************************/

BOOL prs_uint8s(BOOL charmode, char *name, prs_struct *ps, int depth, uint8 *data8s, int len)
{
	char *q = prs_mem_get(ps, len * sizeof(uint8));
	if (q == NULL)
		return False;

	DBG_RW_PCVAL(charmode, name, depth, ps->data_offset, ps->io, q, data8s, len)
	ps->data_offset += (len * sizeof(uint8));

	return True;
}

/******************************************************************
 Stream an array of uint16s. Length is number of uint16s.
 ********************************************************************/

BOOL prs_uint16s(BOOL charmode, char *name, prs_struct *ps, int depth, uint16 *data16s, int len)
{
	char *q = prs_mem_get(ps, len * sizeof(uint16));
	if (q == NULL)
		return False;

	DBG_RW_PSVAL(charmode, name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, data16s, len)
	ps->data_offset += (len * sizeof(uint16));

	return True;
}

/******************************************************************
 Stream an array of uint32s. Length is number of uint32s.
 ********************************************************************/

BOOL prs_uint32s(BOOL charmode, char *name, prs_struct *ps, int depth, uint32 *data32s, int len)
{
	char *q = prs_mem_get(ps, len * sizeof(uint32));
	if (q == NULL)
		return False;

	DBG_RW_PIVAL(charmode, name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, data32s, len)
	ps->data_offset += (len * sizeof(uint32));

	return True;
}

/******************************************************************
 Stream a "not" unicode string, length/buffer specified separately,
 in byte chars. String is in little-endian format.
 ********************************************************************/

BOOL prs_buffer2(BOOL charmode, char *name, prs_struct *ps, int depth, BUFFER2 *str)
{
	char *p;
	char *q = prs_mem_get(ps, str->buf_len);
	if (q == NULL)
		return False;

	if (UNMARSHALLING(ps)) {
		str->buffer = (uint16 *)prs_alloc_mem(ps,str->buf_len);
		if (str->buffer == NULL)
			return False;
	}

	p = (char *)str->buffer;

	/* If we're using big-endian, reverse to get little-endian. */
	if(ps->bigendian_data)
		DBG_RW_PSVAL(charmode, name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, p, str->buf_len/2)
	else
		DBG_RW_PCVAL(charmode, name, depth, ps->data_offset, ps->io, q, p, str->buf_len)
	ps->data_offset += str->buf_len;

	return True;
}

/******************************************************************
 Stream a string, length/buffer specified separately,
 in uint8 chars.
 ********************************************************************/

BOOL prs_string2(BOOL charmode, char *name, prs_struct *ps, int depth, STRING2 *str)
{
	char *q = prs_mem_get(ps, str->str_str_len * sizeof(uint8));
	if (q == NULL)
		return False;

	if (UNMARSHALLING(ps)) {
		str->buffer = (unsigned char *)prs_alloc_mem(ps,str->str_str_len);
		if (str->buffer == NULL)
			return False;
	}

	DBG_RW_PCVAL(charmode, name, depth, ps->data_offset, ps->io, q, str->buffer, str->str_max_len)
	ps->data_offset += (str->str_str_len * sizeof(uint8));

	return True;
}

/******************************************************************
 Stream a unicode string, length/buffer specified separately,
 in uint16 chars. We use DBG_RW_PCVAL, not DBG_RW_PSVAL here
 as the unicode string is already in little-endian format.
 ********************************************************************/

BOOL prs_unistr2(BOOL charmode, char *name, prs_struct *ps, int depth, UNISTR2 *str)
{
	char *p;
	char *q = prs_mem_get(ps, str->uni_str_len * sizeof(uint16));
	if (q == NULL)
		return False;

	if (UNMARSHALLING(ps)) {
		str->buffer = (uint16 *)prs_alloc_mem(ps,str->uni_max_len * sizeof(uint16));
		if (str->buffer == NULL)
			return False;
		memset(str->buffer, '\0', str->uni_max_len * sizeof(uint16));
	}

	/* If the string is empty, we don't have anything to stream */
	if (str->uni_str_len==0)
		return True;

	p = (char *)str->buffer;

	/* If we're using big-endian, reverse to get little-endian. */
	if(ps->bigendian_data) {
		DBG_RW_PSVAL(charmode, name, depth, ps->data_offset, 
			     ps->io, ps->bigendian_data, q, p, 
			     str->uni_str_len)
	} else {
		DBG_RW_PCVAL(charmode, name, depth, ps->data_offset, 
			     ps->io, q, p, str->uni_str_len * sizeof(uint16))
	}
	
	ps->data_offset += (str->uni_str_len * sizeof(uint16));

	return True;
}



/******************************************************************
 Stream a unicode string, length/buffer specified separately,
 in uint16 chars. We use DBG_RW_PCVAL, not DBG_RW_PSVAL here
 as the unicode string is already in little-endian format.
 ********************************************************************/

BOOL prs_unistr3(BOOL charmode, char *name, UNISTR3 *str, prs_struct *ps, int depth)
{
	char *p;
	char *q = prs_mem_get(ps, str->uni_str_len * sizeof(uint16));
	if (q == NULL)
		return False;

	if (UNMARSHALLING(ps)) {
		str->str.buffer = (uint16 *)prs_alloc_mem(ps,str->uni_str_len * sizeof(uint16));
		if (str->str.buffer == NULL)
			return False;
	}

	p = (char *)str->str.buffer;

	/* If we're using big-endian, reverse to get little-endian. */
	if(ps->bigendian_data)
		DBG_RW_PSVAL(charmode, name, depth, ps->data_offset, ps->io, ps->bigendian_data, q, p, str->uni_str_len)
	else
		DBG_RW_PCVAL(charmode, name, depth, ps->data_offset, ps->io, q, p, str->uni_str_len * sizeof(uint16))
	ps->data_offset += (str->uni_str_len * sizeof(uint16));

	return True;
}

/*******************************************************************
 Stream a unicode  null-terminated string. As the string is already
 in little-endian format then do it as a stream of bytes.
 ********************************************************************/

BOOL prs_unistr(char *name, prs_struct *ps, int depth, UNISTR *str)
{
	int len = 0;
	unsigned char *p = (unsigned char *)str->buffer;
	uint8 *start;
	char *q;
	char zero=0;
	uint32 max_len;
	uint16* ptr;

	if (MARSHALLING(ps)) {

		for(len = 0; str->buffer[len] != 0; len++)
			;

		q = prs_mem_get(ps, (len+1)*2);
		if (q == NULL)
			return False;

		start = (uint8*)q;

		for(len = 0; str->buffer[len] != 0; len++) 
		{
			if(ps->bigendian_data) 
			{
				RW_SVAL(ps->io, ps->bigendian_data, q, *p, 0);
				p += 2;
				q += 2;
			} 
			else 
			{
				RW_CVAL(ps->io, q, *p, 0);
				p++;
				q++;
				RW_CVAL(ps->io, q, *p, 0);
				p++;
				q++;
			}
		}

		/*
		 * even if the string is 'empty' (only an \0 char)
		 * at this point the leading \0 hasn't been parsed.
		 * so parse it now
		 */

		RW_CVAL(ps->io, q, zero, 0);
		q++;
		RW_CVAL(ps->io, q, zero, 0);
		q++;

		len++;

		dump_data(5+depth, (char *)start, len * 2);
	}
	else { /* unmarshalling */
	
		uint32 alloc_len = 0;
		q = prs_data_p(ps) + prs_offset(ps);

		/*
		 * Work out how much space we need and talloc it.
		 */
		max_len = (ps->buffer_size - ps->data_offset)/sizeof(uint16);

		/* the test of the value of *ptr helps to catch the circumstance
		   where we have an emtpty (non-existent) string in the buffer */
		for ( ptr = (uint16 *)q; *ptr && (alloc_len <= max_len); alloc_len++)
			/* do nothing */ 
			;

		/* should we allocate anything at all? */
		str->buffer = (uint16 *)prs_alloc_mem(ps,alloc_len * sizeof(uint16));
		if ((str->buffer == NULL) && (alloc_len > 0))
			return False;

		p = (unsigned char *)str->buffer;

		len = 0;
		/* the (len < alloc_len) test is to prevent us from overwriting
		   memory that is not ours...if we get that far, we have a non-null
		   terminated string in the buffer and have messed up somewhere */
		while ((len < alloc_len) && (*q != '\0'))
		{
			if(ps->bigendian_data) 
			{
				RW_SVAL(ps->io, ps->bigendian_data, q, *p, 0);
				p += 2;
				q += 2;
			} else {
#if WORDS_BIGENDIAN
				RW_CVAL(ps->io, q+1, *p, 0);
				p++;
				RW_CVAL(ps->io, q, *p, 0);
				p++;
				q+=2;
#else
				RW_CVAL(ps->io, q, *p, 0);
				p++;
				q++;
				RW_CVAL(ps->io, q, *p, 0);
				p++;
				q++;
#endif	/* WORDS_BIGENDIAN */

			}

			len++;
		} 
		if (len < alloc_len)
		{
			/* NULL terminate the UNISTR */
			str->buffer[len++] = '\0';
		}
	}

	/* set the offset in the prs_struct; 'len' points to the
	   terminiating NULL in the UNISTR so we need to go one more
	   uint16 */
	ps->data_offset += (len)*2;
	
	return True;
}


/*******************************************************************
 Stream a null-terminated string.  len is strlen, and therefore does
 not include the null-termination character.
 ********************************************************************/

BOOL prs_string(char *name, prs_struct *ps, int depth, char *str, int len, int max_buf_size)
{
	char *q;
	uint8 *start;
	int i;

	len = MIN(len, (max_buf_size-1));

	q = prs_mem_get(ps, len+1);
	if (q == NULL)
		return False;

	start = (uint8*)q;

	for(i = 0; i < len; i++) {
		RW_CVAL(ps->io, q, str[i],0);
		q++;
	}

	/* The terminating null. */
	str[i] = '\0';

	if (MARSHALLING(ps)) {
		RW_CVAL(ps->io, q, str[i], 0);
	}

	ps->data_offset += len+1;

	dump_data(5+depth, (char *)start, len);

	return True;
}

/*******************************************************************
 prs_uint16 wrapper. Call this and it sets up a pointer to where the
 uint16 should be stored, or gets the size if reading.
 ********************************************************************/

BOOL prs_uint16_pre(char *name, prs_struct *ps, int depth, uint16 *data16, uint32 *offset)
{
	*offset = ps->data_offset;
	if (UNMARSHALLING(ps)) {
		/* reading. */
		return prs_uint16(name, ps, depth, data16);
	} else {
		char *q = prs_mem_get(ps, sizeof(uint16));
		if(q ==NULL)
			return False;
		ps->data_offset += sizeof(uint16);
	}
	return True;
}

/*******************************************************************
 prs_uint16 wrapper.  call this and it retrospectively stores the size.
 does nothing on reading, as that is already handled by ...._pre()
 ********************************************************************/

BOOL prs_uint16_post(char *name, prs_struct *ps, int depth, uint16 *data16,
				uint32 ptr_uint16, uint32 start_offset)
{
	if (MARSHALLING(ps)) {
		/* 
		 * Writing - temporarily move the offset pointer.
		 */
		uint16 data_size = ps->data_offset - start_offset;
		uint32 old_offset = ps->data_offset;

		ps->data_offset = ptr_uint16;
		if(!prs_uint16(name, ps, depth, &data_size)) {
			ps->data_offset = old_offset;
			return False;
		}
		ps->data_offset = old_offset;
	} else {
		ps->data_offset = start_offset + (uint32)(*data16);
	}
	return True;
}

/*******************************************************************
 prs_uint32 wrapper. Call this and it sets up a pointer to where the
 uint32 should be stored, or gets the size if reading.
 ********************************************************************/

BOOL prs_uint32_pre(char *name, prs_struct *ps, int depth, uint32 *data32, uint32 *offset)
{
	*offset = ps->data_offset;
	if (UNMARSHALLING(ps) && (data32 != NULL)) {
		/* reading. */
		return prs_uint32(name, ps, depth, data32);
	} else {
		ps->data_offset += sizeof(uint32);
	}
	return True;
}

/*******************************************************************
 prs_uint32 wrapper.  call this and it retrospectively stores the size.
 does nothing on reading, as that is already handled by ...._pre()
 ********************************************************************/

BOOL prs_uint32_post(char *name, prs_struct *ps, int depth, uint32 *data32,
				uint32 ptr_uint32, uint32 data_size)
{
	if (MARSHALLING(ps)) {
		/* 
		 * Writing - temporarily move the offset pointer.
		 */
		uint32 old_offset = ps->data_offset;
		ps->data_offset = ptr_uint32;
		if(!prs_uint32(name, ps, depth, &data_size)) {
			ps->data_offset = old_offset;
			return False;
		}
		ps->data_offset = old_offset;
	}
	return True;
}

/* useful function to store a structure in rpc wire format */
int tdb_prs_store(TDB_CONTEXT *tdb, char *keystr, prs_struct *ps)
{
    TDB_DATA kbuf, dbuf;
    kbuf.dptr = keystr;
    kbuf.dsize = strlen(keystr)+1;
    dbuf.dptr = prs_data_p(ps);
    dbuf.dsize = prs_offset(ps);
    return tdb_store(tdb, kbuf, dbuf, TDB_REPLACE);
}

/* useful function to fetch a structure into rpc wire format */
int tdb_prs_fetch(TDB_CONTEXT *tdb, char *keystr, prs_struct *ps, TALLOC_CTX *mem_ctx)
{
    TDB_DATA kbuf, dbuf;
    kbuf.dptr = keystr;
    kbuf.dsize = strlen(keystr)+1;

    dbuf = tdb_fetch(tdb, kbuf);
    if (!dbuf.dptr) return -1;

    ZERO_STRUCTP(ps);
    prs_init(ps, 0, 4, mem_ctx, UNMARSHALL);
    prs_give_memory(ps, dbuf.dptr, dbuf.dsize, True);

    return 0;
} 
