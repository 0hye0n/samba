/*
 * Copyright (c) 1997, 1998 Kungliga Tekniska H�gskolan
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

#include "kadm5_locl.h"

RCSID("$Id$");

static char *op_names[] = {
    "get",
    "delete",
    "create",
    "rename",
    "chpass",
    "modify",
    "randkey",
    "get_privs",
    "get_princs"
};

static void
print_entry(u_int32_t ver,
	    time_t timestamp,
	    enum kadm_ops op,
	    u_int32_t len,
	    krb5_storage *sp)
{
    char *p = ctime(&timestamp);
    p[strlen(p) - 1] = '\0';

    printf ("ver = %u, timestamp = %s, op = %d (%s), len = %u\n",
	    ver, p, op, op_names[op], len);
    sp->seek (sp, len, SEEK_CUR);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    void *kadm_handle;
    kadm5_server_context *server_context;
    kadm5_config_params conf;

    set_progname(argv[0]);

    krb5_init_context(&context);

    memset(&conf, 0, sizeof(conf));
    ret = kadm5_init_with_password_ctx (context,
					KADM5_ADMIN_SERVICE,
					NULL,
					KADM5_ADMIN_SERVICE,
					&conf, 0, 0, 
					&kadm_handle);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_init_with_password_ctx");

    server_context = (kadm5_server_context *)kadm_handle;

    ret = kadm5_log_init (server_context);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_log_init");

    ret = kadm5_log_foreach (server_context, print_entry);
    if(ret)
	krb5_warn(context, ret, "kadm5_log_foreach");

    ret = kadm5_log_end (server_context);
    if (ret)
	krb5_warn(context, ret, "kadm5_log_end");
    return 0;
}
