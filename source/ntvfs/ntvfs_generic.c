/* 
   Unix SMB/CIFS implementation.

   NTVFS generic level mapping code

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
  this implements mappings between info levels for NTVFS backend calls

  the idea is that each of these functions implements one of the NTVFS
  backend calls in terms of the 'generic' call. All backends that use
  these functions must supply the generic call, but can if it wants to
  also implement other levels if the need arises

  this allows backend writers to only implement one varient of each
  call unless they need fine grained control of the calls.
*/

#include "includes.h"

/*
  see if a filename ends in EXE COM DLL or SYM. This is needed for the DENY_DOS mapping for OpenX
*/
static BOOL is_exe_file(const char *fname)
{
	char *p;
	p = strrchr(fname, '.');
	if (!p) {
		return False;
	}
	p++;
	if (strcasecmp(p, "EXE") == 0 ||
	    strcasecmp(p, "COM") == 0 ||
	    strcasecmp(p, "DLL") == 0 ||
	    strcasecmp(p, "SYM") == 0) {
		return True;
	}
	return False;
}


/* 
   NTVFS open generic to any mapper
*/
NTSTATUS ntvfs_map_open(struct request_context *req, union smb_open *io)
{
	NTSTATUS status;
	union smb_open io2;

	if (io->generic.level == RAW_OPEN_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	switch (io->generic.level) {
	case RAW_OPEN_OPENX:
		ZERO_STRUCT(io2.generic.in);
		io2.generic.level = RAW_OPEN_GENERIC;
		if (io->openx.in.flags & OPENX_FLAGS_REQUEST_OPLOCK) {
			io2.generic.in.flags |= NTCREATEX_FLAGS_REQUEST_OPLOCK;
		}
		if (io->openx.in.flags & OPENX_FLAGS_REQUEST_BATCH_OPLOCK) {
			io2.generic.in.flags |= NTCREATEX_FLAGS_REQUEST_BATCH_OPLOCK;
		}

		switch (io->openx.in.open_mode & OPENX_MODE_ACCESS_MASK) {
		case OPENX_MODE_ACCESS_READ:
			io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_READ;
			break;
		case OPENX_MODE_ACCESS_WRITE:
			io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_WRITE;
			break;
		case OPENX_MODE_ACCESS_RDWR:
		case OPENX_MODE_ACCESS_FCB:
			io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_ALL_ACCESS;
			break;
		}

		switch (io->openx.in.open_mode & OPENX_MODE_DENY_MASK) {
		case OPENX_MODE_DENY_READ:
			io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_WRITE;
			break;
		case OPENX_MODE_DENY_WRITE:
			io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ;
			break;
		case OPENX_MODE_DENY_ALL:
			io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
			break;
		case OPENX_MODE_DENY_NONE:
			io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
			break;
		case OPENX_MODE_DENY_DOS:
			/* DENY_DOS is quite strange - it depends on the filename! */
			if (is_exe_file(io->openx.in.fname)) {
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
			} else {
				if ((io->openx.in.open_mode & OPENX_MODE_ACCESS_MASK) == 
				    OPENX_MODE_ACCESS_READ) {
					io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ;
				} else {
					io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
				}
			}
			break;
		case OPENX_MODE_DENY_FCB:
			io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
			break;
		}

		switch (io->openx.in.open_func) {
		case (OPENX_OPEN_FUNC_FAIL):
			io2.generic.in.open_disposition = NTCREATEX_DISP_CREATE;
			break;
		case (OPENX_OPEN_FUNC_OPEN):
			io2.generic.in.open_disposition = NTCREATEX_DISP_OPEN;
			break;
		case (OPENX_OPEN_FUNC_TRUNC):
			io2.generic.in.open_disposition = NTCREATEX_DISP_OVERWRITE;
			break;
		case (OPENX_OPEN_FUNC_FAIL | OPENX_OPEN_FUNC_CREATE):
			io2.generic.in.open_disposition = NTCREATEX_DISP_CREATE;
			break;
		case (OPENX_OPEN_FUNC_OPEN | OPENX_OPEN_FUNC_CREATE):
			io2.generic.in.open_disposition = NTCREATEX_DISP_OPEN_IF;
			break;
		case (OPENX_OPEN_FUNC_TRUNC | OPENX_OPEN_FUNC_CREATE):
			io2.generic.in.open_disposition = NTCREATEX_DISP_OVERWRITE_IF;
			break;			
		}
		io2.generic.in.alloc_size = io->openx.in.size;
		io2.generic.in.file_attr = io->openx.in.file_attrs;
		io2.generic.in.fname = io->openx.in.fname;

		status = req->conn->ntvfs_ops->open(req, &io2);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		
		ZERO_STRUCT(io->openx.out);
		io->openx.out.fnum = io2.generic.out.fnum;
		io->openx.out.attrib = io2.generic.out.attrib;
		io->openx.out.write_time = nt_time_to_unix(&io2.generic.out.write_time);
		io->openx.out.size = io2.generic.out.size;
		
		return NT_STATUS_OK;


	case RAW_OPEN_OPEN:
		ZERO_STRUCT(io2.generic.in);
		io2.generic.level = RAW_OPEN_GENERIC;
		io2.generic.in.file_attr = io->open.in.search_attrs;
		io2.generic.in.fname = io->open.in.fname;
		io2.generic.in.open_disposition = NTCREATEX_DISP_OPEN;
		DEBUG(9,("ntvfs_map_open(OPEN): mapping flags=0x%x\n",
			io->open.in.flags));
		switch (io->open.in.flags & OPEN_FLAGS_MODE_MASK) {
			case OPEN_FLAGS_OPEN_READ:
				io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_READ;
				io->open.out.rmode = DOS_OPEN_RDONLY;
				break;
			case OPEN_FLAGS_OPEN_WRITE:
				io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_WRITE;
				io->open.out.rmode = DOS_OPEN_WRONLY;
				break;
			case OPEN_FLAGS_OPEN_RDWR:
			case 0xf: /* FCB mode */
				io2.generic.in.access_mask = GENERIC_RIGHTS_FILE_ALL_ACCESS;
				io->open.out.rmode = DOS_OPEN_RDWR; /* assume we got r/w */
				break;
			default:
				DEBUG(2,("ntvfs_map_open(OPEN): invalid mode 0x%x\n",
					io->open.in.flags & OPEN_FLAGS_MODE_MASK));
				return NT_STATUS_INVALID_PARAMETER;
		}
		
		switch(io->open.in.flags & OPEN_FLAGS_DENY_MASK) {
			case OPEN_FLAGS_DENY_DOS:
				/* DENY_DOS is quite strange - it depends on the filename! */
				/* REWRITE: is this necessary for OPEN? */
				if (is_exe_file(io->open.in.fname)) {
					io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
				} else {
					if ((io->open.in.flags & OPEN_FLAGS_MODE_MASK) == 
					    OPEN_FLAGS_OPEN_READ) {
						io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ;
					} else {
						io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
					}
				}
				break;
			case OPEN_FLAGS_DENY_ALL:
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
				break;
			case OPEN_FLAGS_DENY_WRITE:
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_READ;
				break;
			case OPEN_FLAGS_DENY_READ:
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_WRITE;
				break;
			case OPEN_FLAGS_DENY_NONE:
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_WRITE |
						NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_DELETE;
				break;
			case 0x70: /* FCB mode */
				io2.generic.in.share_access = NTCREATEX_SHARE_ACCESS_NONE;
				break;
			default:
				DEBUG(2,("ntvfs_map_open(OPEN): invalid DENY 0x%x\n",
					io->open.in.flags & OPEN_FLAGS_DENY_MASK));
				return NT_STATUS_INVALID_PARAMETER;
		}
		DEBUG(9,("ntvfs_map_open(OPEN): mapped flags=0x%x to access_mask=0x%x and share_access=0x%x\n",
			io->open.in.flags, io2.generic.in.access_mask, io2.generic.in.share_access));

		status = req->conn->ntvfs_ops->open(req, &io2);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		
		ZERO_STRUCT(io->openx.out);
		io->open.out.fnum = io2.generic.out.fnum;
		io->open.out.attrib = io2.generic.out.attrib;
		io->open.out.write_time = nt_time_to_unix(&io2.generic.out.write_time);
		io->open.out.size = io2.generic.out.size;
		io->open.out.rmode = DOS_OPEN_RDWR;
		
		return NT_STATUS_OK;
	}

	return NT_STATUS_INVALID_LEVEL;
}


/* 
   NTVFS fsinfo generic to any mapper
*/
NTSTATUS ntvfs_map_fsinfo(struct request_context *req, union smb_fsinfo *fs)
{
	NTSTATUS status;
	union smb_fsinfo fs2;

	if (fs->generic.level == RAW_QFS_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	/* ask the backend for the generic info */
	fs2.generic.level = RAW_QFS_GENERIC;

	status = req->conn->ntvfs_ops->fsinfo(req, &fs2);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* and convert it to the required level */
	switch (fs->generic.level) {
	case RAW_QFS_GENERIC:
		return NT_STATUS_INVALID_LEVEL;

	case RAW_QFS_DSKATTR: {
		/* map from generic to DSKATTR */
		unsigned bpunit = 64;

		/* we need to scale the sizes to fit */
		for (bpunit=64; bpunit<0x10000; bpunit *= 2) {
			if (fs2.generic.out.blocks_total * (double)fs2.generic.out.block_size < bpunit * 512 * 65535.0) {
				break;
			}
		}

		fs->dskattr.out.blocks_per_unit = bpunit;
		fs->dskattr.out.block_size = 512;
		fs->dskattr.out.units_total = 
			(fs2.generic.out.blocks_total * (double)fs2.generic.out.block_size) / (bpunit * 512);
		fs->dskattr.out.units_free  = 
			(fs2.generic.out.blocks_free  * (double)fs2.generic.out.block_size) / (bpunit * 512);

		/* we must return a maximum of 2G to old DOS systems, or they get very confused */
		if (bpunit > 64 && req->smb->negotiate.protocol <= PROTOCOL_LANMAN2) {
			fs->dskattr.out.blocks_per_unit = 64;
			fs->dskattr.out.units_total = 0xFFFF;
			fs->dskattr.out.units_free = 0xFFFF;
		}
		return NT_STATUS_OK;
	}

	case RAW_QFS_ALLOCATION:
		fs->allocation.out.fs_id = fs2.generic.out.fs_id;
		fs->allocation.out.total_alloc_units = fs2.generic.out.blocks_total;
		fs->allocation.out.avail_alloc_units = fs2.generic.out.blocks_free;
		fs->allocation.out.sectors_per_unit = 1;
		fs->allocation.out.bytes_per_sector = fs2.generic.out.block_size;
		return NT_STATUS_OK;

	case RAW_QFS_VOLUME:
		fs->volume.out.serial_number = fs2.generic.out.serial_number;
		fs->volume.out.volume_name.s = fs2.generic.out.volume_name;
		return NT_STATUS_OK;

	case RAW_QFS_VOLUME_INFO:
	case RAW_QFS_VOLUME_INFORMATION:
		fs->volume_info.out.create_time = fs2.generic.out.create_time;
		fs->volume_info.out.serial_number = fs2.generic.out.serial_number;
		fs->volume_info.out.volume_name.s = fs2.generic.out.volume_name;
		return NT_STATUS_OK;

	case RAW_QFS_SIZE_INFO:
	case RAW_QFS_SIZE_INFORMATION:
		fs->size_info.out.total_alloc_units = fs2.generic.out.blocks_total;
		fs->size_info.out.avail_alloc_units = fs2.generic.out.blocks_free;
		fs->size_info.out.sectors_per_unit = 1;
		fs->size_info.out.bytes_per_sector = fs2.generic.out.block_size;
		return NT_STATUS_OK;

	case RAW_QFS_DEVICE_INFO:
	case RAW_QFS_DEVICE_INFORMATION:
		fs->device_info.out.device_type = fs2.generic.out.device_type;
		fs->device_info.out.characteristics = fs2.generic.out.device_characteristics;
		return NT_STATUS_OK;

	case RAW_QFS_ATTRIBUTE_INFO:
	case RAW_QFS_ATTRIBUTE_INFORMATION:
		fs->attribute_info.out.fs_attr = fs2.generic.out.fs_attr;
		fs->attribute_info.out.max_file_component_length = fs2.generic.out.max_file_component_length;
		fs->attribute_info.out.fs_type.s = fs2.generic.out.fs_type;
		return NT_STATUS_OK;

	case RAW_QFS_QUOTA_INFORMATION:
		ZERO_STRUCT(fs->quota_information.out.unknown);
		fs->quota_information.out.quota_soft = fs2.generic.out.quota_soft;
		fs->quota_information.out.quota_hard = fs2.generic.out.quota_hard;
		fs->quota_information.out.quota_flags = fs2.generic.out.quota_flags;
		return NT_STATUS_OK;

	case RAW_QFS_FULL_SIZE_INFORMATION:
		fs->full_size_information.out.total_alloc_units = fs2.generic.out.blocks_total;
		fs->full_size_information.out.call_avail_alloc_units = fs2.generic.out.blocks_free;
		fs->full_size_information.out.actual_avail_alloc_units = fs2.generic.out.blocks_free;
		fs->full_size_information.out.sectors_per_unit = 1;
		fs->full_size_information.out.bytes_per_sector = fs2.generic.out.block_size;
		return NT_STATUS_OK;

	case RAW_QFS_OBJECTID_INFORMATION:
		fs->objectid_information.out.guid = fs2.generic.out.guid;
		ZERO_STRUCT(fs->objectid_information.out.unknown);
		return NT_STATUS_OK;
	}


	return NT_STATUS_INVALID_LEVEL;
}


/* 
   NTVFS fileinfo generic to any mapper
*/
NTSTATUS ntvfs_map_fileinfo(struct request_context *req, union smb_fileinfo *info, union smb_fileinfo *info2)
{
	/* and convert it to the required level using results in info2 */
	switch (info->generic.level) {
		case RAW_FILEINFO_GENERIC:
		return NT_STATUS_INVALID_LEVEL;
	case RAW_FILEINFO_GETATTR:
		info->getattr.out.attrib = info2->generic.out.attrib & 0x3f;
		info->getattr.out.size = info2->generic.out.size;
		info->getattr.out.write_time = nt_time_to_unix(&info2->generic.out.write_time);
		return NT_STATUS_OK;
		
	case RAW_FILEINFO_GETATTRE:
		info->getattre.out.attrib = info2->generic.out.attrib;
		info->getattre.out.size = info2->generic.out.size;
		info->getattre.out.write_time = nt_time_to_unix(&info2->generic.out.write_time);
		info->getattre.out.create_time = nt_time_to_unix(&info2->generic.out.create_time);
		info->getattre.out.access_time = nt_time_to_unix(&info2->generic.out.access_time);
		info->getattre.out.alloc_size = info2->generic.out.alloc_size;
		return NT_STATUS_OK;
		
	case RAW_FILEINFO_NETWORK_OPEN_INFORMATION:
		info->network_open_information.out.create_time = info2->generic.out.create_time;
		info->network_open_information.out.access_time = info2->generic.out.access_time;
		info->network_open_information.out.write_time =  info2->generic.out.write_time;
		info->network_open_information.out.change_time = info2->generic.out.change_time;
		info->network_open_information.out.alloc_size = info2->generic.out.alloc_size;
		info->network_open_information.out.size = info2->generic.out.size;
		info->network_open_information.out.attrib = info2->generic.out.attrib;
		return NT_STATUS_OK;

	case RAW_FILEINFO_ALL_INFO:
	case RAW_FILEINFO_ALL_INFORMATION:
		info->all_info.out.create_time = info2->generic.out.create_time;
		info->all_info.out.access_time = info2->generic.out.access_time;
		info->all_info.out.write_time =  info2->generic.out.write_time;
		info->all_info.out.change_time = info2->generic.out.change_time;
		info->all_info.out.attrib = info2->generic.out.attrib;
		info->all_info.out.alloc_size = info2->generic.out.alloc_size;
		info->all_info.out.size = info2->generic.out.size;
		info->all_info.out.nlink = info2->generic.out.nlink;
		info->all_info.out.delete_pending = info2->generic.out.delete_pending;
		info->all_info.out.directory = info2->generic.out.directory;
		info->all_info.out.ea_size = info2->generic.out.ea_size;
		info->all_info.out.fname.s = info2->generic.out.fname.s;
		info->all_info.out.fname.private_length = info2->generic.out.fname.private_length;
		return NT_STATUS_OK;

	case RAW_FILEINFO_BASIC_INFO:
	case RAW_FILEINFO_BASIC_INFORMATION:
		info->basic_info.out.create_time = info2->generic.out.create_time;
		info->basic_info.out.access_time = info2->generic.out.access_time;
		info->basic_info.out.write_time = info2->generic.out.write_time;
		info->basic_info.out.change_time = info2->generic.out.change_time;
		info->basic_info.out.attrib = info2->generic.out.attrib;
		return NT_STATUS_OK;

	case RAW_FILEINFO_STANDARD:
		info->standard.out.create_time = nt_time_to_unix(&info2->generic.out.create_time);
		info->standard.out.access_time = nt_time_to_unix(&info2->generic.out.access_time);
		info->standard.out.write_time = nt_time_to_unix(&info2->generic.out.write_time);
		info->standard.out.size = info2->generic.out.size;
		info->standard.out.alloc_size = info2->generic.out.alloc_size;
		info->standard.out.attrib = info2->generic.out.attrib;
		return NT_STATUS_OK;

	case RAW_FILEINFO_EA_SIZE:
		info->ea_size.out.create_time = nt_time_to_unix(&info2->generic.out.create_time);
		info->ea_size.out.access_time = nt_time_to_unix(&info2->generic.out.access_time);
		info->ea_size.out.write_time = nt_time_to_unix(&info2->generic.out.write_time);
		info->ea_size.out.size = info2->generic.out.size;
		info->ea_size.out.alloc_size = info2->generic.out.alloc_size;
		info->ea_size.out.attrib = info2->generic.out.attrib;
		info->ea_size.out.ea_size = info2->generic.out.ea_size;
		return NT_STATUS_OK;

	case RAW_FILEINFO_STANDARD_INFO:
	case RAW_FILEINFO_STANDARD_INFORMATION:
		info->standard_info.out.alloc_size = info2->generic.out.alloc_size;
		info->standard_info.out.size = info2->generic.out.size;
		info->standard_info.out.nlink = info2->generic.out.nlink;
		info->standard_info.out.delete_pending = info2->generic.out.delete_pending;
		info->standard_info.out.directory = info2->generic.out.directory;
		return NT_STATUS_OK;

	case RAW_FILEINFO_INTERNAL_INFORMATION:
		info->internal_information.out.file_id = info2->generic.out.file_id;
		return NT_STATUS_OK;

	case RAW_FILEINFO_EA_INFO:
	case RAW_FILEINFO_EA_INFORMATION:
		info->ea_info.out.ea_size = info2->generic.out.ea_size;
		return NT_STATUS_OK;

	case RAW_FILEINFO_ATTRIBUTE_TAG_INFORMATION:
		info->attribute_tag_information.out.attrib = info2->generic.out.attrib;
		info->attribute_tag_information.out.reparse_tag = info2->generic.out.reparse_tag;
		return NT_STATUS_OK;

	case RAW_FILEINFO_STREAM_INFO:
	case RAW_FILEINFO_STREAM_INFORMATION:
		/* setup a single data stream */
		info->stream_info.out.num_streams = info2->generic.out.num_streams;
		info->stream_info.out.streams = talloc(req->mem_ctx, sizeof(info2->stream_info.out.streams[0]));
		if (!info->stream_info.out.streams) {
			return NT_STATUS_NO_MEMORY;
		}
		info->stream_info.out.streams[0].size = info2->generic.out.streams[0].size;
		info->stream_info.out.streams[0].alloc_size = info2->generic.out.streams[0].alloc_size;
		info->stream_info.out.streams[0].stream_name.s = info2->generic.out.streams[0].stream_name.s;
		info->stream_info.out.streams[0].stream_name.private_length = info->generic.out.streams[0].stream_name.private_length;
		return NT_STATUS_OK;

	case RAW_FILEINFO_NAME_INFO:
	case RAW_FILEINFO_NAME_INFORMATION:
		info->name_info.out.fname.s = info2->generic.out.fname.s;
		info->name_info.out.fname.private_length = info2->generic.out.fname.private_length;
		return NT_STATUS_OK;
		
	case RAW_FILEINFO_ALT_NAME_INFO:
	case RAW_FILEINFO_ALT_NAME_INFORMATION:
		info->alt_name_info.out.fname.s = info2->generic.out.alt_fname.s;
		info->alt_name_info.out.fname.private_length = info2->generic.out.alt_fname.private_length;
		return NT_STATUS_OK;
	}

	return NT_STATUS_INVALID_LEVEL;
}

/* 
   NTVFS fileinfo generic to any mapper
*/
NTSTATUS ntvfs_map_qfileinfo(struct request_context *req, union smb_fileinfo *info)
{
	NTSTATUS status;
	union smb_fileinfo info2;

	if (info->generic.level == RAW_FILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	/* ask the backend for the generic info */
	info2.generic.level = RAW_FILEINFO_GENERIC;
	info2.generic.in.fnum = info->generic.in.fnum;

	status = req->conn->ntvfs_ops->qfileinfo(req, &info2);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	return ntvfs_map_fileinfo(req, info, &info2);
}

/* 
   NTVFS pathinfo generic to any mapper
*/
NTSTATUS ntvfs_map_qpathinfo(struct request_context *req, union smb_fileinfo *info)
{
	NTSTATUS status;
	union smb_fileinfo info2;

	if (info->generic.level == RAW_FILEINFO_GENERIC) {
		return NT_STATUS_INVALID_LEVEL;
	}

	/* ask the backend for the generic info */
	info2.generic.level = RAW_FILEINFO_GENERIC;
	info2.generic.in.fname = info->generic.in.fname;

	status = req->conn->ntvfs_ops->qpathinfo(req, &info2);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	return ntvfs_map_fileinfo(req, info, &info2);
}
