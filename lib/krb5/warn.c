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

#include "krb5_locl.h"
#include <err.h>

RCSID("$Id$");

static krb5_error_code
_warnerr(krb5_context context, int doexit, int eval, int do_errtext, krb5_error_code code, 
	int level, const char *fmt, va_list ap)
{
    char xfmt[7] = "";
    const char *args[2], **arg;
    char *msg = NULL;
    
    arg = args;
    if(fmt){
	strcat(xfmt, "%s");
	if(do_errtext)
	    strcat(xfmt, ": ");
	vasprintf(&msg, fmt, ap);
	if(msg == NULL)
	    return ENOMEM;
	*arg++ = msg;
    }
    if(do_errtext){
	strcat(xfmt, "%s");
	*arg++ = krb5_get_err_text(context, code);
    }
	
    if(context->warn_dest)
	krb5_log(context, context->warn_dest, level, xfmt, args[0], args[1]);
    else
	warnx(xfmt, args[0], args[1]);
    if(doexit)
	exit(eval);
    return 0;
}

#define FUNC(DO_EXIT, EVAL, DO_ERRTEXT, CODE, LEVEL) 					\
	krb5_error_code ret;								\
	va_list ap;									\
	va_start(ap, fmt);								\
	ret = _warnerr(context, DO_EXIT, EVAL, DO_ERRTEXT, CODE, LEVEL, fmt, ap);	\
	va_end(ap);									\
	return ret;

krb5_error_code
krb5_vwarn(krb5_context context, krb5_error_code code, const char *fmt, va_list ap)
{
    return _warnerr(context, 0, 0, 1, code, 1, fmt, ap);
}


krb5_error_code
krb5_warn(krb5_context context, krb5_error_code code, const char *fmt, ...)
{
    FUNC(0, 0, 1, code, 1);
}

krb5_error_code
krb5_vwarnx(krb5_context context, const char *fmt, va_list ap)
{
    return _warnerr(context, 0, 0, 0, 0, 1, fmt, ap);
}

krb5_error_code
krb5_warnx(krb5_context context, const char *fmt, ...)
{
    FUNC(0, 0, 0, 0, 1);
}

krb5_error_code
krb5_verr(krb5_context context, int eval, krb5_error_code code, const char *fmt, va_list ap)
{
    return _warnerr(context, 1, eval, 1, code, 0, fmt, ap);
}


krb5_error_code
krb5_err(krb5_context context, int eval, krb5_error_code code, const char *fmt, ...)
{
    FUNC(1, eval, 1, code, 0);
}

krb5_error_code
krb5_verrx(krb5_context context, int eval, const char *fmt, va_list ap)
{
    return _warnerr(context, 1, eval, 0, 0, 0, fmt, ap);
}

krb5_error_code
krb5_errx(krb5_context context, int eval, const char *fmt, ...)
{
    FUNC(1, eval, 0, 0, 0);
}

krb5_error_code
krb5_set_warn_dest(krb5_context context, krb5_log_facility *fac)
{
    context->warn_dest = fac;
    return 0;
}
