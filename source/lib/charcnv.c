/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   Character set conversion Extensions
   Copyright (C) Igor Vergeichik <iverg@mail.ru> 2001
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

static pstring cvtbuf;

static smb_iconv_t conv_handles[NUM_CHARSETS][NUM_CHARSETS];

static int initialized;

/****************************************************************************
return the name of a charset to give to iconv()
****************************************************************************/
static char *charset_name(charset_t ch)
{
	char *ret = NULL;

	if (ch == CH_UCS2) ret = "UCS-2LE";
	else if (ch == CH_UNIX) ret = lp_unix_charset();
	else if (ch == CH_DOS) ret = lp_dos_charset();
	else if (ch == CH_DISPLAY) ret = lp_display_charset();

	if (!ret || !*ret) ret = "ASCII";
	return ret;
}

/****************************************************************************
 Initialize iconv conversion descriptors 
****************************************************************************/
void init_iconv(void)
{
	int c1, c2;

	/* so that charset_name() works we need to get the UNIX<->UCS2 going
	   first */
	if (!conv_handles[CH_UNIX][CH_UCS2]) {
		conv_handles[CH_UNIX][CH_UCS2] = smb_iconv_open("UCS-2LE", "ASCII");
	}
	if (!conv_handles[CH_UCS2][CH_UNIX]) {
		conv_handles[CH_UCS2][CH_UNIX] = smb_iconv_open("ASCII", "UCS-2LE");
	}
	

	for (c1=0;c1<NUM_CHARSETS;c1++) {
		for (c2=0;c2<NUM_CHARSETS;c2++) {
			char *n1 = charset_name((charset_t)c1);
			char *n2 = charset_name((charset_t)c2);
			if (conv_handles[c1][c2]) {
				smb_iconv_close(conv_handles[c1][c2]);
			}
			conv_handles[c1][c2] = smb_iconv_open(n2,n1);
			if (conv_handles[c1][c2] == (smb_iconv_t)-1) {
				DEBUG(0,("Conversion from %s to %s not supported\n",
					 charset_name((charset_t)c1), charset_name((charset_t)c2)));
				conv_handles[c1][c2] = NULL;
			}
		}
	}
}

/****************************************************************************
 Convert string from one encoding to another, making error checking etc
 Parameters:
	descriptor - conversion descriptor, created in init_iconv
 	src - pointer to source string (multibyte or singlebyte)
	srclen - length of the source string in bytes
	dest - pointer to destination string (multibyte or singlebyte)
	destlen - maximal length allowed for string
return the number of bytes occupied in the destination
****************************************************************************/
size_t convert_string(charset_t from, charset_t to,
		      void const *src, size_t srclen, 
		      void *dest, size_t destlen)
{
	size_t i_len, o_len;
	size_t retval;
	char* inbuf = (char*)src;
	char* outbuf = (char*)dest;
	smb_iconv_t descriptor;

	if (srclen == -1) srclen = strlen(src)+1;

	if (!initialized) {
		initialized = 1;
		load_case_tables();
		init_iconv();
	}

	descriptor = conv_handles[from][to];

	if (descriptor == (smb_iconv_t)-1 || descriptor == (smb_iconv_t)0) {
		/* conversion not supported, use as is */
		int len = MIN(srclen,destlen);
		memcpy(dest,src,len);
		return len;
	}

	i_len=srclen;
	o_len=destlen;
	retval=smb_iconv(descriptor,&inbuf, &i_len, &outbuf, &o_len);
	if(retval==-1) 		
	{
	    	char *reason="unknown error";
		switch(errno)
		{ case EINVAL: reason="Incomplete multibyte sequence"; break;
		  case E2BIG:  reason="No more room"; 
		   	       DEBUG(0, ("Required %d, available %d\n",
			       srclen, destlen));
			       /* we are not sure we need srclen bytes,
			          may be more, may be less.
				  We only know we need more than destlen
				  bytes ---simo */
				
						
		               break;
		  case EILSEQ: reason="Illegal myltibyte sequence"; break;
		}
		DEBUG(0,("Conversion error: %s(%s)\n",reason,inbuf));
		/* smb_panic(reason); */
	}
	return destlen-o_len;
}

/* you must provide source lenght -1 is not accepted as lenght.
   this function will return the size in bytes of the converted string
   or -1 in case of error.
 */
size_t convert_string_allocate(charset_t from, charset_t to,
		      		void const *src, size_t srclen, void **dest)
{
	size_t i_len, o_len, destlen;
	size_t retval;
	char* inbuf = (char *)src;
	char *outbuf, *ob;
	smb_iconv_t descriptor;

	*dest = NULL;

	if (src == NULL || srclen == -1) return -1;

	if (!initialized) {
		initialized = 1;
		load_case_tables();
		init_iconv();
	}

	descriptor = conv_handles[from][to];

	if (descriptor == (smb_iconv_t)-1 || descriptor == (smb_iconv_t)0) {
		/* conversion not supported, return -1*/
		return -1;
	}

	destlen = MAX(srclen, 1024);
	outbuf = NULL;
convert:
	destlen = destlen * 2;
	ob = (char *)realloc(outbuf, destlen);
	if (!ob) {
		DEBUG(0, ("convert_string_allocate: realloc failed!\n"));
		free(outbuf);
		return -1;
	}
	else outbuf = ob;
	i_len = srclen;
	o_len = destlen;
	retval = smb_iconv(descriptor,
				&inbuf, &i_len,
				&outbuf, &o_len);
	if(retval == -1) 		
	{
	    	char *reason="unknown error";
		switch(errno)
		{
		case EINVAL:
			reason="Incomplete multibyte sequence";
			break;
		case E2BIG:
			goto convert;		
			break;
		case EILSEQ:
			reason="Illegal myltibyte sequence";
			break;
		}
		DEBUG(0,("Conversion error: %s(%s)\n",reason,inbuf));
		/* smb_panic(reason); */
		return -1;
	}
	
	destlen = destlen - o_len;
	*dest = (char *)malloc(destlen);
	if (!*dest) {
		DEBUG(0, ("convert_string_allocate: out of memory!\n"));
		free(outbuf);
		return -1;
	}
	memcpy(*dest, outbuf, destlen);
	free(outbuf);
	
	return destlen;
}

int unix_strupper(const char *src, size_t srclen, char *dest, size_t destlen)
{
	int size;
	smb_ucs2_t *buffer=(smb_ucs2_t*)cvtbuf;
	size=convert_string(CH_UNIX, CH_UCS2, src, srclen, buffer, sizeof(cvtbuf));
	if (!strupper_w(buffer) && (dest == src)) return srclen;
	return convert_string(CH_UCS2, CH_UNIX, buffer, size, dest, destlen);
}

int unix_strlower(const char *src, size_t srclen, char *dest, size_t destlen)
{
	int size;
	smb_ucs2_t *buffer=(smb_ucs2_t*)cvtbuf;
	size=convert_string(CH_UNIX, CH_UCS2, src, srclen, buffer, sizeof(cvtbuf));
	if (!strlower_w(buffer) && (dest == src)) return srclen;
	return convert_string(CH_UCS2, CH_UNIX, buffer, size, dest, destlen);
}


int ucs2_align(const void *base_ptr, const void *p, int flags)
{
	if (flags & (STR_NOALIGN|STR_ASCII)) return 0;
	return PTR_DIFF(p, base_ptr) & 1;
}


/****************************************************************************
copy a string from a char* unix src to a dos codepage string destination
return the number of bytes occupied by the string in the destination
flags can have:
  STR_TERMINATE means include the null termination
  STR_UPPER     means uppercase in the destination
dest_len is the maximum length allowed in the destination. If dest_len
is -1 then no maxiumum is used
****************************************************************************/
int push_ascii(void *dest, const char *src, int dest_len, int flags)
{
	int src_len = strlen(src);
	pstring tmpbuf;

	/* treat a pstring as "unlimited" length */
	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (flags & STR_UPPER) {
		pstrcpy(tmpbuf, src);
		strupper(tmpbuf);
		src = tmpbuf;
	}

	if (flags & STR_TERMINATE) {
		src_len++;
	}

	return convert_string(CH_UNIX, CH_DOS, src, src_len, dest, dest_len);
}

int push_ascii_fstring(void *dest, const char *src)
{
	return push_ascii(dest, src, sizeof(fstring), STR_TERMINATE);
}

int push_ascii_pstring(void *dest, const char *src)
{
	return push_ascii(dest, src, sizeof(pstring), STR_TERMINATE);
}

int push_pstring(void *dest, const char *src)
{
	return push_ascii(dest, src, sizeof(pstring), STR_TERMINATE);
}


/****************************************************************************
copy a string from a dos codepage source to a unix char* destination
flags can have:
  STR_TERMINATE means the string in src is null terminated
if STR_TERMINATE is set then src_len is ignored
src_len is the length of the source area in bytes
return the number of bytes occupied by the string in src
the resulting string in "dest" is always null terminated
****************************************************************************/
int pull_ascii(char *dest, const void *src, int dest_len, int src_len, int flags)
{
	int ret;

	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (flags & STR_TERMINATE) src_len = strlen(src)+1;

	ret = convert_string(CH_DOS, CH_UNIX, src, src_len, dest, dest_len);

	if (dest_len) dest[MIN(ret, dest_len-1)] = 0;

	return src_len;
}

int pull_ascii_pstring(char *dest, const void *src)
{
	return pull_ascii(dest, src, sizeof(pstring), -1, STR_TERMINATE);
}

int pull_ascii_fstring(char *dest, const void *src)
{
	return pull_ascii(dest, src, sizeof(fstring), -1, STR_TERMINATE);
}

/****************************************************************************
copy a string from a char* src to a unicode destination
return the number of bytes occupied by the string in the destination
flags can have:
  STR_TERMINATE means include the null termination
  STR_UPPER     means uppercase in the destination
  STR_NOALIGN   means don't do alignment
dest_len is the maximum length allowed in the destination. If dest_len
is -1 then no maxiumum is used
****************************************************************************/
int push_ucs2(const void *base_ptr, void *dest, const char *src, int dest_len, int flags)
{
	int len=0;
	int src_len = strlen(src);
	pstring tmpbuf;

	/* treat a pstring as "unlimited" length */
	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (flags & STR_UPPER) {
		pstrcpy(tmpbuf, src);
		strupper(tmpbuf);
		src = tmpbuf;
	}

	if (flags & STR_TERMINATE) {
		src_len++;
	}

	if (ucs2_align(base_ptr, dest, flags)) {
		*(char *)dest = 0;
		dest = (void *)((char *)dest + 1);
		if (dest_len) dest_len--;
		len++;
	}

	/* ucs2 is always a multiple of 2 bytes */
	dest_len &= ~1;

	len += convert_string(CH_UNIX, CH_UCS2, src, src_len, dest, dest_len);
	return len;
}


/****************************************************************************
copy a string from a ucs2 source to a unix char* destination
flags can have:
  STR_TERMINATE means the string in src is null terminated
  STR_NOALIGN   means don't try to align
if STR_TERMINATE is set then src_len is ignored
src_len is the length of the source area in bytes
return the number of bytes occupied by the string in src
the resulting string in "dest" is always null terminated
****************************************************************************/
int pull_ucs2(const void *base_ptr, char *dest, const void *src, int dest_len, int src_len, int flags)
{
	int ret;

	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (ucs2_align(base_ptr, src, flags)) {
		src = (const void *)((const char *)src + 1);
		if (src_len > 0) src_len--;
	}

	if (flags & STR_TERMINATE) src_len = strlen_w(src)*2+2;

	/* ucs2 is always a multiple of 2 bytes */
	src_len &= ~1;
	
	ret = convert_string(CH_UCS2, CH_UNIX, src, src_len, dest, dest_len);
	if (dest_len) dest[MIN(ret, dest_len-1)] = 0;

	return src_len;
}

int pull_ucs2_pstring(char *dest, const void *src)
{
	return pull_ucs2(NULL, dest, src, sizeof(pstring), -1, STR_TERMINATE);
}

int pull_ucs2_fstring(char *dest, const void *src)
{
	return pull_ucs2(NULL, dest, src, sizeof(fstring), -1, STR_TERMINATE);
}


/****************************************************************************
copy a string from a char* src to a unicode or ascii
dos codepage destination choosing unicode or ascii based on the 
flags in the SMB buffer starting at base_ptr
return the number of bytes occupied by the string in the destination
flags can have:
  STR_TERMINATE means include the null termination
  STR_UPPER     means uppercase in the destination
  STR_ASCII     use ascii even with unicode packet
  STR_NOALIGN   means don't do alignment
dest_len is the maximum length allowed in the destination. If dest_len
is -1 then no maxiumum is used
****************************************************************************/
int push_string(const void *base_ptr, void *dest, const char *src, int dest_len, int flags)
{
	if (!(flags & STR_ASCII) && \
	    ((flags & STR_UNICODE || \
	      (SVAL(base_ptr, smb_flg2) & FLAGS2_UNICODE_STRINGS)))) {
		return push_ucs2(base_ptr, dest, src, dest_len, flags);
	}
	return push_ascii(dest, src, dest_len, flags);
}


/****************************************************************************
copy a string from a unicode or ascii source (depending on
the packet flags) to a char* destination
flags can have:
  STR_TERMINATE means the string in src is null terminated
  STR_UNICODE   means to force as unicode
  STR_ASCII     use ascii even with unicode packet
  STR_NOALIGN   means don't do alignment
if STR_TERMINATE is set then src_len is ignored
src_len is the length of the source area in bytes
return the number of bytes occupied by the string in src
the resulting string in "dest" is always null terminated
****************************************************************************/
int pull_string(const void *base_ptr, char *dest, const void *src, int dest_len, int src_len, 
		int flags)
{
	if (!(flags & STR_ASCII) && \
	    ((flags & STR_UNICODE || \
	      (SVAL(base_ptr, smb_flg2) & FLAGS2_UNICODE_STRINGS)))) {
		return pull_ucs2(base_ptr, dest, src, dest_len, src_len, flags);
	}
	return pull_ascii(dest, src, dest_len, src_len, flags);
}

int align_string(const void *base_ptr, const char *p, int flags)
{
	if (!(flags & STR_ASCII) && \
	    ((flags & STR_UNICODE || \
	      (SVAL(base_ptr, smb_flg2) & FLAGS2_UNICODE_STRINGS)))) {
		return ucs2_align(base_ptr, p, flags);
	}
	return 0;
}
