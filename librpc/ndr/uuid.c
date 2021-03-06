/* 
   Unix SMB/CIFS implementation.

   UUID/GUID functions

   Copyright (C) Theodore Ts'o               1996, 1997,
   Copyright (C) Jim McDonough                     2002.
   Copyright (C) Andrew Tridgell                   2003.
   
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

#include "includes.h"
#include "librpc/ndr/libndr.h"
#include "librpc/gen_ndr/ndr_misc.h"

/**
  build a GUID from a string
*/
_PUBLIC_ NTSTATUS GUID_from_data_blob(const DATA_BLOB *s, struct GUID *guid)
{
	NTSTATUS status = NT_STATUS_INVALID_PARAMETER;
	uint32_t time_low;
	uint32_t time_mid, time_hi_and_version;
	uint32_t clock_seq[2];
	uint32_t node[6];
	uint8_t buf16[16];

	DATA_BLOB blob16 = data_blob_const(buf16, sizeof(buf16));
	int i;

	if (s->data == NULL) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (s->length == 36) {
		TALLOC_CTX *mem_ctx;
		const char *string;

		mem_ctx = talloc_new(NULL);
		NT_STATUS_HAVE_NO_MEMORY(mem_ctx);
		string = talloc_strndup(mem_ctx, (const char *)s->data, s->length);
		NT_STATUS_HAVE_NO_MEMORY(string);
		if (11 == sscanf(string,
				 "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				 &time_low, &time_mid, &time_hi_and_version, 
				 &clock_seq[0], &clock_seq[1],
				 &node[0], &node[1], &node[2], &node[3], &node[4], &node[5])) {
			status = NT_STATUS_OK;
		}
		talloc_free(mem_ctx);

	} else if (s->length == 38) {
		TALLOC_CTX *mem_ctx;
		const char *string;

		mem_ctx = talloc_new(NULL);
		NT_STATUS_HAVE_NO_MEMORY(mem_ctx);
		string = talloc_strndup(mem_ctx, (const char *)s->data, s->length);
		NT_STATUS_HAVE_NO_MEMORY(string);
		if (11 == sscanf((const char *)s->data, 
				 "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
				 &time_low, &time_mid, &time_hi_and_version, 
				 &clock_seq[0], &clock_seq[1],
				 &node[0], &node[1], &node[2], &node[3], &node[4], &node[5])) {
			status = NT_STATUS_OK;
		}
		talloc_free(mem_ctx);

	} else if (s->length == 32) {
		size_t rlen = strhex_to_str((char *)blob16.data, blob16.length,
					    (const char *)s->data, s->length);
		if (rlen == blob16.length) {
			/* goto the ndr_pull_struct_blob() path */
			status = NT_STATUS_OK;
			s = &blob16;
		}
	}

	if (s->length == 16) {
		enum ndr_err_code ndr_err;
		struct GUID guid2;
		TALLOC_CTX *mem_ctx;

		mem_ctx = talloc_new(NULL);
		NT_STATUS_HAVE_NO_MEMORY(mem_ctx);

		ndr_err = ndr_pull_struct_blob(s, mem_ctx, NULL, &guid2,
					       (ndr_pull_flags_fn_t)ndr_pull_GUID);
		talloc_free(mem_ctx);
		if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
			return ndr_map_error2ntstatus(ndr_err);
		}
		*guid = guid2;
		return NT_STATUS_OK;
	}

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	guid->time_low = time_low;
	guid->time_mid = time_mid;
	guid->time_hi_and_version = time_hi_and_version;
	guid->clock_seq[0] = clock_seq[0];
	guid->clock_seq[1] = clock_seq[1];
	for (i=0;i<6;i++) {
		guid->node[i] = node[i];
	}

	return NT_STATUS_OK;
}

/**
  build a GUID from a string
*/
_PUBLIC_ NTSTATUS GUID_from_string(const char *s, struct GUID *guid)
{
	DATA_BLOB blob = data_blob_string_const(s);
	return GUID_from_data_blob(&blob, guid);
	return NT_STATUS_OK;
}

/**
  build a GUID from a string
*/
_PUBLIC_ NTSTATUS NS_GUID_from_string(const char *s, struct GUID *guid)
{
	NTSTATUS status = NT_STATUS_INVALID_PARAMETER;
	uint32_t time_low;
	uint32_t time_mid, time_hi_and_version;
	uint32_t clock_seq[2];
	uint32_t node[6];
	int i;

	if (s == NULL) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (11 == sscanf(s, "%08x-%04x%04x-%02x%02x%02x%02x-%02x%02x%02x%02x",
			 &time_low, &time_mid, &time_hi_and_version, 
			 &clock_seq[0], &clock_seq[1],
			 &node[0], &node[1], &node[2], &node[3], &node[4], &node[5])) {
	        status = NT_STATUS_OK;
	}

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	guid->time_low = time_low;
	guid->time_mid = time_mid;
	guid->time_hi_and_version = time_hi_and_version;
	guid->clock_seq[0] = clock_seq[0];
	guid->clock_seq[1] = clock_seq[1];
	for (i=0;i<6;i++) {
		guid->node[i] = node[i];
	}

	return NT_STATUS_OK;
}

/**
 * generate a random GUID
 */
_PUBLIC_ struct GUID GUID_random(void)
{
	struct GUID guid;

	generate_random_buffer((uint8_t *)&guid, sizeof(guid));
	guid.clock_seq[0] = (guid.clock_seq[0] & 0x3F) | 0x80;
	guid.time_hi_and_version = (guid.time_hi_and_version & 0x0FFF) | 0x4000;

	return guid;
}

/**
 * generate an empty GUID 
 */
_PUBLIC_ struct GUID GUID_zero(void)
{
	struct GUID guid;

	ZERO_STRUCT(guid);

	return guid;
}

_PUBLIC_ bool GUID_all_zero(const struct GUID *u)
{
	if (u->time_low != 0 ||
	    u->time_mid != 0 ||
	    u->time_hi_and_version != 0 ||
	    u->clock_seq[0] != 0 ||
	    u->clock_seq[1] != 0 ||
	    !all_zero(u->node, 6)) {
		return false;
	}
	return true;
}

_PUBLIC_ bool GUID_equal(const struct GUID *u1, const struct GUID *u2)
{
	if (u1->time_low != u2->time_low ||
	    u1->time_mid != u2->time_mid ||
	    u1->time_hi_and_version != u2->time_hi_and_version ||
	    u1->clock_seq[0] != u2->clock_seq[0] ||
	    u1->clock_seq[1] != u2->clock_seq[1] ||
	    memcmp(u1->node, u2->node, 6) != 0) {
		return false;
	}
	return true;
}

_PUBLIC_ int GUID_compare(const struct GUID *u1, const struct GUID *u2)
{
	if (u1->time_low != u2->time_low) {
		return u1->time_low - u2->time_low;
	}

	if (u1->time_mid != u2->time_mid) {
		return u1->time_mid - u2->time_mid;
	}

	if (u1->time_hi_and_version != u2->time_hi_and_version) {
		return u1->time_hi_and_version - u2->time_hi_and_version;
	}

	if (u1->clock_seq[0] != u2->clock_seq[0]) {
		return u1->clock_seq[0] - u2->clock_seq[0];
	}

	if (u1->clock_seq[1] != u2->clock_seq[1]) {
		return u1->clock_seq[1] - u2->clock_seq[1];
	}

	return memcmp(u1->node, u2->node, 6);
}

/**
  its useful to be able to display these in debugging messages
*/
_PUBLIC_ char *GUID_string(TALLOC_CTX *mem_ctx, const struct GUID *guid)
{
	return talloc_asprintf(mem_ctx, 
			       "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			       guid->time_low, guid->time_mid,
			       guid->time_hi_and_version,
			       guid->clock_seq[0],
			       guid->clock_seq[1],
			       guid->node[0], guid->node[1],
			       guid->node[2], guid->node[3],
			       guid->node[4], guid->node[5]);
}

_PUBLIC_ char *GUID_string2(TALLOC_CTX *mem_ctx, const struct GUID *guid)
{
	char *ret, *s = GUID_string(mem_ctx, guid);
	ret = talloc_asprintf(mem_ctx, "{%s}", s);
	talloc_free(s);
	return ret;
}

_PUBLIC_ char *GUID_hexstring(TALLOC_CTX *mem_ctx, const struct GUID *guid)
{
	char *ret;
	DATA_BLOB guid_blob;
	enum ndr_err_code ndr_err;
	TALLOC_CTX *tmp_mem;

	tmp_mem = talloc_new(mem_ctx);
	if (!tmp_mem) {
		return NULL;
	}
	ndr_err = ndr_push_struct_blob(&guid_blob, tmp_mem,
				       NULL,
				       guid,
				       (ndr_push_flags_fn_t)ndr_push_GUID);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		talloc_free(tmp_mem);
		return NULL;
	}

	ret = data_blob_hex_string(mem_ctx, &guid_blob);
	talloc_free(tmp_mem);
	return ret;
}

_PUBLIC_ char *NS_GUID_string(TALLOC_CTX *mem_ctx, const struct GUID *guid)
{
	return talloc_asprintf(mem_ctx, 
			       "%08x-%04x%04x-%02x%02x%02x%02x-%02x%02x%02x%02x",
			       guid->time_low, guid->time_mid,
			       guid->time_hi_and_version,
			       guid->clock_seq[0],
			       guid->clock_seq[1],
			       guid->node[0], guid->node[1],
			       guid->node[2], guid->node[3],
			       guid->node[4], guid->node[5]);
}

_PUBLIC_ bool policy_handle_empty(struct policy_handle *h) 
{
	return (h->handle_type == 0 && GUID_all_zero(&h->uuid));
}
