/*
 * Unix SMB/CIFS implementation.
 * Support for OneFS
 *
 * Copyright (C) Tim Prouty, 2008
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

#include "includes.h"
#include "onefs.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_VFS

static int onefs_mkdir(vfs_handle_struct *handle, const char *path,
		       mode_t mode)
{
	DEBUG(0, ("SMB_VFS_MKDIR should never be called in vfs_onefs"));
	return SMB_VFS_NEXT_MKDIR(handle, path, mode);
}

static int onefs_open(vfs_handle_struct *handle, const char *fname,
		      files_struct *fsp, int flags, mode_t mode)
{
	DEBUG(0, ("SMB_VFS_OPEN should never be called in vfs_onefs"));
	return SMB_VFS_NEXT_OPEN(handle, fname, fsp, flags, mode);
}

static vfs_op_tuple onefs_ops[] = {
	{SMB_VFS_OP(onefs_mkdir), SMB_VFS_OP_MKDIR,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(onefs_open), SMB_VFS_OP_OPEN,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(onefs_create_file), SMB_VFS_OP_CREATE_FILE,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(onefs_fget_nt_acl), SMB_VFS_OP_FGET_NT_ACL,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(onefs_get_nt_acl), SMB_VFS_OP_GET_NT_ACL,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(onefs_fset_nt_acl), SMB_VFS_OP_FSET_NT_ACL,
	 SMB_VFS_LAYER_OPAQUE},
	{SMB_VFS_OP(NULL), SMB_VFS_OP_NOOP, SMB_VFS_LAYER_NOOP}
};

NTSTATUS vfs_onefs_init(void)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION, "onefs",
				onefs_ops);
}