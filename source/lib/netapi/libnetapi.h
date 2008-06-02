#ifndef __LIBNETAPI_LIBNETAPI__
#define __LIBNETAPI_LIBNETAPI__
NET_API_STATUS NetJoinDomain(const char * server /* [in] [unique] */,
			     const char * domain /* [in] [ref] */,
			     const char * account_ou /* [in] [unique] */,
			     const char * account /* [in] [unique] */,
			     const char * password /* [in] [unique] */,
			     uint32_t join_flags /* [in] */);
WERROR NetJoinDomain_r(struct libnetapi_ctx *ctx,
		       struct NetJoinDomain *r);
WERROR NetJoinDomain_l(struct libnetapi_ctx *ctx,
		       struct NetJoinDomain *r);
NET_API_STATUS NetUnjoinDomain(const char * server_name /* [in] [unique] */,
			       const char * account /* [in] [unique] */,
			       const char * password /* [in] [unique] */,
			       uint32_t unjoin_flags /* [in] */);
WERROR NetUnjoinDomain_r(struct libnetapi_ctx *ctx,
			 struct NetUnjoinDomain *r);
WERROR NetUnjoinDomain_l(struct libnetapi_ctx *ctx,
			 struct NetUnjoinDomain *r);
NET_API_STATUS NetGetJoinInformation(const char * server_name /* [in] [unique] */,
				     const char * *name_buffer /* [out] [ref] */,
				     uint16_t *name_type /* [out] [ref] */);
WERROR NetGetJoinInformation_r(struct libnetapi_ctx *ctx,
			       struct NetGetJoinInformation *r);
WERROR NetGetJoinInformation_l(struct libnetapi_ctx *ctx,
			       struct NetGetJoinInformation *r);
NET_API_STATUS NetGetJoinableOUs(const char * server_name /* [in] [unique] */,
				 const char * domain /* [in] [ref] */,
				 const char * account /* [in] [unique] */,
				 const char * password /* [in] [unique] */,
				 uint32_t *ou_count /* [out] [ref] */,
				 const char * **ous /* [out] [ref] */);
WERROR NetGetJoinableOUs_r(struct libnetapi_ctx *ctx,
			   struct NetGetJoinableOUs *r);
WERROR NetGetJoinableOUs_l(struct libnetapi_ctx *ctx,
			   struct NetGetJoinableOUs *r);
NET_API_STATUS NetServerGetInfo(const char * server_name /* [in] [unique] */,
				uint32_t level /* [in] */,
				uint8_t **buffer /* [out] [ref] */);
WERROR NetServerGetInfo_r(struct libnetapi_ctx *ctx,
			  struct NetServerGetInfo *r);
WERROR NetServerGetInfo_l(struct libnetapi_ctx *ctx,
			  struct NetServerGetInfo *r);
NET_API_STATUS NetServerSetInfo(const char * server_name /* [in] [unique] */,
				uint32_t level /* [in] */,
				uint8_t *buffer /* [in] [ref] */,
				uint32_t *parm_error /* [out] [ref] */);
WERROR NetServerSetInfo_r(struct libnetapi_ctx *ctx,
			  struct NetServerSetInfo *r);
WERROR NetServerSetInfo_l(struct libnetapi_ctx *ctx,
			  struct NetServerSetInfo *r);
NET_API_STATUS NetGetDCName(const char * server_name /* [in] [unique] */,
			    const char * domain_name /* [in] [unique] */,
			    uint8_t **buffer /* [out] [ref] */);
WERROR NetGetDCName_r(struct libnetapi_ctx *ctx,
		      struct NetGetDCName *r);
WERROR NetGetDCName_l(struct libnetapi_ctx *ctx,
		      struct NetGetDCName *r);
NET_API_STATUS NetGetAnyDCName(const char * server_name /* [in] [unique] */,
			       const char * domain_name /* [in] [unique] */,
			       uint8_t **buffer /* [out] [ref] */);
WERROR NetGetAnyDCName_r(struct libnetapi_ctx *ctx,
			 struct NetGetAnyDCName *r);
WERROR NetGetAnyDCName_l(struct libnetapi_ctx *ctx,
			 struct NetGetAnyDCName *r);
NET_API_STATUS DsGetDcName(const char * server_name /* [in] [unique] */,
			   const char * domain_name /* [in] [ref] */,
			   struct GUID *domain_guid /* [in] [unique] */,
			   const char * site_name /* [in] [unique] */,
			   uint32_t flags /* [in] */,
			   struct DOMAIN_CONTROLLER_INFO **dc_info /* [out] [ref] */);
WERROR DsGetDcName_r(struct libnetapi_ctx *ctx,
		     struct DsGetDcName *r);
WERROR DsGetDcName_l(struct libnetapi_ctx *ctx,
		     struct DsGetDcName *r);
NET_API_STATUS NetUserAdd(const char * server_name /* [in] [unique] */,
			  uint32_t level /* [in] */,
			  uint8_t *buffer /* [in] [ref] */,
			  uint32_t *parm_error /* [out] [ref] */);
WERROR NetUserAdd_r(struct libnetapi_ctx *ctx,
		    struct NetUserAdd *r);
WERROR NetUserAdd_l(struct libnetapi_ctx *ctx,
		    struct NetUserAdd *r);
NET_API_STATUS NetUserDel(const char * server_name /* [in] [unique] */,
			  const char * user_name /* [in] [ref] */);
WERROR NetUserDel_r(struct libnetapi_ctx *ctx,
		    struct NetUserDel *r);
WERROR NetUserDel_l(struct libnetapi_ctx *ctx,
		    struct NetUserDel *r);
NET_API_STATUS NetUserEnum(const char * server_name /* [in] [unique] */,
			   uint32_t level /* [in] */,
			   uint32_t filter /* [in] */,
			   uint8_t **buffer /* [out] [ref] */,
			   uint32_t prefmaxlen /* [in] */,
			   uint32_t *entries_read /* [out] [ref] */,
			   uint32_t *total_entries /* [out] [ref] */,
			   uint32_t *resume_handle /* [in,out] [ref] */);
WERROR NetUserEnum_r(struct libnetapi_ctx *ctx,
		     struct NetUserEnum *r);
WERROR NetUserEnum_l(struct libnetapi_ctx *ctx,
		     struct NetUserEnum *r);
NET_API_STATUS NetQueryDisplayInformation(const char * server_name /* [in] [unique] */,
					  uint32_t level /* [in] */,
					  uint32_t idx /* [in] */,
					  uint32_t entries_requested /* [in] */,
					  uint32_t prefmaxlen /* [in] */,
					  uint32_t *entries_read /* [out] [ref] */,
					  void **buffer /* [out] [noprint,ref] */);
WERROR NetQueryDisplayInformation_r(struct libnetapi_ctx *ctx,
				    struct NetQueryDisplayInformation *r);
WERROR NetQueryDisplayInformation_l(struct libnetapi_ctx *ctx,
				    struct NetQueryDisplayInformation *r);
NET_API_STATUS NetGroupAdd(const char * server_name /* [in] */,
			   uint32_t level /* [in] */,
			   uint8_t *buf /* [in] [ref] */,
			   uint32_t *parm_err /* [out] [ref] */);
WERROR NetGroupAdd_r(struct libnetapi_ctx *ctx,
		     struct NetGroupAdd *r);
WERROR NetGroupAdd_l(struct libnetapi_ctx *ctx,
		     struct NetGroupAdd *r);
NET_API_STATUS NetGroupDel(const char * server_name /* [in] */,
			   const char * group_name /* [in] */);
WERROR NetGroupDel_r(struct libnetapi_ctx *ctx,
		     struct NetGroupDel *r);
WERROR NetGroupDel_l(struct libnetapi_ctx *ctx,
		     struct NetGroupDel *r);
NET_API_STATUS NetGroupSetInfo(const char * server_name /* [in] */,
			       const char * group_name /* [in] */,
			       uint32_t level /* [in] */,
			       uint8_t *buf /* [in] [ref] */,
			       uint32_t *parm_err /* [out] [ref] */);
WERROR NetGroupSetInfo_r(struct libnetapi_ctx *ctx,
			 struct NetGroupSetInfo *r);
WERROR NetGroupSetInfo_l(struct libnetapi_ctx *ctx,
			 struct NetGroupSetInfo *r);
NET_API_STATUS NetGroupGetInfo(const char * server_name /* [in] */,
			       const char * group_name /* [in] */,
			       uint32_t level /* [in] */,
			       uint8_t **buf /* [out] [ref] */);
WERROR NetGroupGetInfo_r(struct libnetapi_ctx *ctx,
			 struct NetGroupGetInfo *r);
WERROR NetGroupGetInfo_l(struct libnetapi_ctx *ctx,
			 struct NetGroupGetInfo *r);
NET_API_STATUS NetGroupAddUser(const char * server_name /* [in] */,
			       const char * group_name /* [in] */,
			       const char * user_name /* [in] */);
WERROR NetGroupAddUser_r(struct libnetapi_ctx *ctx,
			 struct NetGroupAddUser *r);
WERROR NetGroupAddUser_l(struct libnetapi_ctx *ctx,
			 struct NetGroupAddUser *r);
NET_API_STATUS NetGroupDelUser(const char * server_name /* [in] */,
			       const char * group_name /* [in] */,
			       const char * user_name /* [in] */);
WERROR NetGroupDelUser_r(struct libnetapi_ctx *ctx,
			 struct NetGroupDelUser *r);
WERROR NetGroupDelUser_l(struct libnetapi_ctx *ctx,
			 struct NetGroupDelUser *r);
NET_API_STATUS NetLocalGroupAdd(const char * server_name /* [in] */,
				uint32_t level /* [in] */,
				uint8_t *buf /* [in] [ref] */,
				uint32_t *parm_err /* [out] [ref] */);
WERROR NetLocalGroupAdd_r(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupAdd *r);
WERROR NetLocalGroupAdd_l(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupAdd *r);
#endif /* __LIBNETAPI_LIBNETAPI__ */
