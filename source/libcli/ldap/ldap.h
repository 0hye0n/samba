/* 
   Unix SMB/CIFS Implementation.
   LDAP protocol helper functions for SAMBA
   Copyright (C) Volker Lendecke 2004
    
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

#ifndef _SMB_LDAP_H
#define _SMB_LDAP_H

enum ldap_request_tag {
	LDAP_TAG_BindRequest = 0,
	LDAP_TAG_BindResponse = 1,
	LDAP_TAG_UnbindRequest = 2,
	LDAP_TAG_SearchRequest = 3,
	LDAP_TAG_SearchResultEntry = 4,
	LDAP_TAG_SearchResultDone = 5,
	LDAP_TAG_ModifyRequest = 6,
	LDAP_TAG_ModifyResponse = 7,
	LDAP_TAG_AddRequest = 8,
	LDAP_TAG_AddResponse = 9,
	LDAP_TAG_DelRequest = 10,
	LDAP_TAG_DelResponse = 11,
	LDAP_TAG_ModifyDNRequest = 12,
	LDAP_TAG_ModifyDNResponse = 13,
	LDAP_TAG_CompareRequest = 14,
	LDAP_TAG_CompareResponse = 15,
	LDAP_TAG_AbandonRequest = 16,
	LDAP_TAG_SearchResultReference = 19,
	LDAP_TAG_ExtendedRequest = 23,
	LDAP_TAG_ExtendedResponse = 24
};

enum ldap_auth_mechanism {
	LDAP_AUTH_MECH_SIMPLE = 0,
	LDAP_AUTH_MECH_SASL = 3
};

enum ldap_result_code {
	LDAP_SUCCESS				= 0,
	LDAP_OPERATIONS_ERROR			= 1,
	LDAP_PROTOCOL_ERROR			= 2,
	LDAP_TIME_LIMIT_EXCEEDED		= 3,
	LDAP_SIZE_LIMIT_EXCEEDED		= 4,
	LDAP_COMPARE_FALSE			= 5,
	LDAP_COMPARE_TRUE			= 6,
	LDAP_AUTH_METHOD_NOT_SUPPORTED		= 7,
	LDAP_STRONG_AUTH_REQUIRED		= 8,
	LDAP_REFERRAL				= 10,
	LDAP_ADMIN_LIMIT_EXCEEDED		= 11,
	LDAP_UNAVAILABLE_CRITICAL_EXTENSION	= 12,
	LDAP_CONFIDENTIALITY_REQUIRED		= 13,
	LDAP_SASL_BIND_IN_PROGRESS		= 14,
	LDAP_NO_SUCH_ATTRIBUTE			= 16,
	LDAP_UNDEFINED_ATTRIBUTE_TYPE		= 17,
	LDAP_INAPPROPRIATE_MATCHING		= 18,
	LDAP_CONSTRAINT_VIOLATION		= 19,
	LDAP_ATTRIBUTE_OR_VALUE_EXISTS		= 20,
	LDAP_INVALID_ATTRIBUTE_SYNTAX		= 21,
	LDAP_NO_SUCH_OBJECT			= 32,
	LDAP_ALIAS_PROBLEM			= 33,
	LDAP_INVALID_DN_SYNTAX			= 34,
	LDAP_ALIAS_DEREFERENCING_PROBLEM	= 36,
	LDAP_INAPPROPRIATE_AUTHENTICATION	= 48,
	LDAP_INVALID_CREDENTIALS		= 49,
	LDAP_INSUFFICIENT_ACCESS_RIGHTs		= 50,
	LDAP_BUSY				= 51,
	LDAP_UNAVAILABLE			= 52,
	LDAP_UNWILLING_TO_PERFORM		= 53,
	LDAP_LOOP_DETECT			= 54,
	LDAP_NAMING_VIOLATION			= 64,
	LDAP_OBJECT_CLASS_VIOLATION		= 65,
	LDAP_NOT_ALLOWED_ON_NON_LEAF		= 66,
	LDAP_NOT_ALLOWED_ON_RDN			= 67,
	LDAP_ENTRY_ALREADY_EXISTS		= 68,
	LDAP_OBJECT_CLASS_MODS_PROHIBITED	= 69,
	LDAP_AFFECTS_MULTIPLE_DSAS		= 71,
	LDAP_OTHER 				= 80
};

struct ldap_Result {
	int resultcode;
	const char *dn;
	const char *errormessage;
	const char *referral;
};

struct ldap_attribute {
	const char *name;
	int num_values;
	DATA_BLOB *values;
};

struct ldap_BindRequest {
	int version;
	const char *dn;
	enum ldap_auth_mechanism mechanism;
	union {
		const char *password;
		struct {
			const char *mechanism;
			DATA_BLOB secblob;
		} SASL;
	} creds;
};

struct ldap_BindResponse {
	struct ldap_Result response;
	union {
		DATA_BLOB secblob;
	} SASL;
};

struct ldap_UnbindRequest {
	uint8_t __dummy;
};

enum ldap_scope {
	LDAP_SEARCH_SCOPE_BASE = 0,
	LDAP_SEARCH_SCOPE_SINGLE = 1,
	LDAP_SEARCH_SCOPE_SUB = 2
};

enum ldap_deref {
	LDAP_DEREFERENCE_NEVER = 0,
	LDAP_DEREFERENCE_IN_SEARCHING = 1,
	LDAP_DEREFERENCE_FINDING_BASE = 2,
	LDAP_DEREFERENCE_ALWAYS
};

struct ldap_SearchRequest {
	const char *basedn;
	enum ldap_scope scope;
	enum ldap_deref deref;
	uint32_t timelimit;
	uint32_t sizelimit;
	BOOL attributesonly;
	const char *filter;
	int num_attributes;
	const char **attributes;
};

struct ldap_SearchResEntry {
	const char *dn;
	int num_attributes;
	struct ldap_attribute *attributes;
};

struct ldap_SearchResRef {
	const char *referral;
};

enum ldap_modify_type {
	LDAP_MODIFY_NONE = -1,
	LDAP_MODIFY_ADD = 0,
	LDAP_MODIFY_DELETE = 1,
	LDAP_MODIFY_REPLACE = 2
};

struct ldap_mod {
	enum ldap_modify_type type;
	struct ldap_attribute attrib;
};

struct ldap_ModifyRequest {
	const char *dn;
	int num_mods;
	struct ldap_mod *mods;
};

struct ldap_AddRequest {
	const char *dn;
	int num_attributes;
	struct ldap_attribute *attributes;
};

struct ldap_DelRequest {
	const char *dn;
};

struct ldap_ModifyDNRequest {
	const char *dn;
	const char *newrdn;
	BOOL deleteolddn;
	const char *newsuperior;
};

struct ldap_CompareRequest {
	const char *dn;
	const char *attribute;
	DATA_BLOB value;
};

struct ldap_AbandonRequest {
	uint32_t messageid;
};

struct ldap_ExtendedRequest {
	const char *oid;
	DATA_BLOB value;
};

struct ldap_ExtendedResponse {
	struct ldap_Result response;
	const char *name;
	DATA_BLOB value;
};

union ldap_Request {
	struct ldap_BindRequest 	BindRequest;
	struct ldap_BindResponse 	BindResponse;
	struct ldap_UnbindRequest 	UnbindRequest;
	struct ldap_SearchRequest 	SearchRequest;
	struct ldap_SearchResEntry 	SearchResultEntry;
	struct ldap_Result 		SearchResultDone;
	struct ldap_SearchResRef 	SearchResultReference;
	struct ldap_ModifyRequest 	ModifyRequest;
	struct ldap_Result 		ModifyResponse;
	struct ldap_AddRequest 		AddRequest;
	struct ldap_Result 		AddResponse;
	struct ldap_DelRequest 		DelRequest;
	struct ldap_Result 		DelResponse;
	struct ldap_ModifyDNRequest 	ModifyDNRequest;
	struct ldap_Result 		ModifyDNResponse;
	struct ldap_CompareRequest 	CompareRequest;
	struct ldap_Result 		CompareResponse;
	struct ldap_AbandonRequest 	AbandonRequest;
	struct ldap_ExtendedRequest 	ExtendedRequest;
	struct ldap_ExtendedResponse 	ExtendedResponse;
};

struct ldap_Control {
	const char *oid;
	BOOL        critical;
	DATA_BLOB   value;
};

struct ldap_message {
	TALLOC_CTX	       *mem_ctx;
	uint32_t                messageid;
	enum ldap_request_tag   type;
	union ldap_Request      r;
	int			num_controls;
	struct ldap_Control    *controls;
};

struct ldap_queue_entry {
	struct ldap_queue_entry *next, *prev;
	int msgid;
	struct ldap_message *msg;
};

struct ldap_connection {
	TALLOC_CTX *mem_ctx;
	int sock;
	int next_msgid;
	char *host;
	uint16_t port;
	BOOL ldaps;

	const char *auth_dn;
	const char *simple_pw;

	/* Current outstanding search entry */
	int searchid;

	/* List for incoming search entries */
	struct ldap_queue_entry *search_entries;

	/* Outstanding LDAP requests that have not yet been replied to */
	struct ldap_queue_entry *outstanding;

	/* Let's support SASL */
	struct gensec_security *gensec;
};

/* Hmm. A blob might be more appropriate here :-) */

struct ldap_val {
	unsigned int length;
	void *data;
};

enum ldap_parse_op {LDAP_OP_SIMPLE, LDAP_OP_AND, LDAP_OP_OR, LDAP_OP_NOT};

struct ldap_parse_tree {
	enum ldap_parse_op operation;
	union {
		struct {
			char *attr;
			struct ldap_val value;
		} simple;
		struct {
			unsigned int num_elements;
			struct ldap_parse_tree **elements;
		} list;
		struct {
			struct ldap_parse_tree *child;
		} not;
	} u;
};

#define LDAP_ALL_SEP "()&|=!"
#define LDAP_CONNECTION_TIMEOUT 10000

/* The following definitions come from libcli/ldap/ldap.c  */

BOOL ldap_encode(struct ldap_message *msg, DATA_BLOB *result);
BOOL ldap_decode(struct asn1_data *data, struct ldap_message *msg);
BOOL ldap_parse_basic_url(TALLOC_CTX *mem_ctx, const char *url,
			  char **host, uint16_t *port, BOOL *ldaps);

/* The following definitions come from libcli/ldap/ldap_client.c  */

struct ldap_connection *ldap_connect(TALLOC_CTX *mem_ctx, const char *url);
struct ldap_message *new_ldap_message(TALLOC_CTX *mem_ctx);
BOOL ldap_send_msg(struct ldap_connection *conn, struct ldap_message *msg,
		   const struct timeval *endtime);
BOOL ldap_receive_msg(struct ldap_connection *conn, struct ldap_message *msg,
		      const struct timeval *endtime);
struct ldap_message *ldap_receive(struct ldap_connection *conn, int msgid,
				  const struct timeval *endtime);
struct ldap_message *ldap_transaction(struct ldap_connection *conn,
				      struct ldap_message *request);
int ldap_bind_simple(struct ldap_connection *conn, const char *userdn, const char *password);
int ldap_bind_sasl(struct ldap_connection *conn, struct cli_credentials *creds);
struct ldap_connection *ldap_setup_connection(TALLOC_CTX *mem_ctx, const char *url, 
						const char *userdn, const char *password);
struct ldap_connection *ldap_setup_connection_with_sasl(TALLOC_CTX *mem_ctx, const char *url,
							struct cli_credentials *creds);
BOOL ldap_abandon_message(struct ldap_connection *conn, int msgid,
				 const struct timeval *endtime);
BOOL ldap_setsearchent(struct ldap_connection *conn, struct ldap_message *msg,
		       const struct timeval *endtime);
struct ldap_message *ldap_getsearchent(struct ldap_connection *conn,
				       const struct timeval *endtime);
void ldap_endsearchent(struct ldap_connection *conn,
		       const struct timeval *endtime);
struct ldap_message *ldap_searchone(struct ldap_connection *conn,
				    struct ldap_message *msg,
				    const struct timeval *endtime);
BOOL ldap_find_single_value(struct ldap_message *msg, const char *attr,
			    DATA_BLOB *value);
BOOL ldap_find_single_string(struct ldap_message *msg, const char *attr,
			     TALLOC_CTX *mem_ctx, char **value);
BOOL ldap_find_single_int(struct ldap_message *msg, const char *attr,
			  int *value);
int ldap_error(struct ldap_connection *conn);
NTSTATUS ldap2nterror(int ldaperror);

/* The following definitions come from libcli/ldap/ldap_ldif.c  */

BOOL add_value_to_attrib(TALLOC_CTX *mem_ctx, struct ldap_val *value,
			 struct ldap_attribute *attrib);
BOOL add_attrib_to_array_talloc(TALLOC_CTX *mem_ctx,
				       const struct ldap_attribute *attrib,
				       struct ldap_attribute **attribs,
				       int *num_attribs);
BOOL add_mod_to_array_talloc(TALLOC_CTX *mem_ctx,
				    struct ldap_mod *mod,
				    struct ldap_mod **mods,
				    int *num_mods);
struct ldap_message *ldap_ldif2msg(TALLOC_CTX *mem_ctx, const char *s);

#endif
