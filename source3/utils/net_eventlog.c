/*
 * Samba Unix/Linux SMB client library
 * Distributed SMB/CIFS Server Management Utility
 * Local win32 eventlog interface
 *
 * Copyright (C) Guenther Deschner 2009
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "utils/net.h"

/**
 * Dump an *evt win32 eventlog file
 *
 * @param argc  Standard main() style argc.
 * @param argv  Standard main() style argv. Initial components are already
 *              stripped.
 *
 * @return A shell status integer (0 for success).
 **/

static int net_eventlog_dump(struct net_context *c, int argc,
			     const char **argv)
{
	int ret = -1;
	TALLOC_CTX *ctx = talloc_stackframe();
	enum ndr_err_code ndr_err;
	DATA_BLOB blob;
	struct EVENTLOG_EVT_FILE evt;
	char *s;

	if (argc < 1 || c->display_usage) {
		d_fprintf(stderr, "usage: net eventlog dump <file.evt>\n");
		goto done;
	}

	blob.data = (uint8_t *)file_load(argv[0], &blob.length, 0, ctx);
	if (!blob.data) {
		d_fprintf(stderr, "failed to load evt file: %s\n", argv[0]);
		goto done;
	}

	ndr_err = ndr_pull_struct_blob(&blob, ctx, NULL, &evt,
		   (ndr_pull_flags_fn_t)ndr_pull_EVENTLOG_EVT_FILE);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		d_fprintf(stderr, "evt pull failed: %s\n", ndr_errstr(ndr_err));
		goto done;
	}

	s = NDR_PRINT_STRUCT_STRING(ctx, EVENTLOG_EVT_FILE, &evt);
	if (s) {
		printf("%s\n", s);
	}

	ret = 0;
 done:
	TALLOC_FREE(ctx);
	return ret;
}

/**
 * Import an *evt win32 eventlog file to internal tdb representation
 *
 * @param argc  Standard main() style argc.
 * @param argv  Standard main() style argv. Initial components are already
 *              stripped.
 *
 * @return A shell status integer (0 for success).
 **/

static int net_eventlog_import(struct net_context *c, int argc,
			       const char **argv)
{
	int ret = -1;
	TALLOC_CTX *ctx = talloc_stackframe();
	NTSTATUS status;
	enum ndr_err_code ndr_err;
	DATA_BLOB blob;
	uint32_t num_records = 0;
	uint32_t i;
	ELOG_TDB *etdb = NULL;

	struct EVENTLOGHEADER evt_header;
	struct EVENTLOG_EVT_FILE evt;

	if (argc < 2 || c->display_usage) {
		d_fprintf(stderr, "usage: net eventlog import <file> <eventlog>\n");
		goto done;
	}

	blob.data = (uint8_t *)file_load(argv[0], &blob.length, 0, ctx);
	if (!blob.data) {
		d_fprintf(stderr, "failed to load evt file: %s\n", argv[0]);
		goto done;
	}

	/* dump_data(0, blob.data, blob.length); */
	ndr_err = ndr_pull_struct_blob(&blob, ctx, NULL, &evt_header,
		   (ndr_pull_flags_fn_t)ndr_pull_EVENTLOGHEADER);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		d_fprintf(stderr, "evt header pull failed: %s\n", ndr_errstr(ndr_err));
		goto done;
	}

	if (evt_header.Flags & ELF_LOGFILE_HEADER_WRAP) {
		d_fprintf(stderr, "input file is wrapped, cannot proceed\n");
		goto done;
	}

	ndr_err = ndr_pull_struct_blob(&blob, ctx, NULL, &evt,
		   (ndr_pull_flags_fn_t)ndr_pull_EVENTLOG_EVT_FILE);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		d_fprintf(stderr, "evt pull failed: %s\n", ndr_errstr(ndr_err));
		goto done;
	}

	/* NDR_PRINT_DEBUG(EVENTLOG_EVT_FILE, &evt); */

	etdb = elog_open_tdb(argv[1], false, false);
	if (!etdb) {
		d_fprintf(stderr, "can't open the eventlog TDB (%s)\n", argv[1]);
		goto done;
	}

	num_records = evt.hdr.CurrentRecordNumber - evt.hdr.OldestRecordNumber;

	for (i=0; i<num_records; i++) {
		uint32_t record_number;
		struct eventlog_Record_tdb e;

		status = evlog_evt_entry_to_tdb_entry(ctx, &evt.records[i], &e);
		if (!NT_STATUS_IS_OK(status)) {
			goto done;
		}

		status = evlog_push_record_tdb(ctx, ELOG_TDB_CTX(etdb),
					       &e, &record_number);
		if (!NT_STATUS_IS_OK(status)) {
			d_fprintf(stderr, "can't write to the eventlog: %s\n",
				nt_errstr(status));
			goto done;
		}
	}

	printf("wrote %d entries to tdb\n", i);

	ret = 0;
 done:

	elog_close_tdb(etdb, false);

	TALLOC_FREE(ctx);
	return ret;
}

/**
 * Export internal eventlog tdb representation to an *evt win32 eventlog file
 *
 * @param argc  Standard main() style argc.
 * @param argv  Standard main() style argv. Initial components are already
 *              stripped.
 *
 * @return A shell status integer (0 for success).
 **/

static int net_eventlog_export(struct net_context *c, int argc,
			       const char **argv)
{
	int ret = -1;
	NTSTATUS status;
	TALLOC_CTX *ctx = talloc_stackframe();
	enum ndr_err_code ndr_err;
	DATA_BLOB blob;
	uint32_t num_records = 0;
	struct EVENTLOG_EVT_FILE evt;
	ELOG_TDB *etdb = NULL;
	uint32_t count = 1;
	size_t endoffset = 0;

	if (argc < 2 || c->display_usage) {
		d_fprintf(stderr, "usage: net eventlog export <file> <eventlog>\n");
		goto done;
	}

	etdb = elog_open_tdb(argv[1], false, true);
	if (!etdb) {
		d_fprintf(stderr, "can't open the eventlog TDB (%s)\n", argv[1]);
		goto done;
	}

	ZERO_STRUCT(evt);

	while (1) {

		struct eventlog_Record_tdb *r;
		struct EVENTLOGRECORD e;

		r = evlog_pull_record_tdb(ctx, etdb->tdb, count);
		if (!r) {
			break;
		}

		status = evlog_tdb_entry_to_evt_entry(ctx, r, &e);
		if (!NT_STATUS_IS_OK(status)) {
			goto done;
		}

		endoffset += ndr_size_EVENTLOGRECORD(&e, NULL, 0);

		ADD_TO_ARRAY(ctx, struct EVENTLOGRECORD, e, &evt.records, &num_records);
		count++;
	}

	evt.hdr.StartOffset		= 0x30;
	evt.hdr.EndOffset		= evt.hdr.StartOffset + endoffset;
	evt.hdr.CurrentRecordNumber	= count;
	evt.hdr.OldestRecordNumber	= 1;
	evt.hdr.MaxSize			= tdb_fetch_int32(etdb->tdb, EVT_MAXSIZE);
	evt.hdr.Flags			= 0;
	evt.hdr.Retention		= tdb_fetch_int32(etdb->tdb, EVT_RETENTION);

	if (DEBUGLEVEL >= 10) {
		NDR_PRINT_DEBUG(EVENTLOGHEADER, &evt.hdr);
	}

	evt.eof.BeginRecord		= 0x30;
	evt.eof.EndRecord		= evt.hdr.StartOffset + endoffset;
	evt.eof.CurrentRecordNumber	= evt.hdr.CurrentRecordNumber;
	evt.eof.OldestRecordNumber	= evt.hdr.OldestRecordNumber;

	if (DEBUGLEVEL >= 10) {
		NDR_PRINT_DEBUG(EVENTLOGEOF, &evt.eof);
	}

	ndr_err = ndr_push_struct_blob(&blob, ctx, NULL, &evt,
		   (ndr_push_flags_fn_t)ndr_push_EVENTLOG_EVT_FILE);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		d_fprintf(stderr, "evt push failed: %s\n", ndr_errstr(ndr_err));
		goto done;
	}

	if (!file_save(argv[0], blob.data, blob.length)) {
		d_fprintf(stderr, "failed to save evt file: %s\n", argv[0]);
		goto done;
	}

	ret = 0;
 done:

	elog_close_tdb(etdb, false);

	TALLOC_FREE(ctx);
	return ret;
}

/**
 * 'net rpc eventlog' entrypoint.
 * @param argc  Standard main() style argc.
 * @param argv  Standard main() style argv. Initial components are already
 *              stripped.
 **/

int net_eventlog(struct net_context *c, int argc, const char **argv)
{
	int ret = -1;

	struct functable func[] = {
		{
			"dump",
			net_eventlog_dump,
			NET_TRANSPORT_LOCAL,
			"Dump eventlog",
			"net eventlog dump\n"
			"    Dump win32 *.evt eventlog file"
		},
		{
			"import",
			net_eventlog_import,
			NET_TRANSPORT_LOCAL,
			"Import eventlog",
			"net eventlog import\n"
			"    Import win32 *.evt eventlog file"
		},
		{
			"export",
			net_eventlog_export,
			NET_TRANSPORT_LOCAL,
			"Export eventlog",
			"net eventlog export\n"
			"    Export win32 *.evt eventlog file"
		},


	{ NULL, NULL, 0, NULL, NULL }
	};

	ret = net_run_function(c, argc, argv, "net eventlog", func);

	return ret;
}