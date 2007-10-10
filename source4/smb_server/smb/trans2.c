/* 
   Unix SMB/CIFS implementation.
   transaction2 handling
   Copyright (C) Andrew Tridgell 2003

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
/*
   This file handles the parsing of transact2 requests
*/

#include "includes.h"
#include "dlinklist.h"
#include "smb_server/smb_server.h"
#include "librpc/gen_ndr/ndr_misc.h"
#include "ntvfs/ntvfs.h"
#include "libcli/raw/libcliraw.h"

#define TRANS2_CHECK_ASYNC_STATUS_SIMPLE do { \
	if (!NT_STATUS_IS_OK(req->ntvfs->async_states->status)) { \
		trans2_setup_reply(trans, 0, 0, 0);\
		return req->ntvfs->async_states->status; \
	} \
} while (0)
#define TRANS2_CHECK_ASYNC_STATUS(ptr, type) do { \
	TRANS2_CHECK_ASYNC_STATUS_SIMPLE; \
	ptr = talloc_get_type(op->op_info, type); \
} while (0)
#define TRANS2_CHECK(cmd) do { \
	NTSTATUS _status; \
	_status = cmd; \
	NT_STATUS_NOT_OK_RETURN(_status); \
} while (0)

/*
  hold the state of a nttrans op while in progress. Needed to allow for async backend
  functions.
*/
struct trans_op {
	struct smbsrv_request *req;
	struct smb_trans2 *trans;
	uint8_t command;
	NTSTATUS (*send_fn)(struct trans_op *);
	void *op_info;
};

#define CHECK_MIN_BLOB_SIZE(blob, size) do { \
	if ((blob)->length < (size)) { \
		return NT_STATUS_INFO_LENGTH_MISMATCH; \
	}} while (0)

/* grow the data size of a trans2 reply */
static NTSTATUS trans2_grow_data(TALLOC_CTX *mem_ctx,
				 DATA_BLOB *blob,
				 uint32_t new_size)
{
	if (new_size > blob->length) {
		blob->data = talloc_realloc(mem_ctx, blob->data, uint8_t, new_size);
		NT_STATUS_HAVE_NO_MEMORY(blob->data);
	}
	blob->length = new_size;
	return NT_STATUS_OK;
}

/* grow the data, zero filling any new bytes */
static NTSTATUS trans2_grow_data_fill(TALLOC_CTX *mem_ctx,
				      DATA_BLOB *blob,
				      uint32_t new_size)
{
	uint32_t old_size = blob->length;
	TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, new_size));
	if (new_size > old_size) {
		memset(blob->data + old_size, 0, new_size - old_size);
	}
	return NT_STATUS_OK;
}


/* setup a trans2 reply, given the data and params sizes */
static NTSTATUS trans2_setup_reply(struct smb_trans2 *trans,
				   uint16_t param_size, uint16_t data_size,
				   uint16_t setup_count)
{
	trans->out.setup_count = setup_count;
	if (setup_count > 0) {
		trans->out.setup = talloc_zero_array(trans, uint16_t, setup_count);
		NT_STATUS_HAVE_NO_MEMORY(trans->out.setup);
	}
	trans->out.params = data_blob_talloc(trans, NULL, param_size);
	if (param_size > 0) NT_STATUS_HAVE_NO_MEMORY(trans->out.params.data);

	trans->out.data = data_blob_talloc(trans, NULL, data_size);
	if (data_size > 0) NT_STATUS_HAVE_NO_MEMORY(trans->out.data.data);

	return NT_STATUS_OK;
}


/*
  pull a string from a blob in a trans2 request
*/
static size_t trans2_pull_blob_string(struct smbsrv_request *req, 
				      const DATA_BLOB *blob,
				      uint16_t offset,
				      const char **str,
				      int flags)
{
	/* we use STR_NO_RANGE_CHECK because the params are allocated
	   separately in a DATA_BLOB, so we need to do our own range
	   checking */
	if (offset >= blob->length) {
		*str = NULL;
		return 0;
	}
	
	return req_pull_string(req, str, 
			       blob->data + offset, 
			       blob->length - offset,
			       STR_NO_RANGE_CHECK | flags);
}

/*
  push a string into the data section of a trans2 request
  return the number of bytes consumed in the output
*/
static size_t trans2_push_data_string(struct smbsrv_request *req,
				      TALLOC_CTX *mem_ctx,
				      DATA_BLOB *blob,
				      uint32_t len_offset,
				      uint32_t offset,
				      const struct smb_wire_string *str,
				      int dest_len,
				      int flags)
{
	int alignment = 0, ret = 0, pkt_len;

	/* we use STR_NO_RANGE_CHECK because the params are allocated
	   separately in a DATA_BLOB, so we need to do our own range
	   checking */
	if (!str->s || offset >= blob->length) {
		if (flags & STR_LEN8BIT) {
			SCVAL(blob->data, len_offset, 0);
		} else {
			SIVAL(blob->data, len_offset, 0);
		}
		return 0;
	}

	flags |= STR_NO_RANGE_CHECK;

	if (dest_len == -1 || (dest_len > blob->length - offset)) {
		dest_len = blob->length - offset;
	}

	if (!(flags & (STR_ASCII|STR_UNICODE))) {
		flags |= (req->flags2 & FLAGS2_UNICODE_STRINGS) ? STR_UNICODE : STR_ASCII;
	}

	if ((offset&1) && (flags & STR_UNICODE) && !(flags & STR_NOALIGN)) {
		alignment = 1;
		if (dest_len > 0) {
			SCVAL(blob->data + offset, 0, 0);
			ret = push_string(blob->data + offset + 1, str->s, dest_len-1, flags);
		}
	} else {
		ret = push_string(blob->data + offset, str->s, dest_len, flags);
	}

	/* sometimes the string needs to be terminated, but the length
	   on the wire must not include the termination! */
	pkt_len = ret;

	if ((flags & STR_LEN_NOTERM) && (flags & STR_TERMINATE)) {
		if ((flags & STR_UNICODE) && ret >= 2) {
			pkt_len = ret-2;
		}
		if ((flags & STR_ASCII) && ret >= 1) {
			pkt_len = ret-1;
		}
	}

	if (flags & STR_LEN8BIT) {
		SCVAL(blob->data, len_offset, pkt_len);
	} else {
		SIVAL(blob->data, len_offset, pkt_len);
	}

	return ret + alignment;
}

/*
  append a string to the data section of a trans2 reply
  len_offset points to the place in the packet where the length field
  should go
*/
static NTSTATUS trans2_append_data_string(struct smbsrv_request *req, 
					  TALLOC_CTX *mem_ctx,
					  DATA_BLOB *blob,
					  const struct smb_wire_string *str,
					  uint_t len_offset,
					  int flags)
{
	size_t ret;
	uint32_t offset;
	const int max_bytes_per_char = 3;

	offset = blob->length;
	TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, offset + (2+strlen_m(str->s))*max_bytes_per_char));
	ret = trans2_push_data_string(req, mem_ctx, blob, len_offset, offset, str, -1, flags);
	if (ret < 0) {
		return NT_STATUS_FOOBAR;
	}
	TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, offset + ret));
	return NT_STATUS_OK;
}

/*
  align the end of the data section of a trans reply on an even boundary
*/
static NTSTATUS trans2_align_data(struct smb_trans2 *trans)
{
	if (trans->out.data.length & 1) {
		TRANS2_CHECK(trans2_grow_data_fill(trans, &trans->out.data, trans->out.data.length+1));
	}
	return NT_STATUS_OK;
}

static NTSTATUS trans2_push_fsinfo(struct smbsrv_request *req,
				   union smb_fsinfo *fsinfo,
				   TALLOC_CTX *mem_ctx,
				   DATA_BLOB *blob)
{
	uint_t i;
	DATA_BLOB guid_blob;

	switch (fsinfo->generic.level) {
	case SMB_QFS_ALLOCATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 18));

		SIVAL(blob->data,  0, fsinfo->allocation.out.fs_id);
		SIVAL(blob->data,  4, fsinfo->allocation.out.sectors_per_unit);
		SIVAL(blob->data,  8, fsinfo->allocation.out.total_alloc_units);
		SIVAL(blob->data, 12, fsinfo->allocation.out.avail_alloc_units);
		SSVAL(blob->data, 16, fsinfo->allocation.out.bytes_per_sector);

		return NT_STATUS_OK;

	case SMB_QFS_VOLUME:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 5));

		SIVAL(blob->data,       0, fsinfo->volume.out.serial_number);
		/* w2k3 implements this incorrectly for unicode - it
		 * leaves the last byte off the string */
		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
						       &fsinfo->volume.out.volume_name, 
						       4, STR_LEN8BIT|STR_NOALIGN));

		return NT_STATUS_OK;

	case SMB_QFS_VOLUME_INFO:
	case SMB_QFS_VOLUME_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 18));

		push_nttime(blob->data, 0, fsinfo->volume_info.out.create_time);
		SIVAL(blob->data,       8, fsinfo->volume_info.out.serial_number);
		SSVAL(blob->data,      16, 0); /* padding */
		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
						       &fsinfo->volume_info.out.volume_name, 
						       12, STR_UNICODE));

		return NT_STATUS_OK;

	case SMB_QFS_SIZE_INFO:
	case SMB_QFS_SIZE_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 24));

		SBVAL(blob->data,  0, fsinfo->size_info.out.total_alloc_units);
		SBVAL(blob->data,  8, fsinfo->size_info.out.avail_alloc_units);
		SIVAL(blob->data, 16, fsinfo->size_info.out.sectors_per_unit);
		SIVAL(blob->data, 20, fsinfo->size_info.out.bytes_per_sector);

		return NT_STATUS_OK;

	case SMB_QFS_DEVICE_INFO:
	case SMB_QFS_DEVICE_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 8));

		SIVAL(blob->data,      0, fsinfo->device_info.out.device_type);
		SIVAL(blob->data,      4, fsinfo->device_info.out.characteristics);

		return NT_STATUS_OK;

	case SMB_QFS_ATTRIBUTE_INFO:
	case SMB_QFS_ATTRIBUTE_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 12));

		SIVAL(blob->data, 0, fsinfo->attribute_info.out.fs_attr);
		SIVAL(blob->data, 4, fsinfo->attribute_info.out.max_file_component_length);
		/* this must not be null terminated or win98 gets
		   confused!  also note that w2k3 returns this as
		   unicode even when ascii is negotiated */
		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
						       &fsinfo->attribute_info.out.fs_type,
						       8, STR_UNICODE));
		return NT_STATUS_OK;


	case SMB_QFS_QUOTA_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 48));

		SBVAL(blob->data,   0, fsinfo->quota_information.out.unknown[0]);
		SBVAL(blob->data,   8, fsinfo->quota_information.out.unknown[1]);
		SBVAL(blob->data,  16, fsinfo->quota_information.out.unknown[2]);
		SBVAL(blob->data,  24, fsinfo->quota_information.out.quota_soft);
		SBVAL(blob->data,  32, fsinfo->quota_information.out.quota_hard);
		SBVAL(blob->data,  40, fsinfo->quota_information.out.quota_flags);

		return NT_STATUS_OK;


	case SMB_QFS_FULL_SIZE_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 32));

		SBVAL(blob->data,  0, fsinfo->full_size_information.out.total_alloc_units);
		SBVAL(blob->data,  8, fsinfo->full_size_information.out.call_avail_alloc_units);
		SBVAL(blob->data, 16, fsinfo->full_size_information.out.actual_avail_alloc_units);
		SIVAL(blob->data, 24, fsinfo->full_size_information.out.sectors_per_unit);
		SIVAL(blob->data, 28, fsinfo->full_size_information.out.bytes_per_sector);

		return NT_STATUS_OK;

	case SMB_QFS_OBJECTID_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 64));

		TRANS2_CHECK(ndr_push_struct_blob(&guid_blob, mem_ctx, 
						  &fsinfo->objectid_information.out.guid,
						  (ndr_push_flags_fn_t)ndr_push_GUID));
		memcpy(blob->data, guid_blob.data, guid_blob.length);

		for (i=0;i<6;i++) {
			SBVAL(blob->data, 16 + 8*i, fsinfo->objectid_information.out.unknown[i]);
		}

		return NT_STATUS_OK;
	}

	return NT_STATUS_INVALID_LEVEL;
}

/*
  trans2 qfsinfo implementation send
*/
static NTSTATUS trans2_qfsinfo_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;
	union smb_fsinfo *fsinfo;

	TRANS2_CHECK_ASYNC_STATUS(fsinfo, union smb_fsinfo);

	TRANS2_CHECK(trans2_setup_reply(trans, 0, 0, 0));

	TRANS2_CHECK(trans2_push_fsinfo(req, fsinfo, trans, &trans->out.data));

	return NT_STATUS_OK;
}

/*
  trans2 qfsinfo implementation
*/
static NTSTATUS trans2_qfsinfo(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_fsinfo *fsinfo;
	uint16_t level;

	/* make sure we got enough parameters */
	if (trans->in.params.length != 2) {
		return NT_STATUS_FOOBAR;
	}

	fsinfo = talloc(op, union smb_fsinfo);
	NT_STATUS_HAVE_NO_MEMORY(fsinfo);

	op->op_info = fsinfo;
	op->send_fn = trans2_qfsinfo_send;

	level = SVAL(trans->in.params.data, 0);

	switch (level) {
	case SMB_QFS_ALLOCATION:
		fsinfo->allocation.level = RAW_QFS_ALLOCATION;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_VOLUME:
		fsinfo->volume.level = RAW_QFS_VOLUME;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_VOLUME_INFO:
	case SMB_QFS_VOLUME_INFORMATION:
		fsinfo->volume_info.level = RAW_QFS_VOLUME_INFO;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_SIZE_INFO:
	case SMB_QFS_SIZE_INFORMATION:
		fsinfo->size_info.level = RAW_QFS_SIZE_INFO;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_DEVICE_INFO:
	case SMB_QFS_DEVICE_INFORMATION:
		fsinfo->device_info.level = RAW_QFS_DEVICE_INFO;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_ATTRIBUTE_INFO:
	case SMB_QFS_ATTRIBUTE_INFORMATION:
		fsinfo->attribute_info.level = RAW_QFS_ATTRIBUTE_INFO;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_QUOTA_INFORMATION:
		fsinfo->quota_information.level = RAW_QFS_QUOTA_INFORMATION;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_FULL_SIZE_INFORMATION:
		fsinfo->full_size_information.level = RAW_QFS_FULL_SIZE_INFORMATION;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);

	case SMB_QFS_OBJECTID_INFORMATION:
		fsinfo->objectid_information.level = RAW_QFS_OBJECTID_INFORMATION;
		return ntvfs_fsinfo(req->ntvfs, fsinfo);
	}

	return NT_STATUS_INVALID_LEVEL;
}


/*
  trans2 open implementation send
*/
static NTSTATUS trans2_open_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;
	union smb_open *io;

	TRANS2_CHECK_ASYNC_STATUS(io, union smb_open);

	trans2_setup_reply(trans, 30, 0, 0);

	smbsrv_push_fnum(trans->out.params.data, VWV(0), io->t2open.out.file.ntvfs);
	SSVAL(trans->out.params.data, VWV(1), io->t2open.out.attrib);
	srv_push_dos_date3(req->smb_conn, trans->out.params.data, 
			   VWV(2), io->t2open.out.write_time);
	SIVAL(trans->out.params.data, VWV(4), io->t2open.out.size);
	SSVAL(trans->out.params.data, VWV(6), io->t2open.out.access);
	SSVAL(trans->out.params.data, VWV(7), io->t2open.out.ftype);
	SSVAL(trans->out.params.data, VWV(8), io->t2open.out.devstate);
	SSVAL(trans->out.params.data, VWV(9), io->t2open.out.action);
	SIVAL(trans->out.params.data, VWV(10), 0); /* reserved */
	SSVAL(trans->out.params.data, VWV(12), 0); /* EaErrorOffset */
	SIVAL(trans->out.params.data, VWV(13), 0); /* EaLength */

	return NT_STATUS_OK;
}

/*
  trans2 open implementation
*/
static NTSTATUS trans2_open(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_open *io;
	NTSTATUS status;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 29) {
		return NT_STATUS_FOOBAR;
	}

	io = talloc(op, union smb_open);
	NT_STATUS_HAVE_NO_MEMORY(io);

	io->t2open.level           = RAW_OPEN_T2OPEN;
	io->t2open.in.flags        = SVAL(trans->in.params.data, VWV(0));
	io->t2open.in.open_mode    = SVAL(trans->in.params.data, VWV(1));
	io->t2open.in.search_attrs = SVAL(trans->in.params.data, VWV(2));
	io->t2open.in.file_attrs   = SVAL(trans->in.params.data, VWV(3));
	io->t2open.in.write_time   = srv_pull_dos_date(req->smb_conn, 
						    trans->in.params.data + VWV(4));;
	io->t2open.in.open_func    = SVAL(trans->in.params.data, VWV(6));
	io->t2open.in.size         = IVAL(trans->in.params.data, VWV(7));
	io->t2open.in.timeout      = IVAL(trans->in.params.data, VWV(9));
	io->t2open.in.num_eas      = 0;
	io->t2open.in.eas          = NULL;

	trans2_pull_blob_string(req, &trans->in.params, 28, &io->t2open.in.fname, 0);

	status = ea_pull_list(&trans->in.data, io, &io->t2open.in.num_eas, &io->t2open.in.eas);
	NT_STATUS_NOT_OK_RETURN(status);

	op->op_info = io;
	op->send_fn = trans2_open_send;

	return ntvfs_open(req->ntvfs, io);
}


/*
  trans2 simple send
*/
static NTSTATUS trans2_simple_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;

	TRANS2_CHECK_ASYNC_STATUS_SIMPLE;

	trans2_setup_reply(trans, 2, 0, 0);

	SSVAL(trans->out.params.data, VWV(0), 0);

	return NT_STATUS_OK;
}

/*
  trans2 mkdir implementation
*/
static NTSTATUS trans2_mkdir(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_mkdir *io;
	NTSTATUS status;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 5) {
		return NT_STATUS_FOOBAR;
	}

	io = talloc(op, union smb_mkdir);
	NT_STATUS_HAVE_NO_MEMORY(io);

	io->t2mkdir.level = RAW_MKDIR_T2MKDIR;
	trans2_pull_blob_string(req, &trans->in.params, 4, &io->t2mkdir.in.path, 0);

	status = ea_pull_list(&trans->in.data, io, 
			      &io->t2mkdir.in.num_eas, 
			      &io->t2mkdir.in.eas);
	NT_STATUS_NOT_OK_RETURN(status);

	op->op_info = io;
	op->send_fn = trans2_simple_send;

	return ntvfs_mkdir(req->ntvfs, io);
}

static NTSTATUS trans2_push_fileinfo(struct smbsrv_request *req,
				     union smb_fileinfo *st,
				     TALLOC_CTX *mem_ctx,
				     DATA_BLOB *blob)
{
	uint_t i;
	uint32_t list_size;

	switch (st->generic.level) {
	case RAW_FILEINFO_GENERIC:
	case RAW_FILEINFO_GETATTR:
	case RAW_FILEINFO_GETATTRE:
	case RAW_FILEINFO_SEC_DESC:
		/* handled elsewhere */
		return NT_STATUS_INVALID_LEVEL;

	case RAW_FILEINFO_BASIC_INFO:
	case RAW_FILEINFO_BASIC_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 40));

		push_nttime(blob->data,  0, st->basic_info.out.create_time);
		push_nttime(blob->data,  8, st->basic_info.out.access_time);
		push_nttime(blob->data, 16, st->basic_info.out.write_time);
		push_nttime(blob->data, 24, st->basic_info.out.change_time);
		SIVAL(blob->data,       32, st->basic_info.out.attrib);
		SIVAL(blob->data,       36, 0); /* padding */
		return NT_STATUS_OK;

	case RAW_FILEINFO_STANDARD:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 22));

		srv_push_dos_date2(req->smb_conn, blob->data, 0, st->standard.out.create_time);
		srv_push_dos_date2(req->smb_conn, blob->data, 4, st->standard.out.access_time);
		srv_push_dos_date2(req->smb_conn, blob->data, 8, st->standard.out.write_time);
		SIVAL(blob->data,        12, st->standard.out.size);
		SIVAL(blob->data,        16, st->standard.out.alloc_size);
		SSVAL(blob->data,        20, st->standard.out.attrib);
		return NT_STATUS_OK;

	case RAW_FILEINFO_EA_SIZE:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 26));

		srv_push_dos_date2(req->smb_conn, blob->data, 0, st->ea_size.out.create_time);
		srv_push_dos_date2(req->smb_conn, blob->data, 4, st->ea_size.out.access_time);
		srv_push_dos_date2(req->smb_conn, blob->data, 8, st->ea_size.out.write_time);
		SIVAL(blob->data,        12, st->ea_size.out.size);
		SIVAL(blob->data,        16, st->ea_size.out.alloc_size);
		SSVAL(blob->data,        20, st->ea_size.out.attrib);
		SIVAL(blob->data,        22, st->ea_size.out.ea_size);
		return NT_STATUS_OK;

	case RAW_FILEINFO_NETWORK_OPEN_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 56));

		push_nttime(blob->data,  0, st->network_open_information.out.create_time);
		push_nttime(blob->data,  8, st->network_open_information.out.access_time);
		push_nttime(blob->data, 16, st->network_open_information.out.write_time);
		push_nttime(blob->data, 24, st->network_open_information.out.change_time);
		SBVAL(blob->data,       32, st->network_open_information.out.alloc_size);
		SBVAL(blob->data,       40, st->network_open_information.out.size);
		SIVAL(blob->data,       48, st->network_open_information.out.attrib);
		SIVAL(blob->data,       52, 0); /* padding */
		return NT_STATUS_OK;

	case RAW_FILEINFO_STANDARD_INFO:
	case RAW_FILEINFO_STANDARD_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 24));

		SBVAL(blob->data,  0, st->standard_info.out.alloc_size);
		SBVAL(blob->data,  8, st->standard_info.out.size);
		SIVAL(blob->data, 16, st->standard_info.out.nlink);
		SCVAL(blob->data, 20, st->standard_info.out.delete_pending);
		SCVAL(blob->data, 21, st->standard_info.out.directory);
		SSVAL(blob->data, 22, 0); /* padding */
		return NT_STATUS_OK;

	case RAW_FILEINFO_ATTRIBUTE_TAG_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 8));

		SIVAL(blob->data,  0, st->attribute_tag_information.out.attrib);
		SIVAL(blob->data,  4, st->attribute_tag_information.out.reparse_tag);
		return NT_STATUS_OK;

	case RAW_FILEINFO_EA_INFO:
	case RAW_FILEINFO_EA_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		SIVAL(blob->data,  0, st->ea_info.out.ea_size);
		return NT_STATUS_OK;

	case RAW_FILEINFO_MODE_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		SIVAL(blob->data,  0, st->mode_information.out.mode);
		return NT_STATUS_OK;

	case RAW_FILEINFO_ALIGNMENT_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		SIVAL(blob->data,  0, 
		      st->alignment_information.out.alignment_requirement);
		return NT_STATUS_OK;

	case RAW_FILEINFO_EA_LIST:
		list_size = ea_list_size(st->ea_list.out.num_eas,
					 st->ea_list.out.eas);
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, list_size));

		ea_put_list(blob->data, 
			    st->ea_list.out.num_eas, st->ea_list.out.eas);
		return NT_STATUS_OK;

	case RAW_FILEINFO_ALL_EAS:
		list_size = ea_list_size(st->all_eas.out.num_eas,
						  st->all_eas.out.eas);
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, list_size));

		ea_put_list(blob->data, 
			    st->all_eas.out.num_eas, st->all_eas.out.eas);
		return NT_STATUS_OK;

	case RAW_FILEINFO_ACCESS_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		SIVAL(blob->data,  0, st->access_information.out.access_flags);
		return NT_STATUS_OK;

	case RAW_FILEINFO_POSITION_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 8));

		SBVAL(blob->data,  0, st->position_information.out.position);
		return NT_STATUS_OK;

	case RAW_FILEINFO_COMPRESSION_INFO:
	case RAW_FILEINFO_COMPRESSION_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 16));

		SBVAL(blob->data,  0, st->compression_info.out.compressed_size);
		SSVAL(blob->data,  8, st->compression_info.out.format);
		SCVAL(blob->data, 10, st->compression_info.out.unit_shift);
		SCVAL(blob->data, 11, st->compression_info.out.chunk_shift);
		SCVAL(blob->data, 12, st->compression_info.out.cluster_shift);
		SSVAL(blob->data, 13, 0); /* 3 bytes padding */
		SCVAL(blob->data, 15, 0);
		return NT_STATUS_OK;

	case RAW_FILEINFO_IS_NAME_VALID:
		return NT_STATUS_OK;

	case RAW_FILEINFO_INTERNAL_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 8));

		SBVAL(blob->data,  0, st->internal_information.out.file_id);
		return NT_STATUS_OK;

	case RAW_FILEINFO_ALL_INFO:
	case RAW_FILEINFO_ALL_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 72));

		push_nttime(blob->data,  0, st->all_info.out.create_time);
		push_nttime(blob->data,  8, st->all_info.out.access_time);
		push_nttime(blob->data, 16, st->all_info.out.write_time);
		push_nttime(blob->data, 24, st->all_info.out.change_time);
		SIVAL(blob->data,       32, st->all_info.out.attrib);
		SIVAL(blob->data,       36, 0);
		SBVAL(blob->data,       40, st->all_info.out.alloc_size);
		SBVAL(blob->data,       48, st->all_info.out.size);
		SIVAL(blob->data,       56, st->all_info.out.nlink);
		SCVAL(blob->data,       60, st->all_info.out.delete_pending);
		SCVAL(blob->data,       61, st->all_info.out.directory);
		SSVAL(blob->data,       62, 0); /* padding */
		SIVAL(blob->data,       64, st->all_info.out.ea_size);
		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
						       &st->all_info.out.fname,
						       68, STR_UNICODE));
		return NT_STATUS_OK;

	case RAW_FILEINFO_NAME_INFO:
	case RAW_FILEINFO_NAME_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
						       &st->name_info.out.fname,
						       0, STR_UNICODE));
		return NT_STATUS_OK;

	case RAW_FILEINFO_ALT_NAME_INFO:
	case RAW_FILEINFO_ALT_NAME_INFORMATION:
		TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, 4));

		TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob, 
						       &st->alt_name_info.out.fname,
						       0, STR_UNICODE));
		return NT_STATUS_OK;

	case RAW_FILEINFO_STREAM_INFO:
	case RAW_FILEINFO_STREAM_INFORMATION:
		for (i=0;i<st->stream_info.out.num_streams;i++) {
			uint32_t data_size = blob->length;
			uint8_t *data;

			TRANS2_CHECK(trans2_grow_data(mem_ctx, blob, data_size + 24));
			data = blob->data + data_size;
			SBVAL(data,  8, st->stream_info.out.streams[i].size);
			SBVAL(data, 16, st->stream_info.out.streams[i].alloc_size);
			TRANS2_CHECK(trans2_append_data_string(req, mem_ctx, blob,
							       &st->stream_info.out.streams[i].stream_name,
							       data_size + 4, STR_UNICODE));
			if (i == st->stream_info.out.num_streams - 1) {
				SIVAL(blob->data, data_size, 0);
			} else {
				TRANS2_CHECK(trans2_grow_data_fill(mem_ctx, blob, (blob->length+7)&~7));
				SIVAL(blob->data, data_size, 
				      blob->length - data_size);
			}
		}
		return NT_STATUS_OK;
		
	case RAW_FILEINFO_UNIX_BASIC:
	case RAW_FILEINFO_UNIX_LINK:
		return NT_STATUS_INVALID_LEVEL;

	case RAW_FILEINFO_SMB2_ALL_EAS:
	case RAW_FILEINFO_SMB2_ALL_INFORMATION:
		return NT_STATUS_INVALID_LEVEL;
	}

	return NT_STATUS_INVALID_LEVEL;
}

/*
  fill in the reply from a qpathinfo or qfileinfo call
*/
static NTSTATUS trans2_fileinfo_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;
	union smb_fileinfo *st;

	TRANS2_CHECK_ASYNC_STATUS(st, union smb_fileinfo);

	TRANS2_CHECK(trans2_setup_reply(trans, 2, 0, 0));
	SSVAL(trans->out.params.data, 0, 0);

	TRANS2_CHECK(trans2_push_fileinfo(req, st, trans, &trans->out.data));

	return NT_STATUS_OK;
}

/*
  trans2 qpathinfo implementation
*/
static NTSTATUS trans2_qpathinfo(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_fileinfo *st;
	NTSTATUS status;
	uint16_t level;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 2) {
		return NT_STATUS_FOOBAR;
	}

	st = talloc(op, union smb_fileinfo);
	NT_STATUS_HAVE_NO_MEMORY(st);

	level = SVAL(trans->in.params.data, 0);

	trans2_pull_blob_string(req, &trans->in.params, 6, &st->generic.in.file.path, 0);
	if (st->generic.in.file.path == NULL) {
		return NT_STATUS_FOOBAR;
	}

	/* work out the backend level - we make it 1-1 in the header */
	st->generic.level = (enum smb_fileinfo_level)level;
	if (st->generic.level >= RAW_FILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	if (st->generic.level == RAW_FILEINFO_EA_LIST) {
		status = ea_pull_name_list(&trans->in.data, req, 
					   &st->ea_list.in.num_names,
					   &st->ea_list.in.ea_names);
		NT_STATUS_NOT_OK_RETURN(status);
	}

	op->op_info = st;
	op->send_fn = trans2_fileinfo_send;

	return ntvfs_qpathinfo(req->ntvfs, st);
}


/*
  trans2 qpathinfo implementation
*/
static NTSTATUS trans2_qfileinfo(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_fileinfo *st;
	NTSTATUS status;
	uint16_t level;
	struct ntvfs_handle *h;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 4) {
		return NT_STATUS_FOOBAR;
	}

	st = talloc(op, union smb_fileinfo);
	NT_STATUS_HAVE_NO_MEMORY(st);

	h     = smbsrv_pull_fnum(req, trans->in.params.data, 0);
	level = SVAL(trans->in.params.data, 2);

	st->generic.in.file.ntvfs = h;
	/* work out the backend level - we make it 1-1 in the header */
	st->generic.level = (enum smb_fileinfo_level)level;
	if (st->generic.level >= RAW_FILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	if (st->generic.level == RAW_FILEINFO_EA_LIST) {
		status = ea_pull_name_list(&trans->in.data, req, 
					   &st->ea_list.in.num_names,
					   &st->ea_list.in.ea_names);
		NT_STATUS_NOT_OK_RETURN(status);
	}

	op->op_info = st;
	op->send_fn = trans2_fileinfo_send;

	SMBSRV_CHECK_FILE_HANDLE_NTSTATUS(st->generic.in.file.ntvfs);
	return ntvfs_qfileinfo(req->ntvfs, st);
}


/*
  parse a trans2 setfileinfo/setpathinfo data blob
*/
static NTSTATUS trans2_parse_sfileinfo(struct smbsrv_request *req,
				       union smb_setfileinfo *st,
				       const DATA_BLOB *blob)
{
	uint32_t len;

	switch (st->generic.level) {
	case RAW_SFILEINFO_GENERIC:
	case RAW_SFILEINFO_SETATTR:
	case RAW_SFILEINFO_SETATTRE:
	case RAW_SFILEINFO_SEC_DESC:
		/* handled elsewhere */
		return NT_STATUS_INVALID_LEVEL;

	case RAW_SFILEINFO_STANDARD:
		CHECK_MIN_BLOB_SIZE(blob, 12);
		st->standard.in.create_time = srv_pull_dos_date2(req->smb_conn, blob->data + 0);
		st->standard.in.access_time = srv_pull_dos_date2(req->smb_conn, blob->data + 4);
		st->standard.in.write_time  = srv_pull_dos_date2(req->smb_conn, blob->data + 8);
		return NT_STATUS_OK;

	case RAW_SFILEINFO_EA_SET:
		return ea_pull_list(blob, req, 
				    &st->ea_set.in.num_eas, 
				    &st->ea_set.in.eas);

	case SMB_SFILEINFO_BASIC_INFO:
	case SMB_SFILEINFO_BASIC_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 36);
		st->basic_info.in.create_time = pull_nttime(blob->data,  0);
		st->basic_info.in.access_time = pull_nttime(blob->data,  8);
		st->basic_info.in.write_time =  pull_nttime(blob->data, 16);
		st->basic_info.in.change_time = pull_nttime(blob->data, 24);
		st->basic_info.in.attrib =      IVAL(blob->data,        32);
		return NT_STATUS_OK;

	case SMB_SFILEINFO_DISPOSITION_INFO:
	case SMB_SFILEINFO_DISPOSITION_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 1);
		st->disposition_info.in.delete_on_close = CVAL(blob->data, 0);
		return NT_STATUS_OK;

	case SMB_SFILEINFO_ALLOCATION_INFO:
	case SMB_SFILEINFO_ALLOCATION_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 8);
		st->allocation_info.in.alloc_size = BVAL(blob->data, 0);
		return NT_STATUS_OK;				

	case RAW_SFILEINFO_END_OF_FILE_INFO:
	case RAW_SFILEINFO_END_OF_FILE_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 8);
		st->end_of_file_info.in.size = BVAL(blob->data, 0);
		return NT_STATUS_OK;

	case RAW_SFILEINFO_RENAME_INFORMATION: {
		DATA_BLOB blob2;

		CHECK_MIN_BLOB_SIZE(blob, 12);
		st->rename_information.in.overwrite = CVAL(blob->data, 0);
		st->rename_information.in.root_fid  = IVAL(blob->data, 4);
		len                                 = IVAL(blob->data, 8);
		blob2.data = blob->data+12;
		blob2.length = MIN(blob->length, len);
		trans2_pull_blob_string(req, &blob2, 0, 
					&st->rename_information.in.new_name, STR_UNICODE);
		return NT_STATUS_OK;
	}

	case RAW_SFILEINFO_POSITION_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 8);
		st->position_information.in.position = BVAL(blob->data, 0);
		return NT_STATUS_OK;

	case RAW_SFILEINFO_MODE_INFORMATION:
		CHECK_MIN_BLOB_SIZE(blob, 4);
		st->mode_information.in.mode = IVAL(blob->data, 0);
		return NT_STATUS_OK;

	case RAW_SFILEINFO_UNIX_BASIC:
	case RAW_SFILEINFO_UNIX_LINK:
	case RAW_SFILEINFO_UNIX_HLINK:
	case RAW_SFILEINFO_1023:
	case RAW_SFILEINFO_1025:
	case RAW_SFILEINFO_1029:
	case RAW_SFILEINFO_1032:
	case RAW_SFILEINFO_1039:
	case RAW_SFILEINFO_1040:
		return NT_STATUS_INVALID_LEVEL;
	}

	return NT_STATUS_INVALID_LEVEL;
}

/*
  trans2 setfileinfo implementation
*/
static NTSTATUS trans2_setfileinfo(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_setfileinfo *st;
	NTSTATUS status;
	uint16_t level;
	struct ntvfs_handle *h;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 4) {
		return NT_STATUS_FOOBAR;
	}

	st = talloc(op, union smb_setfileinfo);
	NT_STATUS_HAVE_NO_MEMORY(st);

	h     = smbsrv_pull_fnum(req, trans->in.params.data, 0);
	level = SVAL(trans->in.params.data, 2);

	st->generic.in.file.ntvfs = h;
	/* work out the backend level - we make it 1-1 in the header */
	st->generic.level = (enum smb_setfileinfo_level)level;
	if (st->generic.level >= RAW_SFILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	status = trans2_parse_sfileinfo(req, st, &trans->in.data);
	NT_STATUS_NOT_OK_RETURN(status);

	op->op_info = st;
	op->send_fn = trans2_simple_send;

	SMBSRV_CHECK_FILE_HANDLE_NTSTATUS(st->generic.in.file.ntvfs);
	return ntvfs_setfileinfo(req->ntvfs, st);
}

/*
  trans2 setpathinfo implementation
*/
static NTSTATUS trans2_setpathinfo(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_setfileinfo *st;
	NTSTATUS status;
	uint16_t level;

	/* make sure we got enough parameters */
	if (trans->in.params.length < 4) {
		return NT_STATUS_FOOBAR;
	}

	st = talloc(op, union smb_setfileinfo);
	NT_STATUS_HAVE_NO_MEMORY(st);

	level = SVAL(trans->in.params.data, 0);

	trans2_pull_blob_string(req, &trans->in.params, 6, &st->generic.in.file.path, 0);
	if (st->generic.in.file.path == NULL) {
		return NT_STATUS_FOOBAR;
	}

	/* work out the backend level - we make it 1-1 in the header */
	st->generic.level = (enum smb_setfileinfo_level)level;
	if (st->generic.level >= RAW_SFILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	status = trans2_parse_sfileinfo(req, st, &trans->in.data);
	NT_STATUS_NOT_OK_RETURN(status);

	op->op_info = st;
	op->send_fn = trans2_simple_send;

	return ntvfs_setpathinfo(req->ntvfs, st);
}


/* a structure to encapsulate the state information about an in-progress ffirst/fnext operation */
struct find_state {
	struct trans_op *op;
	void *search;
	enum smb_search_level level;
	uint16_t last_entry_offset;
	uint16_t flags;
};

/*
  fill a single entry in a trans2 find reply 
*/
static BOOL find_fill_info(struct find_state *state,
			   union smb_search_data *file)
{
	struct smbsrv_request *req = state->op->req;
	struct smb_trans2 *trans = state->op->trans;
	uint8_t *data;
	uint_t ofs = trans->out.data.length;
	uint32_t ea_size;

	switch (state->level) {
	case RAW_SEARCH_SEARCH:
	case RAW_SEARCH_FFIRST:
	case RAW_SEARCH_FUNIQUE:
	case RAW_SEARCH_GENERIC:
	case RAW_SEARCH_SMB2:
		/* handled elsewhere */
		return False;

	case RAW_SEARCH_STANDARD:
		if (state->flags & FLAG_TRANS2_FIND_REQUIRE_RESUME) {
			trans2_grow_data(trans, &trans->out.data, ofs + 27);
			SIVAL(trans->out.data.data, ofs, file->standard.resume_key);
			ofs += 4;
		} else {
			trans2_grow_data(trans, &trans->out.data, ofs + 23);
		}
		data = trans->out.data.data + ofs;
		srv_push_dos_date2(req->smb_conn, data, 0, file->standard.create_time);
		srv_push_dos_date2(req->smb_conn, data, 4, file->standard.access_time);
		srv_push_dos_date2(req->smb_conn, data, 8, file->standard.write_time);
		SIVAL(data, 12, file->standard.size);
		SIVAL(data, 16, file->standard.alloc_size);
		SSVAL(data, 20, file->standard.attrib);
		trans2_append_data_string(req, trans, &trans->out.data, &file->standard.name, 
					  ofs + 22, STR_LEN8BIT | STR_TERMINATE | STR_LEN_NOTERM);
		break;

	case RAW_SEARCH_EA_SIZE:
		if (state->flags & FLAG_TRANS2_FIND_REQUIRE_RESUME) {
			trans2_grow_data(trans, &trans->out.data, ofs + 31);
			SIVAL(trans->out.data.data, ofs, file->ea_size.resume_key);
			ofs += 4;
		} else {
			trans2_grow_data(trans, &trans->out.data, ofs + 27);
		}
		data = trans->out.data.data + ofs;
		srv_push_dos_date2(req->smb_conn, data, 0, file->ea_size.create_time);
		srv_push_dos_date2(req->smb_conn, data, 4, file->ea_size.access_time);
		srv_push_dos_date2(req->smb_conn, data, 8, file->ea_size.write_time);
		SIVAL(data, 12, file->ea_size.size);
		SIVAL(data, 16, file->ea_size.alloc_size);
		SSVAL(data, 20, file->ea_size.attrib);
		SIVAL(data, 22, file->ea_size.ea_size);
		trans2_append_data_string(req, trans, &trans->out.data, &file->ea_size.name, 
					  ofs + 26, STR_LEN8BIT | STR_NOALIGN);
		trans2_grow_data(trans, &trans->out.data, trans->out.data.length + 1);
		trans->out.data.data[trans->out.data.length-1] = 0;
		break;

	case RAW_SEARCH_EA_LIST:
		ea_size = ea_list_size(file->ea_list.eas.num_eas, file->ea_list.eas.eas);
		if (state->flags & FLAG_TRANS2_FIND_REQUIRE_RESUME) {
			if (!NT_STATUS_IS_OK(trans2_grow_data(trans, &trans->out.data, ofs + 27 + ea_size))) {
				return False;
			}
			SIVAL(trans->out.data.data, ofs, file->ea_list.resume_key);
			ofs += 4;
		} else {
			if (!NT_STATUS_IS_OK(trans2_grow_data(trans, &trans->out.data, ofs + 23 + ea_size))) {
				return False;
			}
		}
		data = trans->out.data.data + ofs;
		srv_push_dos_date2(req->smb_conn, data, 0, file->ea_list.create_time);
		srv_push_dos_date2(req->smb_conn, data, 4, file->ea_list.access_time);
		srv_push_dos_date2(req->smb_conn, data, 8, file->ea_list.write_time);
		SIVAL(data, 12, file->ea_list.size);
		SIVAL(data, 16, file->ea_list.alloc_size);
		SSVAL(data, 20, file->ea_list.attrib);
		ea_put_list(data+22, file->ea_list.eas.num_eas, file->ea_list.eas.eas);
		trans2_append_data_string(req, trans, &trans->out.data, &file->ea_list.name, 
					  ofs + 22 + ea_size, STR_LEN8BIT | STR_NOALIGN);
		trans2_grow_data(trans, &trans->out.data, trans->out.data.length + 1);
		trans->out.data.data[trans->out.data.length-1] = 0;
		break;

	case RAW_SEARCH_DIRECTORY_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 64);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->directory_info.file_index);
		push_nttime(data,    8, file->directory_info.create_time);
		push_nttime(data,   16, file->directory_info.access_time);
		push_nttime(data,   24, file->directory_info.write_time);
		push_nttime(data,   32, file->directory_info.change_time);
		SBVAL(data,         40, file->directory_info.size);
		SBVAL(data,         48, file->directory_info.alloc_size);
		SIVAL(data,         56, file->directory_info.attrib);
		trans2_append_data_string(req, trans, &trans->out.data, &file->directory_info.name, 
					  ofs + 60, STR_TERMINATE_ASCII);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;

	case RAW_SEARCH_FULL_DIRECTORY_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 68);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->full_directory_info.file_index);
		push_nttime(data,    8, file->full_directory_info.create_time);
		push_nttime(data,   16, file->full_directory_info.access_time);
		push_nttime(data,   24, file->full_directory_info.write_time);
		push_nttime(data,   32, file->full_directory_info.change_time);
		SBVAL(data,         40, file->full_directory_info.size);
		SBVAL(data,         48, file->full_directory_info.alloc_size);
		SIVAL(data,         56, file->full_directory_info.attrib);
		SIVAL(data,         64, file->full_directory_info.ea_size);
		trans2_append_data_string(req, trans, &trans->out.data, &file->full_directory_info.name, 
					  ofs + 60, STR_TERMINATE_ASCII);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;

	case RAW_SEARCH_NAME_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 12);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->name_info.file_index);
		trans2_append_data_string(req, trans, &trans->out.data, &file->name_info.name, 
					  ofs + 8, STR_TERMINATE_ASCII);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;

	case RAW_SEARCH_BOTH_DIRECTORY_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 94);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->both_directory_info.file_index);
		push_nttime(data,    8, file->both_directory_info.create_time);
		push_nttime(data,   16, file->both_directory_info.access_time);
		push_nttime(data,   24, file->both_directory_info.write_time);
		push_nttime(data,   32, file->both_directory_info.change_time);
		SBVAL(data,         40, file->both_directory_info.size);
		SBVAL(data,         48, file->both_directory_info.alloc_size);
		SIVAL(data,         56, file->both_directory_info.attrib);
		SIVAL(data,         64, file->both_directory_info.ea_size);
		SCVAL(data,         69, 0); /* reserved */
		memset(data+70,0,24);
		trans2_push_data_string(req, trans, &trans->out.data, 
					68 + ofs, 70 + ofs, 
					&file->both_directory_info.short_name, 
					24, STR_UNICODE | STR_LEN8BIT);
		trans2_append_data_string(req, trans, &trans->out.data, &file->both_directory_info.name, 
					  ofs + 60, STR_TERMINATE_ASCII);
		trans2_align_data(trans);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;

	case RAW_SEARCH_ID_FULL_DIRECTORY_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 80);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->id_full_directory_info.file_index);
		push_nttime(data,    8, file->id_full_directory_info.create_time);
		push_nttime(data,   16, file->id_full_directory_info.access_time);
		push_nttime(data,   24, file->id_full_directory_info.write_time);
		push_nttime(data,   32, file->id_full_directory_info.change_time);
		SBVAL(data,         40, file->id_full_directory_info.size);
		SBVAL(data,         48, file->id_full_directory_info.alloc_size);
		SIVAL(data,         56, file->id_full_directory_info.attrib);
		SIVAL(data,         64, file->id_full_directory_info.ea_size);
		SIVAL(data,         68, 0); /* padding */
		SBVAL(data,         72, file->id_full_directory_info.file_id);
		trans2_append_data_string(req, trans, &trans->out.data, &file->id_full_directory_info.name, 
					  ofs + 60, STR_TERMINATE_ASCII);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;

	case RAW_SEARCH_ID_BOTH_DIRECTORY_INFO:
		trans2_grow_data(trans, &trans->out.data, ofs + 104);
		data = trans->out.data.data + ofs;
		SIVAL(data,          4, file->id_both_directory_info.file_index);
		push_nttime(data,    8, file->id_both_directory_info.create_time);
		push_nttime(data,   16, file->id_both_directory_info.access_time);
		push_nttime(data,   24, file->id_both_directory_info.write_time);
		push_nttime(data,   32, file->id_both_directory_info.change_time);
		SBVAL(data,         40, file->id_both_directory_info.size);
		SBVAL(data,         48, file->id_both_directory_info.alloc_size);
		SIVAL(data,         56, file->id_both_directory_info.attrib);
		SIVAL(data,         64, file->id_both_directory_info.ea_size);
		SCVAL(data,         69, 0); /* reserved */
		memset(data+70,0,26);
		trans2_push_data_string(req, trans, &trans->out.data, 
					68 + ofs, 70 + ofs, 
					&file->id_both_directory_info.short_name, 
					24, STR_UNICODE | STR_LEN8BIT);
		SBVAL(data,         96, file->id_both_directory_info.file_id);
		trans2_append_data_string(req, trans, &trans->out.data, &file->id_both_directory_info.name, 
					  ofs + 60, STR_TERMINATE_ASCII);
		data = trans->out.data.data + ofs;
		SIVAL(data,          0, trans->out.data.length - ofs);
		break;
	}

	return True;
}

/* callback function for trans2 findfirst/findnext */
static BOOL find_callback(void *private, union smb_search_data *file)
{
	struct find_state *state = talloc_get_type(private, struct find_state);
	struct smb_trans2 *trans = state->op->trans;
	uint_t old_length;

	old_length = trans->out.data.length;

	if (!find_fill_info(state, file) ||
	    trans->out.data.length > trans->in.max_data) {
		/* restore the old length and tell the backend to stop */
		trans2_grow_data(trans, &trans->out.data, old_length);
		return False;
	}

	state->last_entry_offset = old_length;	
	return True;
}

/*
  trans2 findfirst send
 */
static NTSTATUS trans2_findfirst_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;
	union smb_search_first *search;
	struct find_state *state;
	uint8_t *param;

	TRANS2_CHECK_ASYNC_STATUS(state, struct find_state);
	search = talloc_get_type(state->search, union smb_search_first);

	/* fill in the findfirst reply header */
	param = trans->out.params.data;
	SSVAL(param, VWV(0), search->t2ffirst.out.handle);
	SSVAL(param, VWV(1), search->t2ffirst.out.count);
	SSVAL(param, VWV(2), search->t2ffirst.out.end_of_search);
	SSVAL(param, VWV(3), 0);
	SSVAL(param, VWV(4), state->last_entry_offset);

	return NT_STATUS_OK;
}


/*
  trans2 findfirst implementation
*/
static NTSTATUS trans2_findfirst(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_search_first *search;
	NTSTATUS status;
	uint16_t level;
	struct find_state *state;

	/* make sure we got all the parameters */
	if (trans->in.params.length < 14) {
		return NT_STATUS_FOOBAR;
	}

	search = talloc(op, union smb_search_first);
	NT_STATUS_HAVE_NO_MEMORY(search);

	search->t2ffirst.in.search_attrib = SVAL(trans->in.params.data, 0);
	search->t2ffirst.in.max_count     = SVAL(trans->in.params.data, 2);
	search->t2ffirst.in.flags         = SVAL(trans->in.params.data, 4);
	level                             = SVAL(trans->in.params.data, 6);
	search->t2ffirst.in.storage_type  = IVAL(trans->in.params.data, 8);

	trans2_pull_blob_string(req, &trans->in.params, 12, &search->t2ffirst.in.pattern, 0);
	if (search->t2ffirst.in.pattern == NULL) {
		return NT_STATUS_FOOBAR;
	}

	search->t2ffirst.level = (enum smb_search_level)level;
	if (search->t2ffirst.level >= RAW_SEARCH_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	if (search->t2ffirst.level == RAW_SEARCH_EA_LIST) {
		status = ea_pull_name_list(&trans->in.data, req,
					   &search->t2ffirst.in.num_names, 
					   &search->t2ffirst.in.ea_names);
		NT_STATUS_NOT_OK_RETURN(status);
	}

	/* setup the private state structure that the backend will
	   give us in the callback */
	state = talloc(op, struct find_state);
	NT_STATUS_HAVE_NO_MEMORY(state);
	state->op		= op;
	state->search		= search;
	state->level		= search->t2ffirst.level;
	state->last_entry_offset= 0;
	state->flags		= search->t2ffirst.in.flags;

	/* setup for just a header in the reply */
	trans2_setup_reply(trans, 10, 0, 0);

	op->op_info = state;
	op->send_fn = trans2_findfirst_send;

	return ntvfs_search_first(req->ntvfs, search, state, find_callback);
}


/*
  trans2 findnext send
*/
static NTSTATUS trans2_findnext_send(struct trans_op *op)
{
	struct smbsrv_request *req = op->req;
	struct smb_trans2 *trans = op->trans;
	union smb_search_next *search;
	struct find_state *state;
	uint8_t *param;

	TRANS2_CHECK_ASYNC_STATUS(state, struct find_state);
	search = talloc_get_type(state->search, union smb_search_next);

	/* fill in the findfirst reply header */
	param = trans->out.params.data;
	SSVAL(param, VWV(0), search->t2fnext.out.count);
	SSVAL(param, VWV(1), search->t2fnext.out.end_of_search);
	SSVAL(param, VWV(2), 0);
	SSVAL(param, VWV(3), state->last_entry_offset);
	
	return NT_STATUS_OK;
}


/*
  trans2 findnext implementation
*/
static NTSTATUS trans2_findnext(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	union smb_search_next *search;
	NTSTATUS status;
	uint16_t level;
	struct find_state *state;

	/* make sure we got all the parameters */
	if (trans->in.params.length < 12) {
		return NT_STATUS_FOOBAR;
	}

	search = talloc(op, union smb_search_next);
	NT_STATUS_HAVE_NO_MEMORY(search);

	search->t2fnext.in.handle        = SVAL(trans->in.params.data, 0);
	search->t2fnext.in.max_count     = SVAL(trans->in.params.data, 2);
	level                            = SVAL(trans->in.params.data, 4);
	search->t2fnext.in.resume_key    = IVAL(trans->in.params.data, 6);
	search->t2fnext.in.flags         = SVAL(trans->in.params.data, 10);

	trans2_pull_blob_string(req, &trans->in.params, 12, &search->t2fnext.in.last_name, 0);
	if (search->t2fnext.in.last_name == NULL) {
		return NT_STATUS_FOOBAR;
	}

	search->t2fnext.level = (enum smb_search_level)level;
	if (search->t2fnext.level >= RAW_SEARCH_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	if (search->t2fnext.level == RAW_SEARCH_EA_LIST) {
		status = ea_pull_name_list(&trans->in.data, req,
					   &search->t2fnext.in.num_names, 
					   &search->t2fnext.in.ea_names);
		NT_STATUS_NOT_OK_RETURN(status);
	}

	/* setup the private state structure that the backend will give us in the callback */
	state = talloc(op, struct find_state);
	NT_STATUS_HAVE_NO_MEMORY(state);
	state->op		= op;
	state->search		= search;
	state->level		= search->t2fnext.level;
	state->last_entry_offset= 0;
	state->flags		= search->t2fnext.in.flags;

	/* setup for just a header in the reply */
	trans2_setup_reply(trans, 8, 0, 0);

	op->op_info = state;
	op->send_fn = trans2_findnext_send;

	return ntvfs_search_next(req->ntvfs, search, state, find_callback);
}


/*
  backend for trans2 requests
*/
static NTSTATUS trans2_backend(struct smbsrv_request *req, struct trans_op *op)
{
	struct smb_trans2 *trans = op->trans;
	NTSTATUS status;

	/* direct trans2 pass thru */
	status = ntvfs_trans2(req->ntvfs, trans);
	if (!NT_STATUS_EQUAL(NT_STATUS_NOT_IMPLEMENTED, status)) {
		return status;
	}

	/* must have at least one setup word */
	if (trans->in.setup_count < 1) {
		return NT_STATUS_FOOBAR;
	}

	/* the trans2 command is in setup[0] */
	switch (trans->in.setup[0]) {
	case TRANSACT2_FINDFIRST:
		return trans2_findfirst(req, op);
	case TRANSACT2_FINDNEXT:
		return trans2_findnext(req, op);
	case TRANSACT2_QPATHINFO:
		return trans2_qpathinfo(req, op);
	case TRANSACT2_QFILEINFO:
		return trans2_qfileinfo(req, op);
	case TRANSACT2_SETFILEINFO:
		return trans2_setfileinfo(req, op);
	case TRANSACT2_SETPATHINFO:
		return trans2_setpathinfo(req, op);
	case TRANSACT2_QFSINFO:
		return trans2_qfsinfo(req, op);
	case TRANSACT2_OPEN:
		return trans2_open(req, op);
	case TRANSACT2_MKDIR:
		return trans2_mkdir(req, op);
	}

	/* an unknown trans2 command */
	return NT_STATUS_FOOBAR;
}


/*
  send a continue request
*/
static void reply_trans_continue(struct smbsrv_request *req, uint8_t command,
				 struct smb_trans2 *trans)
{
	struct smbsrv_trans_partial *tp;
	int count;

	/* make sure they don't flood us */
	for (count=0,tp=req->smb_conn->trans_partial;tp;tp=tp->next) count++;
	if (count > 100) {
		smbsrv_send_error(req, NT_STATUS_INSUFFICIENT_RESOURCES);
		return;
	}

	tp = talloc(req, struct smbsrv_trans_partial);

	tp->req = talloc_reference(tp, req);
	tp->trans = trans;
	tp->command = command;

	DLIST_ADD(req->smb_conn->trans_partial, tp);

	/* send a 'please continue' reply */
	smbsrv_setup_reply(req, 0, 0);
	smbsrv_send_reply(req);
}


/*
  answer a reconstructed trans request
*/
static void reply_trans_send(struct ntvfs_request *ntvfs)
{
	struct smbsrv_request *req;
	struct trans_op *op;
	struct smb_trans2 *trans;
	uint16_t params_left, data_left;
	uint8_t *params, *data;
	int i;

	SMBSRV_CHECK_ASYNC_STATUS_ERR(op, struct trans_op);
	trans = op->trans;

	/* if this function needs work to form the nttrans reply buffer, then
	   call that now */
	if (op->send_fn != NULL) {
		NTSTATUS status;
		status = op->send_fn(op);
		if (!NT_STATUS_IS_OK(status)) {
			smbsrv_send_error(req, status);
			return;
		}
	}

	params_left = trans->out.params.length;
	data_left   = trans->out.data.length;
	params      = trans->out.params.data;
	data        = trans->out.data.data;

	smbsrv_setup_reply(req, 10 + trans->out.setup_count, 0);

	if (!NT_STATUS_IS_OK(req->ntvfs->async_states->status)) {
		smbsrv_setup_error(req, req->ntvfs->async_states->status);
	}

	/* we need to divide up the reply into chunks that fit into
	   the negotiated buffer size */
	do {
		uint16_t this_data, this_param, max_bytes;
		uint_t align1 = 1, align2 = (params_left ? 2 : 0);
		struct smbsrv_request *this_req;

		max_bytes = req_max_data(req) - (align1 + align2);

		this_param = params_left;
		if (this_param > max_bytes) {
			this_param = max_bytes;
		}
		max_bytes -= this_param;

		this_data = data_left;
		if (this_data > max_bytes) {
			this_data = max_bytes;
		}

		/* don't destroy unless this is the last chunk */
		if (params_left - this_param != 0 || 
		    data_left - this_data != 0) {
			this_req = smbsrv_setup_secondary_request(req);
		} else {
			this_req = req;
		}

		req_grow_data(this_req, this_param + this_data + (align1 + align2));

		SSVAL(this_req->out.vwv, VWV(0), trans->out.params.length);
		SSVAL(this_req->out.vwv, VWV(1), trans->out.data.length);
		SSVAL(this_req->out.vwv, VWV(2), 0);

		SSVAL(this_req->out.vwv, VWV(3), this_param);
		SSVAL(this_req->out.vwv, VWV(4), align1 + PTR_DIFF(this_req->out.data, this_req->out.hdr));
		SSVAL(this_req->out.vwv, VWV(5), PTR_DIFF(params, trans->out.params.data));

		SSVAL(this_req->out.vwv, VWV(6), this_data);
		SSVAL(this_req->out.vwv, VWV(7), align1 + align2 + 
		      PTR_DIFF(this_req->out.data + this_param, this_req->out.hdr));
		SSVAL(this_req->out.vwv, VWV(8), PTR_DIFF(data, trans->out.data.data));

		SSVAL(this_req->out.vwv, VWV(9), trans->out.setup_count);
		for (i=0;i<trans->out.setup_count;i++) {
			SSVAL(this_req->out.vwv, VWV(10+i), trans->out.setup[i]);
		}

		memset(this_req->out.data, 0, align1);
		if (this_param != 0) {
			memcpy(this_req->out.data + align1, params, this_param);
		}
		memset(this_req->out.data+this_param+align1, 0, align2);
		if (this_data != 0) {
			memcpy(this_req->out.data+this_param+align1+align2, data, this_data);
		}

		params_left -= this_param;
		data_left -= this_data;
		params += this_param;
		data += this_data;

		smbsrv_send_reply(this_req);
	} while (params_left != 0 || data_left != 0);
}


/*
  answer a reconstructed trans request
*/
static void reply_trans_complete(struct smbsrv_request *req, uint8_t command,
				 struct smb_trans2 *trans)
{
	struct trans_op *op;

	SMBSRV_TALLOC_IO_PTR(op, struct trans_op);
	SMBSRV_SETUP_NTVFS_REQUEST(reply_trans_send, NTVFS_ASYNC_STATE_MAY_ASYNC);

	op->req		= req;
	op->trans	= trans;
	op->command	= command;
	op->op_info	= NULL;
	op->send_fn	= NULL;

	/* its a full request, give it to the backend */
	if (command == SMBtrans) {
		SMBSRV_CALL_NTVFS_BACKEND(ntvfs_trans(req->ntvfs, trans));
		return;
	} else {
		SMBSRV_CALL_NTVFS_BACKEND(trans2_backend(req, op));
		return;
	}
}

/*
  Reply to an SMBtrans or SMBtrans2 request
*/
static void reply_trans_generic(struct smbsrv_request *req, uint8_t command)
{
	struct smb_trans2 *trans;
	int i;
	uint16_t param_ofs, data_ofs;
	uint16_t param_count, data_count;
	uint16_t param_total, data_total;

	/* parse request */
	if (req->in.wct < 14) {
		smbsrv_send_error(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	trans = talloc(req, struct smb_trans2);
	if (trans == NULL) {
		smbsrv_send_error(req, NT_STATUS_NO_MEMORY);
		return;
	}

	param_total           = SVAL(req->in.vwv, VWV(0));
	data_total            = SVAL(req->in.vwv, VWV(1));
	trans->in.max_param   = SVAL(req->in.vwv, VWV(2));
	trans->in.max_data    = SVAL(req->in.vwv, VWV(3));
	trans->in.max_setup   = CVAL(req->in.vwv, VWV(4));
	trans->in.flags       = SVAL(req->in.vwv, VWV(5));
	trans->in.timeout     = IVAL(req->in.vwv, VWV(6));
	param_count           = SVAL(req->in.vwv, VWV(9));
	param_ofs             = SVAL(req->in.vwv, VWV(10));
	data_count            = SVAL(req->in.vwv, VWV(11));
	data_ofs              = SVAL(req->in.vwv, VWV(12));
	trans->in.setup_count = CVAL(req->in.vwv, VWV(13));

	if (req->in.wct != 14 + trans->in.setup_count) {
		smbsrv_send_error(req, NT_STATUS_DOS(ERRSRV, ERRerror));
		return;
	}

	/* parse out the setup words */
	trans->in.setup = talloc_array(trans, uint16_t, trans->in.setup_count);
	if (trans->in.setup_count && !trans->in.setup) {
		smbsrv_send_error(req, NT_STATUS_NO_MEMORY);
		return;
	}
	for (i=0;i<trans->in.setup_count;i++) {
		trans->in.setup[i] = SVAL(req->in.vwv, VWV(14+i));
	}

	if (command == SMBtrans) {
		req_pull_string(req, &trans->in.trans_name, req->in.data, -1, STR_TERMINATE);
	}

	if (!req_pull_blob(req, req->in.hdr + param_ofs, param_count, &trans->in.params) ||
	    !req_pull_blob(req, req->in.hdr + data_ofs, data_count, &trans->in.data)) {
		smbsrv_send_error(req, NT_STATUS_FOOBAR);
		return;
	}

	/* is it a partial request? if so, then send a 'send more' message */
	if (param_total > param_count || data_total > data_count) {
		reply_trans_continue(req, command, trans);
		return;
	}

	reply_trans_complete(req, command, trans);
}


/*
  Reply to an SMBtranss2 request
*/
static void reply_transs_generic(struct smbsrv_request *req, uint8_t command)
{
	struct smbsrv_trans_partial *tp;
	struct smb_trans2 *trans = NULL;
	uint16_t param_ofs, data_ofs;
	uint16_t param_count, data_count;
	uint16_t param_disp, data_disp;
	uint16_t param_total, data_total;
	DATA_BLOB params, data;

	/* parse request */
	if (req->in.wct < 8) {
		smbsrv_send_error(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	for (tp=req->smb_conn->trans_partial;tp;tp=tp->next) {
		if (tp->command == command &&
		    SVAL(tp->req->in.hdr, HDR_MID) == SVAL(req->in.hdr, HDR_MID)) {
/* TODO: check the VUID, PID and TID too? */
			break;
		}
	}

	if (tp == NULL) {
		smbsrv_send_error(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	trans = tp->trans;

	param_total           = SVAL(req->in.vwv, VWV(0));
	data_total            = SVAL(req->in.vwv, VWV(1));
	param_count           = SVAL(req->in.vwv, VWV(2));
	param_ofs             = SVAL(req->in.vwv, VWV(3));
	param_disp            = SVAL(req->in.vwv, VWV(4));
	data_count            = SVAL(req->in.vwv, VWV(5));
	data_ofs              = SVAL(req->in.vwv, VWV(6));
	data_disp             = SVAL(req->in.vwv, VWV(7));

	if (!req_pull_blob(req, req->in.hdr + param_ofs, param_count, &params) ||
	    !req_pull_blob(req, req->in.hdr + data_ofs, data_count, &data)) {
		smbsrv_send_error(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	/* only allow contiguous requests */
	if ((param_count != 0 &&
	     param_disp != trans->in.params.length) ||
	    (data_count != 0 && 
	     data_disp != trans->in.data.length)) {
		smbsrv_send_error(req, NT_STATUS_INVALID_PARAMETER);
		return;		
	}

	/* add to the existing request */
	if (param_count != 0) {
		trans->in.params.data = talloc_realloc(trans, 
							 trans->in.params.data, 
							 uint8_t, 
							 param_disp + param_count);
		if (trans->in.params.data == NULL) {
			goto failed;
		}
		trans->in.params.length = param_disp + param_count;
	}

	if (data_count != 0) {
		trans->in.data.data = talloc_realloc(trans, 
						       trans->in.data.data, 
						       uint8_t, 
						       data_disp + data_count);
		if (trans->in.data.data == NULL) {
			goto failed;
		}
		trans->in.data.length = data_disp + data_count;
	}

	memcpy(trans->in.params.data + param_disp, params.data, params.length);
	memcpy(trans->in.data.data + data_disp, data.data, data.length);

	/* the sequence number of the reply is taken from the last secondary
	   response */
	tp->req->seq_num = req->seq_num;

	/* we don't reply to Transs2 requests */
	talloc_free(req);

	if (trans->in.params.length == param_total &&
	    trans->in.data.length == data_total) {
		/* its now complete */
		DLIST_REMOVE(tp->req->smb_conn->trans_partial, tp);
		reply_trans_complete(tp->req, command, trans);
	}
	return;

failed:	
	smbsrv_send_error(tp->req, NT_STATUS_NO_MEMORY);
	DLIST_REMOVE(req->smb_conn->trans_partial, tp);
	talloc_free(req);
	talloc_free(tp);
}


/*
  Reply to an SMBtrans2
*/
void smbsrv_reply_trans2(struct smbsrv_request *req)
{
	reply_trans_generic(req, SMBtrans2);
}

/*
  Reply to an SMBtrans
*/
void smbsrv_reply_trans(struct smbsrv_request *req)
{
	reply_trans_generic(req, SMBtrans);
}

/*
  Reply to an SMBtranss request
*/
void smbsrv_reply_transs(struct smbsrv_request *req)
{
	reply_transs_generic(req, SMBtrans);
}

/*
  Reply to an SMBtranss2 request
*/
void smbsrv_reply_transs2(struct smbsrv_request *req)
{
	reply_transs_generic(req, SMBtrans2);
}
