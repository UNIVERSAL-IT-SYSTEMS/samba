/*
 * Unix SMB/CIFS implementation.
 * Support for OneFS
 *
 * Copyright (C) Steven Danneman, 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ONEFS_H
#define _ONEFS_H

#include "includes.h"

#include <sys/isi_acl.h>

/* OneFS Module smb.conf parameters and defaults */

/**
* Specifies when ACLs presented to Windows should be canonicalized
* into the ordering which Explorer expects.
*/
enum onefs_acl_wire_format
{
	ACL_FORMAT_RAW, /**< Never canonicalize */
	ACL_FORMAT_WINDOWS_SD, /**< Only canonicalize synthetic ACLs */
	ACL_FORMAT_ALWAYS /**< Always canonicalize */
};

#define PARM_ONEFS_TYPE "onefs"
#define PARM_ACL_WIRE_FORMAT "acl wire format"
#define PARM_ACL_WIRE_FORMAT_DEFAULT ACL_FORMAT_WINDOWS_SD
#define PARM_SIMPLE_FILE_SHARING_COMPATIBILITY_MODE "simple file sharing compatibility mode"
#define PARM_SIMPLE_FILE_SHARING_COMPATIBILITY_MODE_DEFAULT false
#define PARM_CREATOR_OWNER_GETS_FULL_CONTROL "creator owner gets full control"
#define PARM_CREATOR_OWNER_GETS_FULL_CONTROL_DEFAULT true

/*
 * vfs interface handlers
 */
NTSTATUS onefs_create_file(vfs_handle_struct *handle,
			   struct smb_request *req,
			   uint16_t root_dir_fid,
			   const char *fname,
			   uint32_t create_file_flags,
			   uint32_t access_mask,
			   uint32_t share_access,
			   uint32_t create_disposition,
			   uint32_t create_options,
			   uint32_t file_attributes,
			   uint32_t oplock_request,
			   uint64_t allocation_size,
			   struct security_descriptor *sd,
			   struct ea_list *ea_list,
			   files_struct **result,
			   int *pinfo,
			   SMB_STRUCT_STAT *psbuf);

NTSTATUS onefs_fget_nt_acl(vfs_handle_struct *handle, files_struct *fsp,
			   uint32 security_info, SEC_DESC **ppdesc);

NTSTATUS onefs_get_nt_acl(vfs_handle_struct *handle, const char* name,
			  uint32 security_info, SEC_DESC **ppdesc);

NTSTATUS onefs_fset_nt_acl(vfs_handle_struct *handle, files_struct *fsp,
			   uint32 security_info_sent, SEC_DESC *psd);


/*
 * Utility functions
 */
NTSTATUS onefs_setup_sd(uint32 security_info_sent, SEC_DESC *psd,
			struct ifs_security_descriptor *sd);

/*
 * System Interfaces
 */
int onefs_sys_create_file(connection_struct *conn,
			  int base_fd,
			  const char *path,
		          uint32_t access_mask,
		          uint32_t open_access_mask,
			  uint32_t share_access,
			  uint32_t create_options,
			  int flags,
			  mode_t mode,
			  int oplock_request,
			  uint64_t id,
			  struct security_descriptor *sd,
			  uint32_t ntfs_flags,
			  int *granted_oplock);



#endif /* _ONEFS_H */