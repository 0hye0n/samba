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

#include "rsh_locl.h"
RCSID("$Id$");

ssize_t
do_read (int fd,
	 void *buf,
	 size_t sz)
{
    int ret;

    if (do_encrypt) {
#ifdef KRB4
	if (auth_method == AUTH_KRB4) {
	    return des_enc_read (fd, buf, sz, schedule, &iv);
	} else
#endif /* KRB4 */
        if(auth_method == AUTH_KRB5) {
	    u_int32_t len, outer_len;
	    int status;
	    krb5_data data;

	    ret = krb5_net_read (context, fd, &len, 4);
	    if (ret != 4)
		return ret;
	    len = ntohl(len);
	    outer_len = len + 12;
	    outer_len = (outer_len + 7) & ~7;
	    if (outer_len > sz)
		abort ();
	    ret = krb5_net_read (context, fd, buf, outer_len);
	    if (ret != outer_len)
		return ret;
	    status = krb5_decrypt(context, buf, outer_len,
				  ETYPE_DES_CBC_CRC, /* XXX */
				  keyblock, &data);
	    if (status)
		errx (1, "%s", krb5_get_err_text (context, status));
	    memcpy (buf, data.data, len);
	    free (data.data);
	    return len;
	} else {
	    abort ();
	}
    } else
	return read (fd, buf, sz);
}

ssize_t
do_write (int fd, void *buf, size_t sz)
{
    int ret;

    if (do_encrypt) {
#ifdef KRB4
	if(auth_method == AUTH_KRB4) {
	    return des_enc_write (fd, buf, sz, schedule, &iv);
	} else
#endif /* KRB4 */
	if(auth_method == AUTH_KRB5) {
	    krb5_error_code status;
	    krb5_data data;
	    u_int32_t len;
	    int ret;

	    status = krb5_encrypt (context,
				   buf,
				   sz,
				   ETYPE_DES_CBC_CRC, /* XXX */
				   keyblock,
				   &data);
	    if (status)
		errx (1, "%s", krb5_get_err_text(context, status));
	    len = htonl(sz);
	    ret = krb5_net_write (context, fd, &len, 4);
	    if (ret != 4)
		return ret;
	    ret = krb5_net_write (context, fd, data.data, data.length);
	    if (ret != data.length)
		return ret;
	    free (data.data);
	    return sz;
	} else {
	    abort();
	}
    } else
	return write (fd, buf, sz);
}

ssize_t
net_write (int fd,
	   const void *buf,
	   size_t len)
{
  char *cbuf = (char *)buf;
  ssize_t count;
  size_t rem = len;

  while (rem > 0) {
    count = write (fd, cbuf, rem);
    if (count < 0)
      return count;
    cbuf += count;
    rem -= count;
  }
  return len;
}

ssize_t
net_read (int fd,
	  void *buf,
	  size_t len)
{
  char *cbuf = (char *)buf;
  ssize_t count;
  size_t rem = len;

  while (rem > 0) {
    count = read (fd, cbuf, rem);
    if (count <= 0)
      return count;
    cbuf += count;
    rem -= count;
  }
  return len;
}
