/* 
   samba -- Unix SMB/CIFS implementation.

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
#ifndef __LIBCLI_AUTH_H__
#define __LIBCLI_AUTH_H__

#include "librpc/gen_ndr/netlogon.h"
#include "librpc/gen_ndr/wkssvc.h"
#include "libcli/auth/credentials.h"
#include "libcli/auth/ntlm_check.h"
#include "libcli/auth/proto.h"
#include "libcli/auth/msrpc_parse.h"

#define NTLMSSP_NAME_TYPE_SERVER      0x01
#define NTLMSSP_NAME_TYPE_DOMAIN      0x02
#define NTLMSSP_NAME_TYPE_SERVER_DNS  0x03
#define NTLMSSP_NAME_TYPE_DOMAIN_DNS  0x04

#endif /* __LIBCLI_AUTH_H__ */
