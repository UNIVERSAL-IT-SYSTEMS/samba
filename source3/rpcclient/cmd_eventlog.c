/*
   Unix SMB/CIFS implementation.
   RPC pipe client

   Copyright (C) Günther Deschner 2009

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
#include "rpcclient.h"

static NTSTATUS get_eventlog_handle(struct rpc_pipe_client *cli,
				    TALLOC_CTX *mem_ctx,
				    const char *log,
				    struct policy_handle *handle)
{
	NTSTATUS status;
	struct eventlog_OpenUnknown0 unknown0;
	struct lsa_String logname, servername;

	unknown0.unknown0 = 0x005c;
	unknown0.unknown1 = 0x0001;

	init_lsa_String(&logname, log);
	init_lsa_String(&servername, NULL);

	status = rpccli_eventlog_OpenEventLogW(cli, mem_ctx,
					       &unknown0,
					       &logname,
					       &servername,
					       0x00000001, /* major */
					       0x00000001, /* minor */
					       handle);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	return NT_STATUS_OK;
}

static NTSTATUS cmd_eventlog_readlog(struct rpc_pipe_client *cli,
				     TALLOC_CTX *mem_ctx,
				     int argc,
				     const char **argv)
{
	NTSTATUS status;
	struct policy_handle handle;

	uint32_t flags = EVENTLOG_BACKWARDS_READ |
			 EVENTLOG_SEQUENTIAL_READ;
	uint32_t offset = 0;
	uint32_t number_of_bytes = 0;
	uint8_t *data = NULL;
	uint32_t sent_size = 0;
	uint32_t real_size = 0;

	if (argc != 2) {
		printf("Usage: %s logname\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = get_eventlog_handle(cli, mem_ctx, argv[1], &handle);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	while (1) {
		status = rpccli_eventlog_ReadEventLogW(cli, mem_ctx,
						       &handle,
						       flags,
						       offset,
						       number_of_bytes,
						       data,
						       &sent_size,
						       &real_size);
		if (NT_STATUS_EQUAL(status, NT_STATUS_BUFFER_TOO_SMALL)) {
			number_of_bytes = real_size;
			data = talloc_array(mem_ctx, uint8_t, real_size);
			continue;
		}

		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}

		{
			enum ndr_err_code ndr_err;
			DATA_BLOB blob;
			struct eventlog_Record rec;

			blob = data_blob_const(data, sent_size);

			ndr_err = ndr_pull_struct_blob(&blob, mem_ctx, NULL,
						       &rec,
						       (ndr_pull_flags_fn_t)ndr_pull_eventlog_Record);
			if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
				return ndr_map_error2ntstatus(ndr_err);
			}

			NDR_PRINT_DEBUG(eventlog_Record, &rec);
		}

		offset++;
	}

	return status;
}

static NTSTATUS cmd_eventlog_numrecords(struct rpc_pipe_client *cli,
					TALLOC_CTX *mem_ctx,
					int argc,
					const char **argv)
{
	NTSTATUS status;
	struct policy_handle handle;
	uint32_t number = 0;

	if (argc != 2) {
		printf("Usage: %s logname\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = get_eventlog_handle(cli, mem_ctx, argv[1], &handle);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	status = rpccli_eventlog_GetNumRecords(cli, mem_ctx,
					       &handle,
					       &number);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	printf("number of records: %d\n", number);

	return NT_STATUS_OK;
}

static NTSTATUS cmd_eventlog_oldestrecord(struct rpc_pipe_client *cli,
					  TALLOC_CTX *mem_ctx,
					  int argc,
					  const char **argv)
{
	NTSTATUS status;
	struct policy_handle handle;
	uint32_t oldest_entry = 0;

	if (argc != 2) {
		printf("Usage: %s logname\n", argv[0]);
		return NT_STATUS_OK;
	}

	status = get_eventlog_handle(cli, mem_ctx, argv[1], &handle);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	status = rpccli_eventlog_GetOldestRecord(cli, mem_ctx,
						 &handle,
						 &oldest_entry);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	printf("oldest entry: %d\n", oldest_entry);

	return NT_STATUS_OK;
}

struct cmd_set eventlog_commands[] = {
	{ "EVENTLOG" },
	{ "eventlog_readlog",		RPC_RTYPE_NTSTATUS,	cmd_eventlog_readlog,		NULL,	&ndr_table_eventlog.syntax_id,	NULL,	"Read Eventlog", "" },
	{ "eventlog_numrecord",		RPC_RTYPE_NTSTATUS,	cmd_eventlog_numrecords,	NULL,	&ndr_table_eventlog.syntax_id,	NULL,	"Get number of records", "" },
	{ "eventlog_oldestrecord",	RPC_RTYPE_NTSTATUS,	cmd_eventlog_oldestrecord,	NULL,	&ndr_table_eventlog.syntax_id,	NULL,	"Get oldest record", "" },
	{ NULL }
};