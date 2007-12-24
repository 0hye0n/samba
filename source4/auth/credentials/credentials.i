/* 
   Unix SMB/CIFS implementation.
   Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

%module(package="samba.credentials") credentials

%{

/* Include headers */
#include <stdint.h>
#include <stdbool.h>

#include "includes.h"
#include "auth/credentials/credentials.h"
#include "param/param.h"
typedef struct cli_credentials cli_credentials;
%}

%import "carrays.i"
%import "typemaps.i"
%import "param/param.i"

%typemap(default,noblock=1) struct cli_credentials * {
    $1 = NULL;
}

%{
#include "librpc/gen_ndr/samr.h" /* for struct samr_Password */
%}

%typemap(out,noblock=1) struct samr_Password * {
    $result = PyString_FromStringAndSize((char *)$1->hash, 16);
}

%talloctype(cli_credentials);
%rename(Credentials) cli_credentials;
typedef struct cli_credentials {
    %extend {
        cli_credentials(void) {
            return cli_credentials_init(NULL);
        }
        /* username */
        const char *get_username(void);
        bool set_username(const char *value, 
                          enum credentials_obtained=CRED_SPECIFIED);

        /* password */
        const char *get_password(void);
        bool set_password(const char *val, 
                          enum credentials_obtained=CRED_SPECIFIED);

        /* domain */
        const char *get_domain(void);
        bool set_domain(const char *val, 
                        enum credentials_obtained=CRED_SPECIFIED);

        /* realm */
        const char *get_realm(void);
        bool set_realm(const char *val, 
                       enum credentials_obtained=CRED_SPECIFIED);

        void parse_string(const char *text,
                       enum credentials_obtained=CRED_SPECIFIED);

        /* bind dn */
        const char *get_bind_dn(void);
        bool set_bind_dn(const char *bind_dn);

        /* workstation name */
        const char *get_workstation(void);
        bool set_workstation(const char *workstation, 
                             enum credentials_obtained obtained=CRED_SPECIFIED);

        void guess(struct loadparm_context *lp_ctx);
        bool is_anonymous(void);

        const struct samr_Password *get_nt_hash(TALLOC_CTX *mem_ctx);

        bool authentication_requested(void);

        bool wrong_password(void);
    }
} cli_credentials;
