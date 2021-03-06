/* 
   Unix SMB/CIFS implementation.
   test suite for samr rpc operations

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2003
   
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
#include "torture/torture.h"
#include "system/time.h"
#include "librpc/gen_ndr/lsa.h"
#include "librpc/gen_ndr/ndr_netlogon.h"
#include "librpc/gen_ndr/ndr_netlogon_c.h"
#include "librpc/gen_ndr/ndr_samr_c.h"
#include "../lib/crypto/crypto.h"
#include "libcli/auth/libcli_auth.h"
#include "libcli/security/security.h"
#include "torture/rpc/rpc.h"
#include "param/param.h"

#include <unistd.h>

#define TEST_ACCOUNT_NAME "samrtorturetest"
#define TEST_ACCOUNT_NAME_PWD "samrpwdlastset"
#define TEST_ALIASNAME "samrtorturetestalias"
#define TEST_GROUPNAME "samrtorturetestgroup"
#define TEST_MACHINENAME "samrtestmach$"
#define TEST_DOMAINNAME "samrtestdom$"

enum torture_samr_choice {
	TORTURE_SAMR_PASSWORDS,
	TORTURE_SAMR_PASSWORDS_PWDLASTSET,
	TORTURE_SAMR_USER_ATTRIBUTES,
	TORTURE_SAMR_OTHER
};

static bool test_QueryUserInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			       struct policy_handle *handle);

static bool test_QueryUserInfo2(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				struct policy_handle *handle);

static bool test_QueryAliasInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			       struct policy_handle *handle);

static bool test_ChangePassword(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				const char *acct_name, 
				struct policy_handle *domain_handle, char **password);

static void init_lsa_String(struct lsa_String *string, const char *s)
{
	string->string = s;
}

static void init_lsa_BinaryString(struct lsa_BinaryString *string, const char *s, uint32_t length)
{
	string->length = length;
	string->size = length;
	string->array = (uint16_t *)discard_const(s);
}

bool test_samr_handle_Close(struct dcerpc_pipe *p, struct torture_context *tctx,
				   struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_Close r;

	r.in.handle = handle;
	r.out.handle = handle;

	status = dcerpc_samr_Close(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "Close");

	return true;
}

static bool test_Shutdown(struct dcerpc_pipe *p, struct torture_context *tctx,
		       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_Shutdown r;

	if (!torture_setting_bool(tctx, "dangerous", false)) {
		torture_skip(tctx, "samr_Shutdown disabled - enable dangerous tests to use\n");
		return true;
	}

	r.in.connect_handle = handle;

	torture_comment(tctx, "testing samr_Shutdown\n");

	status = dcerpc_samr_Shutdown(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "samr_Shutdown");

	return true;
}

static bool test_SetDsrmPassword(struct dcerpc_pipe *p, struct torture_context *tctx,
				 struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_SetDsrmPassword r;
	struct lsa_String string;
	struct samr_Password hash;

	if (!torture_setting_bool(tctx, "dangerous", false)) {
		torture_skip(tctx, "samr_SetDsrmPassword disabled - enable dangerous tests to use");
	}

	E_md4hash("TeSTDSRM123", hash.hash);

	init_lsa_String(&string, "Administrator");

	r.in.name = &string;
	r.in.unknown = 0;
	r.in.hash = &hash;

	torture_comment(tctx, "testing samr_SetDsrmPassword\n");

	status = dcerpc_samr_SetDsrmPassword(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, status, NT_STATUS_NOT_SUPPORTED, "samr_SetDsrmPassword");

	return true;
}


static bool test_QuerySecurity(struct dcerpc_pipe *p, 
			       struct torture_context *tctx, 
			       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QuerySecurity r;
	struct samr_SetSecurity s;
	struct sec_desc_buf *sdbuf = NULL;

	r.in.handle = handle;
	r.in.sec_info = 7;
	r.out.sdbuf = &sdbuf;

	status = dcerpc_samr_QuerySecurity(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "QuerySecurity");

	torture_assert(tctx, sdbuf != NULL, "sdbuf is NULL");

	s.in.handle = handle;
	s.in.sec_info = 7;
	s.in.sdbuf = sdbuf;

	if (torture_setting_bool(tctx, "samba4", false)) {
		torture_skip(tctx, "skipping SetSecurity test against Samba4\n");
	}

	status = dcerpc_samr_SetSecurity(p, tctx, &s);
	torture_assert_ntstatus_ok(tctx, status, "SetSecurity");

	status = dcerpc_samr_QuerySecurity(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "QuerySecurity");

	return true;
}


static bool test_SetUserInfo(struct dcerpc_pipe *p, struct torture_context *tctx, 
			     struct policy_handle *handle, uint32_t base_acct_flags,
			     const char *base_account_name)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	struct samr_SetUserInfo2 s2;
	struct samr_QueryUserInfo q;
	struct samr_QueryUserInfo q0;
	union samr_UserInfo u;
	union samr_UserInfo *info;
	bool ret = true;
	const char *test_account_name;

	uint32_t user_extra_flags = 0;
	if (base_acct_flags == ACB_NORMAL) {
		/* When created, accounts are expired by default */
		user_extra_flags = ACB_PW_EXPIRED;
	}

	s.in.user_handle = handle;
	s.in.info = &u;

	s2.in.user_handle = handle;
	s2.in.info = &u;

	q.in.user_handle = handle;
	q.out.info = &info;
	q0 = q;

#define TESTCALL(call, r) \
		status = dcerpc_samr_ ##call(p, tctx, &r); \
		if (!NT_STATUS_IS_OK(status)) { \
			torture_comment(tctx, #call " level %u failed - %s (%s)\n", \
			       r.in.level, nt_errstr(status), __location__); \
			ret = false; \
			break; \
		}

#define STRING_EQUAL(s1, s2, field) \
		if ((s1 && !s2) || (s2 && !s1) || strcmp(s1, s2)) { \
			torture_comment(tctx, "Failed to set %s to '%s' (%s)\n", \
			       #field, s2, __location__); \
			ret = false; \
			break; \
		}

#define MEM_EQUAL(s1, s2, length, field) \
		if ((s1 && !s2) || (s2 && !s1) || memcmp(s1, s2, length)) { \
			torture_comment(tctx, "Failed to set %s to '%s' (%s)\n", \
			       #field, (const char *)s2, __location__); \
			ret = false; \
			break; \
		}

#define INT_EQUAL(i1, i2, field) \
		if (i1 != i2) { \
			torture_comment(tctx, "Failed to set %s to 0x%llx - got 0x%llx (%s)\n", \
			       #field, (unsigned long long)i2, (unsigned long long)i1, __location__); \
			ret = false; \
			break; \
		}

#define TEST_USERINFO_STRING(lvl1, field1, lvl2, field2, value, fpval) do { \
		torture_comment(tctx, "field test %d/%s vs %d/%s\n", lvl1, #field1, lvl2, #field2); \
		q.in.level = lvl1; \
		TESTCALL(QueryUserInfo, q) \
		s.in.level = lvl1; \
		s2.in.level = lvl1; \
		u = *info; \
		if (lvl1 == 21) { \
			ZERO_STRUCT(u.info21); \
			u.info21.fields_present = fpval; \
		} \
		init_lsa_String(&u.info ## lvl1.field1, value); \
		TESTCALL(SetUserInfo, s) \
		TESTCALL(SetUserInfo2, s2) \
		init_lsa_String(&u.info ## lvl1.field1, ""); \
		TESTCALL(QueryUserInfo, q); \
		u = *info; \
		STRING_EQUAL(u.info ## lvl1.field1.string, value, field1); \
		q.in.level = lvl2; \
		TESTCALL(QueryUserInfo, q) \
		u = *info; \
		STRING_EQUAL(u.info ## lvl2.field2.string, value, field2); \
	} while (0)

#define TEST_USERINFO_BINARYSTRING(lvl1, field1, lvl2, field2, value, fpval) do { \
		torture_comment(tctx, "field test %d/%s vs %d/%s\n", lvl1, #field1, lvl2, #field2); \
		q.in.level = lvl1; \
		TESTCALL(QueryUserInfo, q) \
		s.in.level = lvl1; \
		s2.in.level = lvl1; \
		u = *info; \
		if (lvl1 == 21) { \
			ZERO_STRUCT(u.info21); \
			u.info21.fields_present = fpval; \
		} \
		init_lsa_BinaryString(&u.info ## lvl1.field1, value, strlen(value)); \
		TESTCALL(SetUserInfo, s) \
		TESTCALL(SetUserInfo2, s2) \
		init_lsa_BinaryString(&u.info ## lvl1.field1, "", 1); \
		TESTCALL(QueryUserInfo, q); \
		u = *info; \
		MEM_EQUAL(u.info ## lvl1.field1.array, value, strlen(value), field1); \
		q.in.level = lvl2; \
		TESTCALL(QueryUserInfo, q) \
		u = *info; \
		MEM_EQUAL(u.info ## lvl2.field2.array, value, strlen(value), field2); \
	} while (0)

#define TEST_USERINFO_INT_EXP(lvl1, field1, lvl2, field2, value, exp_value, fpval) do { \
		torture_comment(tctx, "field test %d/%s vs %d/%s\n", lvl1, #field1, lvl2, #field2); \
		q.in.level = lvl1; \
		TESTCALL(QueryUserInfo, q) \
		s.in.level = lvl1; \
		s2.in.level = lvl1; \
		u = *info; \
		if (lvl1 == 21) { \
			uint8_t *bits = u.info21.logon_hours.bits; \
			ZERO_STRUCT(u.info21); \
			if (fpval == SAMR_FIELD_LOGON_HOURS) { \
				u.info21.logon_hours.units_per_week = 168; \
				u.info21.logon_hours.bits = bits; \
			} \
			u.info21.fields_present = fpval; \
		} \
		u.info ## lvl1.field1 = value; \
		TESTCALL(SetUserInfo, s) \
		TESTCALL(SetUserInfo2, s2) \
		u.info ## lvl1.field1 = 0; \
		TESTCALL(QueryUserInfo, q); \
		u = *info; \
		INT_EQUAL(u.info ## lvl1.field1, exp_value, field1); \
		q.in.level = lvl2; \
		TESTCALL(QueryUserInfo, q) \
		u = *info; \
		INT_EQUAL(u.info ## lvl2.field2, exp_value, field1); \
	} while (0)

#define TEST_USERINFO_INT(lvl1, field1, lvl2, field2, value, fpval) do { \
        TEST_USERINFO_INT_EXP(lvl1, field1, lvl2, field2, value, value, fpval); \
        } while (0)

	q0.in.level = 12;
	do { TESTCALL(QueryUserInfo, q0) } while (0);

	TEST_USERINFO_STRING(2, comment,  1, comment, "xx2-1 comment", 0);
	TEST_USERINFO_STRING(2, comment, 21, comment, "xx2-21 comment", 0);
	TEST_USERINFO_STRING(21, comment, 21, comment, "xx21-21 comment", 
			   SAMR_FIELD_COMMENT);

	test_account_name = talloc_asprintf(tctx, "%sxx7-1", base_account_name);
	TEST_USERINFO_STRING(7, account_name,  1, account_name, base_account_name, 0);
	test_account_name = talloc_asprintf(tctx, "%sxx7-3", base_account_name);
	TEST_USERINFO_STRING(7, account_name,  3, account_name, base_account_name, 0);
	test_account_name = talloc_asprintf(tctx, "%sxx7-5", base_account_name);
	TEST_USERINFO_STRING(7, account_name,  5, account_name, base_account_name, 0);
	test_account_name = talloc_asprintf(tctx, "%sxx7-6", base_account_name);
	TEST_USERINFO_STRING(7, account_name,  6, account_name, base_account_name, 0);
	test_account_name = talloc_asprintf(tctx, "%sxx7-7", base_account_name);
	TEST_USERINFO_STRING(7, account_name,  7, account_name, base_account_name, 0);
	test_account_name = talloc_asprintf(tctx, "%sxx7-21", base_account_name);
	TEST_USERINFO_STRING(7, account_name, 21, account_name, base_account_name, 0);
	test_account_name = base_account_name;
	TEST_USERINFO_STRING(21, account_name, 21, account_name, base_account_name, 
			   SAMR_FIELD_ACCOUNT_NAME);

	TEST_USERINFO_STRING(6, full_name,  1, full_name, "xx6-1 full_name", 0);
	TEST_USERINFO_STRING(6, full_name,  3, full_name, "xx6-3 full_name", 0);
	TEST_USERINFO_STRING(6, full_name,  5, full_name, "xx6-5 full_name", 0);
	TEST_USERINFO_STRING(6, full_name,  6, full_name, "xx6-6 full_name", 0);
	TEST_USERINFO_STRING(6, full_name,  8, full_name, "xx6-8 full_name", 0);
	TEST_USERINFO_STRING(6, full_name, 21, full_name, "xx6-21 full_name", 0);
	TEST_USERINFO_STRING(8, full_name, 21, full_name, "xx8-21 full_name", 0);
	TEST_USERINFO_STRING(21, full_name, 21, full_name, "xx21-21 full_name", 
			   SAMR_FIELD_FULL_NAME);

	TEST_USERINFO_STRING(6, full_name,  1, full_name, "", 0);
	TEST_USERINFO_STRING(6, full_name,  3, full_name, "", 0);
	TEST_USERINFO_STRING(6, full_name,  5, full_name, "", 0);
	TEST_USERINFO_STRING(6, full_name,  6, full_name, "", 0);
	TEST_USERINFO_STRING(6, full_name,  8, full_name, "", 0);
	TEST_USERINFO_STRING(6, full_name, 21, full_name, "", 0);
	TEST_USERINFO_STRING(8, full_name, 21, full_name, "", 0);
	TEST_USERINFO_STRING(21, full_name, 21, full_name, "", 
			   SAMR_FIELD_FULL_NAME);

	TEST_USERINFO_STRING(11, logon_script, 3, logon_script, "xx11-3 logon_script", 0);
	TEST_USERINFO_STRING(11, logon_script, 5, logon_script, "xx11-5 logon_script", 0);
	TEST_USERINFO_STRING(11, logon_script, 21, logon_script, "xx11-21 logon_script", 0);
	TEST_USERINFO_STRING(21, logon_script, 21, logon_script, "xx21-21 logon_script", 
			   SAMR_FIELD_LOGON_SCRIPT);

	TEST_USERINFO_STRING(12, profile_path,  3, profile_path, "xx12-3 profile_path", 0);
	TEST_USERINFO_STRING(12, profile_path,  5, profile_path, "xx12-5 profile_path", 0);
	TEST_USERINFO_STRING(12, profile_path, 21, profile_path, "xx12-21 profile_path", 0);
	TEST_USERINFO_STRING(21, profile_path, 21, profile_path, "xx21-21 profile_path", 
			   SAMR_FIELD_PROFILE_PATH);

	TEST_USERINFO_STRING(10, home_directory, 3, home_directory, "xx10-3 home_directory", 0);
	TEST_USERINFO_STRING(10, home_directory, 5, home_directory, "xx10-5 home_directory", 0);
	TEST_USERINFO_STRING(10, home_directory, 21, home_directory, "xx10-21 home_directory", 0);
	TEST_USERINFO_STRING(21, home_directory, 21, home_directory, "xx21-21 home_directory",
			     SAMR_FIELD_HOME_DIRECTORY);
	TEST_USERINFO_STRING(21, home_directory, 10, home_directory, "xx21-10 home_directory",
			     SAMR_FIELD_HOME_DIRECTORY);

	TEST_USERINFO_STRING(10, home_drive, 3, home_drive, "xx10-3 home_drive", 0);
	TEST_USERINFO_STRING(10, home_drive, 5, home_drive, "xx10-5 home_drive", 0);
	TEST_USERINFO_STRING(10, home_drive, 21, home_drive, "xx10-21 home_drive", 0);
	TEST_USERINFO_STRING(21, home_drive, 21, home_drive, "xx21-21 home_drive",
			     SAMR_FIELD_HOME_DRIVE);
	TEST_USERINFO_STRING(21, home_drive, 10, home_drive, "xx21-10 home_drive",
			     SAMR_FIELD_HOME_DRIVE);
	
	TEST_USERINFO_STRING(13, description,  1, description, "xx13-1 description", 0);
	TEST_USERINFO_STRING(13, description,  5, description, "xx13-5 description", 0);
	TEST_USERINFO_STRING(13, description, 21, description, "xx13-21 description", 0);
	TEST_USERINFO_STRING(21, description, 21, description, "xx21-21 description", 
			   SAMR_FIELD_DESCRIPTION);

	TEST_USERINFO_STRING(14, workstations,  3, workstations, "14workstation3", 0);
	TEST_USERINFO_STRING(14, workstations,  5, workstations, "14workstation4", 0);
	TEST_USERINFO_STRING(14, workstations, 21, workstations, "14workstation21", 0);
	TEST_USERINFO_STRING(21, workstations, 21, workstations, "21workstation21", 
			   SAMR_FIELD_WORKSTATIONS);
	TEST_USERINFO_STRING(21, workstations, 3, workstations, "21workstation3", 
			   SAMR_FIELD_WORKSTATIONS);
	TEST_USERINFO_STRING(21, workstations, 5, workstations, "21workstation5", 
			   SAMR_FIELD_WORKSTATIONS);
	TEST_USERINFO_STRING(21, workstations, 14, workstations, "21workstation14", 
			   SAMR_FIELD_WORKSTATIONS);

	TEST_USERINFO_BINARYSTRING(20, parameters, 21, parameters, "xx20-21 parameters", 0);
	TEST_USERINFO_BINARYSTRING(21, parameters, 21, parameters, "xx21-21 parameters",
			   SAMR_FIELD_PARAMETERS);
	TEST_USERINFO_BINARYSTRING(21, parameters, 20, parameters, "xx21-20 parameters",
			   SAMR_FIELD_PARAMETERS);
	/* also empty user parameters are allowed */
	TEST_USERINFO_BINARYSTRING(20, parameters, 21, parameters, "", 0);
	TEST_USERINFO_BINARYSTRING(21, parameters, 21, parameters, "",
			   SAMR_FIELD_PARAMETERS);
	TEST_USERINFO_BINARYSTRING(21, parameters, 20, parameters, "",
			   SAMR_FIELD_PARAMETERS);

	TEST_USERINFO_INT(2, country_code, 2, country_code, __LINE__, 0);
	TEST_USERINFO_INT(2, country_code, 21, country_code, __LINE__, 0);
	TEST_USERINFO_INT(21, country_code, 21, country_code, __LINE__, 
			  SAMR_FIELD_COUNTRY_CODE);
	TEST_USERINFO_INT(21, country_code, 2, country_code, __LINE__, 
			  SAMR_FIELD_COUNTRY_CODE);

	TEST_USERINFO_INT(2, code_page, 21, code_page, __LINE__, 0);
	TEST_USERINFO_INT(21, code_page, 21, code_page, __LINE__, 
			  SAMR_FIELD_CODE_PAGE);
	TEST_USERINFO_INT(21, code_page, 2, code_page, __LINE__, 
			  SAMR_FIELD_CODE_PAGE);

	TEST_USERINFO_INT(17, acct_expiry, 21, acct_expiry, __LINE__, 0);
	TEST_USERINFO_INT(17, acct_expiry, 5, acct_expiry, __LINE__, 0);
	TEST_USERINFO_INT(21, acct_expiry, 21, acct_expiry, __LINE__, 
			  SAMR_FIELD_ACCT_EXPIRY);
	TEST_USERINFO_INT(21, acct_expiry, 5, acct_expiry, __LINE__, 
			  SAMR_FIELD_ACCT_EXPIRY);
	TEST_USERINFO_INT(21, acct_expiry, 17, acct_expiry, __LINE__, 
			  SAMR_FIELD_ACCT_EXPIRY);

	TEST_USERINFO_INT(4, logon_hours.bits[3],  3, logon_hours.bits[3], 1, 0);
	TEST_USERINFO_INT(4, logon_hours.bits[3],  5, logon_hours.bits[3], 2, 0);
	TEST_USERINFO_INT(4, logon_hours.bits[3], 21, logon_hours.bits[3], 3, 0);
	TEST_USERINFO_INT(21, logon_hours.bits[3], 21, logon_hours.bits[3], 4, 
			  SAMR_FIELD_LOGON_HOURS);

	TEST_USERINFO_INT_EXP(16, acct_flags, 5, acct_flags, 
			      (base_acct_flags  | ACB_DISABLED | ACB_HOMDIRREQ), 
			      (base_acct_flags  | ACB_DISABLED | ACB_HOMDIRREQ | user_extra_flags), 
			      0);
	TEST_USERINFO_INT_EXP(16, acct_flags, 5, acct_flags, 
			      (base_acct_flags  | ACB_DISABLED), 
			      (base_acct_flags  | ACB_DISABLED | user_extra_flags), 
			      0);
	
	/* Setting PWNOEXP clears the magic ACB_PW_EXPIRED flag */
	TEST_USERINFO_INT_EXP(16, acct_flags, 5, acct_flags, 
			      (base_acct_flags  | ACB_DISABLED | ACB_PWNOEXP), 
			      (base_acct_flags  | ACB_DISABLED | ACB_PWNOEXP), 
			      0);
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_HOMDIRREQ), 
			      (base_acct_flags | ACB_DISABLED | ACB_HOMDIRREQ | user_extra_flags), 
			      0);


	/* The 'autolock' flag doesn't stick - check this */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_AUTOLOCK), 
			      (base_acct_flags | ACB_DISABLED | user_extra_flags), 
			      0);
#if 0
	/* Removing the 'disabled' flag doesn't stick - check this */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags), 
			      (base_acct_flags | ACB_DISABLED | user_extra_flags), 
			      0);
#endif
	/* The 'store plaintext' flag does stick */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_ENC_TXT_PWD_ALLOWED), 
			      (base_acct_flags | ACB_DISABLED | ACB_ENC_TXT_PWD_ALLOWED | user_extra_flags), 
			      0);
	/* The 'use DES' flag does stick */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_USE_DES_KEY_ONLY), 
			      (base_acct_flags | ACB_DISABLED | ACB_USE_DES_KEY_ONLY | user_extra_flags), 
			      0);
	/* The 'don't require kerberos pre-authentication flag does stick */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_DONT_REQUIRE_PREAUTH), 
			      (base_acct_flags | ACB_DISABLED | ACB_DONT_REQUIRE_PREAUTH | user_extra_flags), 
			      0);
	/* The 'no kerberos PAC required' flag sticks */
	TEST_USERINFO_INT_EXP(16, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED | ACB_NO_AUTH_DATA_REQD), 
			      (base_acct_flags | ACB_DISABLED | ACB_NO_AUTH_DATA_REQD | user_extra_flags), 
			      0);

	TEST_USERINFO_INT_EXP(21, acct_flags, 21, acct_flags, 
			      (base_acct_flags | ACB_DISABLED), 
			      (base_acct_flags | ACB_DISABLED | user_extra_flags), 
			      SAMR_FIELD_ACCT_FLAGS);

#if 0
	/* these fail with win2003 - it appears you can't set the primary gid?
	   the set succeeds, but the gid isn't changed. Very weird! */
	TEST_USERINFO_INT(9, primary_gid,  1, primary_gid, 513);
	TEST_USERINFO_INT(9, primary_gid,  3, primary_gid, 513);
	TEST_USERINFO_INT(9, primary_gid,  5, primary_gid, 513);
	TEST_USERINFO_INT(9, primary_gid, 21, primary_gid, 513);
#endif

	return ret;
}

/*
  generate a random password for password change tests
*/
static char *samr_rand_pass_silent(TALLOC_CTX *mem_ctx, int min_len)
{
	size_t len = MAX(8, min_len) + (random() % 6);
	char *s = generate_random_str(mem_ctx, len);
	return s;
}

static char *samr_rand_pass(TALLOC_CTX *mem_ctx, int min_len)
{
	char *s = samr_rand_pass_silent(mem_ctx, min_len);
	printf("Generated password '%s'\n", s);
	return s;

}

/*
  generate a random password for password change tests
*/
static DATA_BLOB samr_very_rand_pass(TALLOC_CTX *mem_ctx, int len)
{
	int i;
	DATA_BLOB password = data_blob_talloc(mem_ctx, NULL, len * 2 /* number of unicode chars */);
	generate_random_buffer(password.data, password.length);

	for (i=0; i < len; i++) {
		if (((uint16_t *)password.data)[i] == 0) {
			((uint16_t *)password.data)[i] = 1;
		}
	}

	return password;
}

/*
  generate a random password for password change tests (fixed length)
*/
static char *samr_rand_pass_fixed_len(TALLOC_CTX *mem_ctx, int len)
{
	char *s = generate_random_str(mem_ctx, len);
	printf("Generated password '%s'\n", s);
	return s;
}

static bool test_SetUserPass(struct dcerpc_pipe *p, struct torture_context *tctx,
			     struct policy_handle *handle, char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 24;

	encode_pw_buffer(u.info24.password.data, newpass, STR_UNICODE);
	u.info24.password_expired = 0;

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	arcfour_crypt_blob(u.info24.password.data, 516, &session_key);

	torture_comment(tctx, "Testing SetUserInfo level 24 (set password)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}


static bool test_SetUserPass_23(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct policy_handle *handle, uint32_t fields_present,
				char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 23;

	ZERO_STRUCT(u);

	u.info23.info.fields_present = fields_present;

	encode_pw_buffer(u.info23.password.data, newpass, STR_UNICODE);

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	arcfour_crypt_blob(u.info23.password.data, 516, &session_key);

	torture_comment(tctx, "Testing SetUserInfo level 23 (set password)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	encode_pw_buffer(u.info23.password.data, newpass, STR_UNICODE);

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	/* This should break the key nicely */
	session_key.length--;
	arcfour_crypt_blob(u.info23.password.data, 516, &session_key);

	torture_comment(tctx, "Testing SetUserInfo level 23 (set password) with wrong password\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("SetUserInfo level %u should have failed with WRONG_PASSWORD- %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	}

	return ret;
}


static bool test_SetUserPassEx(struct dcerpc_pipe *p, struct torture_context *tctx,
			       struct policy_handle *handle, bool makeshort, 
			       char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	DATA_BLOB confounded_session_key = data_blob_talloc(tctx, NULL, 16);
	uint8_t confounder[16];
	char *newpass;
	struct MD5Context ctx;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	if (makeshort && policy_min_pw_len) {
		newpass = samr_rand_pass_fixed_len(tctx, policy_min_pw_len - 1);
	} else {
		newpass = samr_rand_pass(tctx, policy_min_pw_len);
	}

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 26;

	encode_pw_buffer(u.info26.password.data, newpass, STR_UNICODE);
	u.info26.password_expired = 0;

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	generate_random_buffer((uint8_t *)confounder, 16);

	MD5Init(&ctx);
	MD5Update(&ctx, confounder, 16);
	MD5Update(&ctx, session_key.data, session_key.length);
	MD5Final(confounded_session_key.data, &ctx);

	arcfour_crypt_blob(u.info26.password.data, 516, &confounded_session_key);
	memcpy(&u.info26.password.data[516], confounder, 16);

	torture_comment(tctx, "Testing SetUserInfo level 26 (set password ex)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	/* This should break the key nicely */
	confounded_session_key.data[0]++;

	arcfour_crypt_blob(u.info26.password.data, 516, &confounded_session_key);
	memcpy(&u.info26.password.data[516], confounder, 16);

	torture_comment(tctx, "Testing SetUserInfo level 26 (set password ex) with wrong session key\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("SetUserInfo level %u should have failed with WRONG_PASSWORD: %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}

static bool test_SetUserPass_25(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct policy_handle *handle, uint32_t fields_present,
				char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	DATA_BLOB confounded_session_key = data_blob_talloc(tctx, NULL, 16);
	struct MD5Context ctx;
	uint8_t confounder[16];
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 25;

	ZERO_STRUCT(u);

	u.info25.info.fields_present = fields_present;

	encode_pw_buffer(u.info25.password.data, newpass, STR_UNICODE);

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	generate_random_buffer((uint8_t *)confounder, 16);

	MD5Init(&ctx);
	MD5Update(&ctx, confounder, 16);
	MD5Update(&ctx, session_key.data, session_key.length);
	MD5Final(confounded_session_key.data, &ctx);

	arcfour_crypt_blob(u.info25.password.data, 516, &confounded_session_key);
	memcpy(&u.info25.password.data[516], confounder, 16);

	torture_comment(tctx, "Testing SetUserInfo level 25 (set password ex)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	/* This should break the key nicely */
	confounded_session_key.data[0]++;

	arcfour_crypt_blob(u.info25.password.data, 516, &confounded_session_key);
	memcpy(&u.info25.password.data[516], confounder, 16);

	torture_comment(tctx, "Testing SetUserInfo level 25 (set password ex) with wrong session key\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("SetUserInfo level %u should have failed with WRONG_PASSWORD- %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	}

	return ret;
}

static bool test_SetUserPass_18(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct policy_handle *handle, char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	uint8_t lm_hash[16], nt_hash[16];

	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 18;

	ZERO_STRUCT(u);

	u.info18.nt_pwd_active = true;
	u.info18.lm_pwd_active = true;

	E_md4hash(newpass, nt_hash);
	E_deshash(newpass, lm_hash);

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	{
		DATA_BLOB in,out;
		in = data_blob_const(nt_hash, 16);
		out = data_blob_talloc_zero(tctx, 16);
		sess_crypt_blob(&out, &in, &session_key, true);
		memcpy(u.info18.nt_pwd.hash, out.data, out.length);
	}
	{
		DATA_BLOB in,out;
		in = data_blob_const(lm_hash, 16);
		out = data_blob_talloc_zero(tctx, 16);
		sess_crypt_blob(&out, &in, &session_key, true);
		memcpy(u.info18.lm_pwd.hash, out.data, out.length);
	}

	torture_comment(tctx, "Testing SetUserInfo level 18 (set password hash)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}

static bool test_SetUserPass_21(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct policy_handle *handle, uint32_t fields_present,
				char **password)
{
	NTSTATUS status;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	uint8_t lm_hash[16], nt_hash[16];

	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 21;

	E_md4hash(newpass, nt_hash);
	E_deshash(newpass, lm_hash);

	ZERO_STRUCT(u);

	u.info21.fields_present = fields_present;

	if (fields_present & SAMR_FIELD_LM_PASSWORD_PRESENT) {
		u.info21.lm_owf_password.length = 16;
		u.info21.lm_owf_password.size = 16;
		u.info21.lm_owf_password.array = (uint16_t *)lm_hash;
		u.info21.lm_password_set = true;
	}

	if (fields_present & SAMR_FIELD_NT_PASSWORD_PRESENT) {
		u.info21.nt_owf_password.length = 16;
		u.info21.nt_owf_password.size = 16;
		u.info21.nt_owf_password.array = (uint16_t *)nt_hash;
		u.info21.nt_password_set = true;
	}

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	if (fields_present & SAMR_FIELD_LM_PASSWORD_PRESENT) {
		DATA_BLOB in,out;
		in = data_blob_const(u.info21.lm_owf_password.array,
				     u.info21.lm_owf_password.length);
		out = data_blob_talloc_zero(tctx, 16);
		sess_crypt_blob(&out, &in, &session_key, true);
		u.info21.lm_owf_password.array = (uint16_t *)out.data;
	}

	if (fields_present & SAMR_FIELD_NT_PASSWORD_PRESENT) {
		DATA_BLOB in,out;
		in = data_blob_const(u.info21.nt_owf_password.array,
				     u.info21.nt_owf_password.length);
		out = data_blob_talloc_zero(tctx, 16);
		sess_crypt_blob(&out, &in, &session_key, true);
		u.info21.nt_owf_password.array = (uint16_t *)out.data;
	}

	torture_comment(tctx, "Testing SetUserInfo level 21 (set password hash)\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	/* try invalid length */
	if (fields_present & SAMR_FIELD_NT_PASSWORD_PRESENT) {

		u.info21.nt_owf_password.length++;

		status = dcerpc_samr_SetUserInfo(p, tctx, &s);

		if (!NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
			printf("SetUserInfo level %u should have failed with NT_STATUS_INVALID_PARAMETER - %s\n",
			       s.in.level, nt_errstr(status));
			ret = false;
		}
	}

	if (fields_present & SAMR_FIELD_LM_PASSWORD_PRESENT) {

		u.info21.lm_owf_password.length++;

		status = dcerpc_samr_SetUserInfo(p, tctx, &s);

		if (!NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
			printf("SetUserInfo level %u should have failed with NT_STATUS_INVALID_PARAMETER - %s\n",
			       s.in.level, nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_SetUserPass_level_ex(struct dcerpc_pipe *p,
				      struct torture_context *tctx,
				      struct policy_handle *handle,
				      uint16_t level,
				      uint32_t fields_present,
				      char **password, uint8_t password_expired,
				      bool use_setinfo2,
				      bool *matched_expected_error)
{
	NTSTATUS status;
	NTSTATUS expected_error = NT_STATUS_OK;
	struct samr_SetUserInfo s;
	struct samr_SetUserInfo2 s2;
	union samr_UserInfo u;
	bool ret = true;
	DATA_BLOB session_key;
	DATA_BLOB confounded_session_key = data_blob_talloc(tctx, NULL, 16);
	struct MD5Context ctx;
	uint8_t confounder[16];
	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;
	const char *comment = NULL;
	uint8_t lm_hash[16], nt_hash[16];

	pwp.in.user_handle = handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass_silent(tctx, policy_min_pw_len);

	if (use_setinfo2) {
		s2.in.user_handle = handle;
		s2.in.info = &u;
		s2.in.level = level;
	} else {
		s.in.user_handle = handle;
		s.in.info = &u;
		s.in.level = level;
	}

	if (fields_present & SAMR_FIELD_COMMENT) {
		comment = talloc_asprintf(tctx, "comment: %ld\n", time(NULL));
	}

	ZERO_STRUCT(u);

	switch (level) {
	case 18:
		E_md4hash(newpass, nt_hash);
		E_deshash(newpass, lm_hash);

		u.info18.nt_pwd_active = true;
		u.info18.lm_pwd_active = true;
		u.info18.password_expired = password_expired;

		memcpy(u.info18.lm_pwd.hash, lm_hash, 16);
		memcpy(u.info18.nt_pwd.hash, nt_hash, 16);

		break;
	case 21:
		E_md4hash(newpass, nt_hash);
		E_deshash(newpass, lm_hash);

		u.info21.fields_present = fields_present;
		u.info21.password_expired = password_expired;
		u.info21.comment.string = comment;

		if (fields_present & SAMR_FIELD_LM_PASSWORD_PRESENT) {
			u.info21.lm_owf_password.length = 16;
			u.info21.lm_owf_password.size = 16;
			u.info21.lm_owf_password.array = (uint16_t *)lm_hash;
			u.info21.lm_password_set = true;
		}

		if (fields_present & SAMR_FIELD_NT_PASSWORD_PRESENT) {
			u.info21.nt_owf_password.length = 16;
			u.info21.nt_owf_password.size = 16;
			u.info21.nt_owf_password.array = (uint16_t *)nt_hash;
			u.info21.nt_password_set = true;
		}

		break;
	case 23:
		u.info23.info.fields_present = fields_present;
		u.info23.info.password_expired = password_expired;
		u.info23.info.comment.string = comment;

		encode_pw_buffer(u.info23.password.data, newpass, STR_UNICODE);

		break;
	case 24:
		u.info24.password_expired = password_expired;

		encode_pw_buffer(u.info24.password.data, newpass, STR_UNICODE);

		break;
	case 25:
		u.info25.info.fields_present = fields_present;
		u.info25.info.password_expired = password_expired;
		u.info25.info.comment.string = comment;

		encode_pw_buffer(u.info25.password.data, newpass, STR_UNICODE);

		break;
	case 26:
		u.info26.password_expired = password_expired;

		encode_pw_buffer(u.info26.password.data, newpass, STR_UNICODE);

		break;
	}

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	generate_random_buffer((uint8_t *)confounder, 16);

	MD5Init(&ctx);
	MD5Update(&ctx, confounder, 16);
	MD5Update(&ctx, session_key.data, session_key.length);
	MD5Final(confounded_session_key.data, &ctx);

	switch (level) {
	case 18:
		{
			DATA_BLOB in,out;
			in = data_blob_const(u.info18.nt_pwd.hash, 16);
			out = data_blob_talloc_zero(tctx, 16);
			sess_crypt_blob(&out, &in, &session_key, true);
			memcpy(u.info18.nt_pwd.hash, out.data, out.length);
		}
		{
			DATA_BLOB in,out;
			in = data_blob_const(u.info18.lm_pwd.hash, 16);
			out = data_blob_talloc_zero(tctx, 16);
			sess_crypt_blob(&out, &in, &session_key, true);
			memcpy(u.info18.lm_pwd.hash, out.data, out.length);
		}

		break;
	case 21:
		if (fields_present & SAMR_FIELD_LM_PASSWORD_PRESENT) {
			DATA_BLOB in,out;
			in = data_blob_const(u.info21.lm_owf_password.array,
					     u.info21.lm_owf_password.length);
			out = data_blob_talloc_zero(tctx, 16);
			sess_crypt_blob(&out, &in, &session_key, true);
			u.info21.lm_owf_password.array = (uint16_t *)out.data;
		}
		if (fields_present & SAMR_FIELD_NT_PASSWORD_PRESENT) {
			DATA_BLOB in,out;
			in = data_blob_const(u.info21.nt_owf_password.array,
					     u.info21.nt_owf_password.length);
			out = data_blob_talloc_zero(tctx, 16);
			sess_crypt_blob(&out, &in, &session_key, true);
			u.info21.nt_owf_password.array = (uint16_t *)out.data;
		}
		break;
	case 23:
		arcfour_crypt_blob(u.info23.password.data, 516, &session_key);
		break;
	case 24:
		arcfour_crypt_blob(u.info24.password.data, 516, &session_key);
		break;
	case 25:
		arcfour_crypt_blob(u.info25.password.data, 516, &confounded_session_key);
		memcpy(&u.info25.password.data[516], confounder, 16);
		break;
	case 26:
		arcfour_crypt_blob(u.info26.password.data, 516, &confounded_session_key);
		memcpy(&u.info26.password.data[516], confounder, 16);
		break;
	}

	if (use_setinfo2) {
		status = dcerpc_samr_SetUserInfo2(p, tctx, &s2);
	} else {
		status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	}

	if (!NT_STATUS_IS_OK(status)) {
		if (fields_present == 0) {
			expected_error = NT_STATUS_INVALID_PARAMETER;
		}
		if (fields_present & SAMR_FIELD_LAST_PWD_CHANGE) {
			expected_error = NT_STATUS_ACCESS_DENIED;
		}
	}

	if (!NT_STATUS_IS_OK(expected_error)) {
		if (use_setinfo2) {
			torture_assert_ntstatus_equal(tctx,
				s2.out.result,
				expected_error, "SetUserInfo2 failed");
		} else {
			torture_assert_ntstatus_equal(tctx,
				s.out.result,
				expected_error, "SetUserInfo failed");
		}
		*matched_expected_error = true;
		return true;
	}

	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo%s level %u failed - %s\n",
		       use_setinfo2 ? "2":"", level, nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}

static bool test_SetAliasInfo(struct dcerpc_pipe *p, struct torture_context *tctx,
			       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_SetAliasInfo r;
	struct samr_QueryAliasInfo q;
	union samr_AliasInfo *info;
	uint16_t levels[] = {2, 3};
	int i;
	bool ret = true;

	/* Ignoring switch level 1, as that includes the number of members for the alias
	 * and setting this to a wrong value might have negative consequences
	 */

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		torture_comment(tctx, "Testing SetAliasInfo level %u\n", levels[i]);

		r.in.alias_handle = handle;
		r.in.level = levels[i];
		r.in.info  = talloc(tctx, union samr_AliasInfo);
		switch (r.in.level) {
		    case ALIASINFONAME: init_lsa_String(&r.in.info->name,TEST_ALIASNAME); break;
		    case ALIASINFODESCRIPTION: init_lsa_String(&r.in.info->description,
				"Test Description, should test I18N as well"); break;
		    case ALIASINFOALL: printf("ALIASINFOALL ignored\n"); break;
		}

		status = dcerpc_samr_SetAliasInfo(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("SetAliasInfo level %u failed - %s\n",
			       levels[i], nt_errstr(status));
			ret = false;
		}

		q.in.alias_handle = handle;
		q.in.level = levels[i];
		q.out.info = &info;

		status = dcerpc_samr_QueryAliasInfo(p, tctx, &q);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryAliasInfo level %u failed - %s\n",
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_GetGroupsForUser(struct dcerpc_pipe *p, struct torture_context *tctx,
				  struct policy_handle *user_handle)
{
	struct samr_GetGroupsForUser r;
	struct samr_RidWithAttributeArray *rids = NULL;
	NTSTATUS status;

	torture_comment(tctx, "testing GetGroupsForUser\n");

	r.in.user_handle = user_handle;
	r.out.rids = &rids;

	status = dcerpc_samr_GetGroupsForUser(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetGroupsForUser");

	return true;

}

static bool test_GetDomPwInfo(struct dcerpc_pipe *p, struct torture_context *tctx,
			      struct lsa_String *domain_name)
{
	NTSTATUS status;
	struct samr_GetDomPwInfo r;
	struct samr_PwInfo info;

	r.in.domain_name = domain_name;
	r.out.info = &info;

	torture_comment(tctx, "Testing GetDomPwInfo with name %s\n", r.in.domain_name->string);

	status = dcerpc_samr_GetDomPwInfo(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetDomPwInfo");

	r.in.domain_name->string = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	torture_comment(tctx, "Testing GetDomPwInfo with name %s\n", r.in.domain_name->string);

	status = dcerpc_samr_GetDomPwInfo(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetDomPwInfo");

	r.in.domain_name->string = "\\\\__NONAME__";
	torture_comment(tctx, "Testing GetDomPwInfo with name %s\n", r.in.domain_name->string);

	status = dcerpc_samr_GetDomPwInfo(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetDomPwInfo");

	r.in.domain_name->string = "\\\\Builtin";
	torture_comment(tctx, "Testing GetDomPwInfo with name %s\n", r.in.domain_name->string);

	status = dcerpc_samr_GetDomPwInfo(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetDomPwInfo");

	return true;
}

static bool test_GetUserPwInfo(struct dcerpc_pipe *p, struct torture_context *tctx,
			       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_GetUserPwInfo r;
	struct samr_PwInfo info;

	torture_comment(tctx, "Testing GetUserPwInfo\n");

	r.in.user_handle = handle;
	r.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetUserPwInfo");

	return true;
}

static NTSTATUS test_LookupName(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct policy_handle *domain_handle, const char *name,
				uint32_t *rid)
{
	NTSTATUS status;
	struct samr_LookupNames n;
	struct lsa_String sname[2];
	struct samr_Ids rids, types;

	init_lsa_String(&sname[0], name);

	n.in.domain_handle = domain_handle;
	n.in.num_names = 1;
	n.in.names = sname;
	n.out.rids = &rids;
	n.out.types = &types;
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (NT_STATUS_IS_OK(status)) {
		*rid = n.out.rids->ids[0];
	} else {
		return status;
	}

	init_lsa_String(&sname[1], "xxNONAMExx");
	n.in.num_names = 2;
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (!NT_STATUS_EQUAL(status, STATUS_SOME_UNMAPPED)) {
		printf("LookupNames[2] failed - %s\n", nt_errstr(status));		
		if (NT_STATUS_IS_OK(status)) {
			return NT_STATUS_UNSUCCESSFUL;
		}
		return status;
	}

	n.in.num_names = 0;
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (!NT_STATUS_IS_OK(status)) {
		printf("LookupNames[0] failed - %s\n", nt_errstr(status));		
		return status;
	}

	init_lsa_String(&sname[0], "xxNONAMExx");
	n.in.num_names = 1;
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_NONE_MAPPED)) {
		printf("LookupNames[1 bad name] failed - %s\n", nt_errstr(status));		
		if (NT_STATUS_IS_OK(status)) {
			return NT_STATUS_UNSUCCESSFUL;
		}
		return status;
	}

	init_lsa_String(&sname[0], "xxNONAMExx");
	init_lsa_String(&sname[1], "xxNONAME2xx");
	n.in.num_names = 2;
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_NONE_MAPPED)) {
		printf("LookupNames[2 bad names] failed - %s\n", nt_errstr(status));		
		if (NT_STATUS_IS_OK(status)) {
			return NT_STATUS_UNSUCCESSFUL;
		}
		return status;
	}

	return NT_STATUS_OK;
}

static NTSTATUS test_OpenUser_byname(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				     struct policy_handle *domain_handle,
				     const char *name, struct policy_handle *user_handle)
{
	NTSTATUS status;
	struct samr_OpenUser r;
	uint32_t rid;

	status = test_LookupName(p, mem_ctx, domain_handle, name, &rid);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	r.in.domain_handle = domain_handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.user_handle = user_handle;
	status = dcerpc_samr_OpenUser(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenUser_byname(%s -> %d) failed - %s\n", name, rid, nt_errstr(status));
	}

	return status;
}

#if 0
static bool test_ChangePasswordNT3(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				   struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_ChangePasswordUser r;
	bool ret = true;
	struct samr_Password hash1, hash2, hash3, hash4, hash5, hash6;
	struct policy_handle user_handle;
	char *oldpass = "test";
	char *newpass = "test2";
	uint8_t old_nt_hash[16], new_nt_hash[16];
	uint8_t old_lm_hash[16], new_lm_hash[16];

	status = test_OpenUser_byname(p, mem_ctx, handle, "testuser", &user_handle);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	printf("Testing ChangePasswordUser for user 'testuser'\n");

	printf("old password: %s\n", oldpass);
	printf("new password: %s\n", newpass);

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);
	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	E_old_pw_hash(new_lm_hash, old_lm_hash, hash1.hash);
	E_old_pw_hash(old_lm_hash, new_lm_hash, hash2.hash);
	E_old_pw_hash(new_nt_hash, old_nt_hash, hash3.hash);
	E_old_pw_hash(old_nt_hash, new_nt_hash, hash4.hash);
	E_old_pw_hash(old_lm_hash, new_nt_hash, hash5.hash);
	E_old_pw_hash(old_nt_hash, new_lm_hash, hash6.hash);

	r.in.handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("ChangePasswordUser failed - %s\n", nt_errstr(status));
		ret = false;
	}

	if (!test_samr_handle_Close(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	return ret;
}
#endif

static bool test_ChangePasswordUser(struct dcerpc_pipe *p, struct torture_context *tctx,
				    const char *acct_name, 
				    struct policy_handle *handle, char **password)
{
	NTSTATUS status;
	struct samr_ChangePasswordUser r;
	bool ret = true;
	struct samr_Password hash1, hash2, hash3, hash4, hash5, hash6;
	struct policy_handle user_handle;
	char *oldpass;
	uint8_t old_nt_hash[16], new_nt_hash[16];
	uint8_t old_lm_hash[16], new_lm_hash[16];
	bool changed = true;

	char *newpass;
	struct samr_GetUserPwInfo pwp;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;

	status = test_OpenUser_byname(p, tctx, handle, acct_name, &user_handle);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}
	pwp.in.user_handle = &user_handle;
	pwp.out.info = &info;

	status = dcerpc_samr_GetUserPwInfo(p, tctx, &pwp);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = pwp.out.info->min_password_length;
	}
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	torture_comment(tctx, "Testing ChangePasswordUser\n");

	torture_assert(tctx, *password != NULL, 
				   "Failing ChangePasswordUser as old password was NULL.  Previous test failed?");

	oldpass = *password;

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);
	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	E_old_pw_hash(new_lm_hash, old_lm_hash, hash1.hash);
	E_old_pw_hash(old_lm_hash, new_lm_hash, hash2.hash);
	E_old_pw_hash(new_nt_hash, old_nt_hash, hash3.hash);
	E_old_pw_hash(old_nt_hash, new_nt_hash, hash4.hash);
	E_old_pw_hash(old_lm_hash, new_nt_hash, hash5.hash);
	E_old_pw_hash(old_nt_hash, new_lm_hash, hash6.hash);

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	/* Break the LM hash */
	hash1.hash[0]++;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, status, NT_STATUS_WRONG_PASSWORD,
		"ChangePasswordUser failed: expected NT_STATUS_WRONG_PASSWORD because we broke the LM hash");

	/* Unbreak the LM hash */
	hash1.hash[0]--;

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	/* Break the NT hash */
	hash3.hash[0]--;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, status, NT_STATUS_WRONG_PASSWORD, 
		"expected NT_STATUS_WRONG_PASSWORD because we broke the NT hash");

	/* Unbreak the NT hash */
	hash3.hash[0]--;

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	/* Break the LM cross */
	hash6.hash[0]++;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("ChangePasswordUser failed: expected NT_STATUS_WRONG_PASSWORD because we broke the LM cross-hash, got %s\n", nt_errstr(status));
		ret = false;
	}

	/* Unbreak the LM cross */
	hash6.hash[0]--;

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	/* Break the NT cross */
	hash5.hash[0]++;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("ChangePasswordUser failed: expected NT_STATUS_WRONG_PASSWORD because we broke the NT cross-hash, got %s\n", nt_errstr(status));
		ret = false;
	}

	/* Unbreak the NT cross */
	hash5.hash[0]--;


	/* Reset the hashes to not broken values */
	E_old_pw_hash(new_lm_hash, old_lm_hash, hash1.hash);
	E_old_pw_hash(old_lm_hash, new_lm_hash, hash2.hash);
	E_old_pw_hash(new_nt_hash, old_nt_hash, hash3.hash);
	E_old_pw_hash(old_nt_hash, new_nt_hash, hash4.hash);
	E_old_pw_hash(old_lm_hash, new_nt_hash, hash5.hash);
	E_old_pw_hash(old_nt_hash, new_lm_hash, hash6.hash);

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 0;
	r.in.lm_cross = NULL;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	if (NT_STATUS_IS_OK(status)) {
		changed = true;
		*password = newpass;
	} else if (!NT_STATUS_EQUAL(NT_STATUS_PASSWORD_RESTRICTION, status)) {
		printf("ChangePasswordUser failed: expected NT_STATUS_OK, or at least NT_STATUS_PASSWORD_RESTRICTION, got %s\n", nt_errstr(status));
		ret = false;
	}

	oldpass = newpass;
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);
	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);


	/* Reset the hashes to not broken values */
	E_old_pw_hash(new_lm_hash, old_lm_hash, hash1.hash);
	E_old_pw_hash(old_lm_hash, new_lm_hash, hash2.hash);
	E_old_pw_hash(new_nt_hash, old_nt_hash, hash3.hash);
	E_old_pw_hash(old_nt_hash, new_nt_hash, hash4.hash);
	E_old_pw_hash(old_lm_hash, new_nt_hash, hash5.hash);
	E_old_pw_hash(old_nt_hash, new_lm_hash, hash6.hash);

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 0;
	r.in.nt_cross = NULL;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	if (NT_STATUS_IS_OK(status)) {
		changed = true;
		*password = newpass;
	} else if (!NT_STATUS_EQUAL(NT_STATUS_PASSWORD_RESTRICTION, status)) {
		printf("ChangePasswordUser failed: expected NT_STATUS_NT_CROSS_ENCRYPTION_REQUIRED, got %s\n", nt_errstr(status));
		ret = false;
	}

	oldpass = newpass;
	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);
	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);


	/* Reset the hashes to not broken values */
	E_old_pw_hash(new_lm_hash, old_lm_hash, hash1.hash);
	E_old_pw_hash(old_lm_hash, new_lm_hash, hash2.hash);
	E_old_pw_hash(new_nt_hash, old_nt_hash, hash3.hash);
	E_old_pw_hash(old_nt_hash, new_nt_hash, hash4.hash);
	E_old_pw_hash(old_lm_hash, new_nt_hash, hash5.hash);
	E_old_pw_hash(old_nt_hash, new_lm_hash, hash6.hash);

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
	if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		printf("ChangePasswordUser returned: %s perhaps min password age? (not fatal)\n", nt_errstr(status));
	} else 	if (!NT_STATUS_IS_OK(status)) {
		printf("ChangePasswordUser failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		changed = true;
		*password = newpass;
	}

	r.in.user_handle = &user_handle;
	r.in.lm_present = 1;
	r.in.old_lm_crypted = &hash1;
	r.in.new_lm_crypted = &hash2;
	r.in.nt_present = 1;
	r.in.old_nt_crypted = &hash3;
	r.in.new_nt_crypted = &hash4;
	r.in.cross1_present = 1;
	r.in.nt_cross = &hash5;
	r.in.cross2_present = 1;
	r.in.lm_cross = &hash6;

	if (changed) {
		status = dcerpc_samr_ChangePasswordUser(p, tctx, &r);
		if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
			printf("ChangePasswordUser returned: %s perhaps min password age? (not fatal)\n", nt_errstr(status));
		} else if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
			printf("ChangePasswordUser failed: expected NT_STATUS_WRONG_PASSWORD because we already changed the password, got %s\n", nt_errstr(status));
			ret = false;
		}
	}

	
	if (!test_samr_handle_Close(p, tctx, &user_handle)) {
		ret = false;
	}

	return ret;
}


static bool test_OemChangePasswordUser2(struct dcerpc_pipe *p, struct torture_context *tctx,
					const char *acct_name,
					struct policy_handle *handle, char **password)
{
	NTSTATUS status;
	struct samr_OemChangePasswordUser2 r;
	bool ret = true;
	struct samr_Password lm_verifier;
	struct samr_CryptPassword lm_pass;
	struct lsa_AsciiString server, account, account_bad;
	char *oldpass;
	char *newpass;
	uint8_t old_lm_hash[16], new_lm_hash[16];

	struct samr_GetDomPwInfo dom_pw_info;
	struct samr_PwInfo info;
	int policy_min_pw_len = 0;

	struct lsa_String domain_name;

	domain_name.string = "";
	dom_pw_info.in.domain_name = &domain_name;
	dom_pw_info.out.info = &info;

	torture_comment(tctx, "Testing OemChangePasswordUser2\n");

	torture_assert(tctx, *password != NULL, 
				   "Failing OemChangePasswordUser2 as old password was NULL.  Previous test failed?");

	oldpass = *password;

	status = dcerpc_samr_GetDomPwInfo(p, tctx, &dom_pw_info);
	if (NT_STATUS_IS_OK(status)) {
		policy_min_pw_len = dom_pw_info.out.info->min_password_length;
	}

	newpass = samr_rand_pass(tctx, policy_min_pw_len);

	server.string = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	account.string = acct_name;

	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	encode_pw_buffer(lm_pass.data, newpass, STR_ASCII);
	arcfour_crypt(lm_pass.data, old_lm_hash, 516);
	E_old_pw_hash(new_lm_hash, old_lm_hash, lm_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.password = &lm_pass;
	r.in.hash = &lm_verifier;

	/* Break the verification */
	lm_verifier.hash[0]++;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)
	    && !NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("OemChangePasswordUser2 failed, should have returned WRONG_PASSWORD (or at least 'PASSWORD_RESTRICTON') for invalid password verifier - %s\n",
			nt_errstr(status));
		ret = false;
	}

	encode_pw_buffer(lm_pass.data, newpass, STR_ASCII);
	/* Break the old password */
	old_lm_hash[0]++;
	arcfour_crypt(lm_pass.data, old_lm_hash, 516);
	/* unbreak it for the next operation */
	old_lm_hash[0]--;
	E_old_pw_hash(new_lm_hash, old_lm_hash, lm_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.password = &lm_pass;
	r.in.hash = &lm_verifier;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)
	    && !NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("OemChangePasswordUser2 failed, should have returned WRONG_PASSWORD (or at least 'PASSWORD_RESTRICTON') for invalidly encrpted password - %s\n",
			nt_errstr(status));
		ret = false;
	}

	encode_pw_buffer(lm_pass.data, newpass, STR_ASCII);
	arcfour_crypt(lm_pass.data, old_lm_hash, 516);

	r.in.server = &server;
	r.in.account = &account;
	r.in.password = &lm_pass;
	r.in.hash = NULL;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)
	    && !NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
		printf("OemChangePasswordUser2 failed, should have returned INVALID_PARAMETER (or at least 'PASSWORD_RESTRICTON') for no supplied validation hash - %s\n",
			nt_errstr(status));
		ret = false;
	}

	/* This shouldn't be a valid name */
	account_bad.string = TEST_ACCOUNT_NAME "XX";
	r.in.account = &account_bad;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
		printf("OemChangePasswordUser2 failed, should have returned INVALID_PARAMETER for no supplied validation hash and invalid user - %s\n",
			nt_errstr(status));
		ret = false;
	}

	/* This shouldn't be a valid name */
	account_bad.string = TEST_ACCOUNT_NAME "XX";
	r.in.account = &account_bad;
	r.in.password = &lm_pass;
	r.in.hash = &lm_verifier;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("OemChangePasswordUser2 failed, should have returned WRONG_PASSWORD for invalid user - %s\n",
			nt_errstr(status));
		ret = false;
	}

	/* This shouldn't be a valid name */
	account_bad.string = TEST_ACCOUNT_NAME "XX";
	r.in.account = &account_bad;
	r.in.password = NULL;
	r.in.hash = &lm_verifier;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);

	if (!NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
		printf("OemChangePasswordUser2 failed, should have returned INVALID_PARAMETER for no supplied password and invalid user - %s\n",
			nt_errstr(status));
		ret = false;
	}

	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	encode_pw_buffer(lm_pass.data, newpass, STR_ASCII);
	arcfour_crypt(lm_pass.data, old_lm_hash, 516);
	E_old_pw_hash(new_lm_hash, old_lm_hash, lm_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.password = &lm_pass;
	r.in.hash = &lm_verifier;

	status = dcerpc_samr_OemChangePasswordUser2(p, tctx, &r);
	if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		printf("OemChangePasswordUser2 returned: %s perhaps min password age? (not fatal)\n", nt_errstr(status));
	} else if (!NT_STATUS_IS_OK(status)) {
		printf("OemChangePasswordUser2 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}


static bool test_ChangePasswordUser2(struct dcerpc_pipe *p, struct torture_context *tctx,
				     const char *acct_name,
				     char **password,
				     char *newpass, bool allow_password_restriction)
{
	NTSTATUS status;
	struct samr_ChangePasswordUser2 r;
	bool ret = true;
	struct lsa_String server, account;
	struct samr_CryptPassword nt_pass, lm_pass;
	struct samr_Password nt_verifier, lm_verifier;
	char *oldpass;
	uint8_t old_nt_hash[16], new_nt_hash[16];
	uint8_t old_lm_hash[16], new_lm_hash[16];

	struct samr_GetDomPwInfo dom_pw_info;
	struct samr_PwInfo info;

	struct lsa_String domain_name;

	domain_name.string = "";
	dom_pw_info.in.domain_name = &domain_name;
	dom_pw_info.out.info = &info;

	torture_comment(tctx, "Testing ChangePasswordUser2 on %s\n", acct_name);

	torture_assert(tctx, *password != NULL, 
				   "Failing ChangePasswordUser2 as old password was NULL.  Previous test failed?");
	oldpass = *password;

	if (!newpass) {
		int policy_min_pw_len = 0;
		status = dcerpc_samr_GetDomPwInfo(p, tctx, &dom_pw_info);
		if (NT_STATUS_IS_OK(status)) {
			policy_min_pw_len = dom_pw_info.out.info->min_password_length;
		}

		newpass = samr_rand_pass(tctx, policy_min_pw_len);
	} 

	server.string = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	init_lsa_String(&account, acct_name);

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);

	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	encode_pw_buffer(lm_pass.data, newpass, STR_ASCII|STR_TERMINATE);
	arcfour_crypt(lm_pass.data, old_lm_hash, 516);
	E_old_pw_hash(new_nt_hash, old_lm_hash, lm_verifier.hash);

	encode_pw_buffer(nt_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 1;
	r.in.lm_password = &lm_pass;
	r.in.lm_verifier = &lm_verifier;

	status = dcerpc_samr_ChangePasswordUser2(p, tctx, &r);
	if (allow_password_restriction && NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		printf("ChangePasswordUser2 returned: %s perhaps min password age? (not fatal)\n", nt_errstr(status));
	} else if (!NT_STATUS_IS_OK(status)) {
		printf("ChangePasswordUser2 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		*password = newpass;
	}

	return ret;
}


bool test_ChangePasswordUser3(struct dcerpc_pipe *p, struct torture_context *tctx, 
			      const char *account_string,
			      int policy_min_pw_len,
			      char **password,
			      const char *newpass,
			      NTTIME last_password_change,
			      bool handle_reject_reason)
{
	NTSTATUS status;
	struct samr_ChangePasswordUser3 r;
	bool ret = true;
	struct lsa_String server, account, account_bad;
	struct samr_CryptPassword nt_pass, lm_pass;
	struct samr_Password nt_verifier, lm_verifier;
	char *oldpass;
	uint8_t old_nt_hash[16], new_nt_hash[16];
	uint8_t old_lm_hash[16], new_lm_hash[16];
	NTTIME t;
	struct samr_DomInfo1 *dominfo = NULL;
	struct samr_ChangeReject *reject = NULL;

	torture_comment(tctx, "Testing ChangePasswordUser3\n");

	if (newpass == NULL) {
		do {
			if (policy_min_pw_len == 0) {
				newpass = samr_rand_pass(tctx, policy_min_pw_len);
			} else {
				newpass = samr_rand_pass_fixed_len(tctx, policy_min_pw_len);
			}
		} while (check_password_quality(newpass) == false);
	} else {
		torture_comment(tctx, "Using password '%s'\n", newpass);
	}

	torture_assert(tctx, *password != NULL, 
				   "Failing ChangePasswordUser3 as old password was NULL.  Previous test failed?");

	oldpass = *password;
	server.string = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	init_lsa_String(&account, account_string);

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);

	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	encode_pw_buffer(lm_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(lm_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_lm_hash, lm_verifier.hash);

	encode_pw_buffer(nt_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);
	
	/* Break the verification */
	nt_verifier.hash[0]++;

	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 1;
	r.in.lm_password = &lm_pass;
	r.in.lm_verifier = &lm_verifier;
	r.in.password3 = NULL;
	r.out.dominfo = &dominfo;
	r.out.reject = &reject;

	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION) &&
	    (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD))) {
		printf("ChangePasswordUser3 failed, should have returned WRONG_PASSWORD (or at least 'PASSWORD_RESTRICTON') for invalid password verifier - %s\n",
			nt_errstr(status));
		ret = false;
	}
	
	encode_pw_buffer(lm_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(lm_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_lm_hash, lm_verifier.hash);

	encode_pw_buffer(nt_pass.data, newpass, STR_UNICODE);
	/* Break the NT hash */
	old_nt_hash[0]++;
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	/* Unbreak it again */
	old_nt_hash[0]--;
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);
	
	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 1;
	r.in.lm_password = &lm_pass;
	r.in.lm_verifier = &lm_verifier;
	r.in.password3 = NULL;
	r.out.dominfo = &dominfo;
	r.out.reject = &reject;

	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION) &&
	    (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD))) {
		printf("ChangePasswordUser3 failed, should have returned WRONG_PASSWORD (or at least 'PASSWORD_RESTRICTON') for invalidly encrpted password - %s\n",
			nt_errstr(status));
		ret = false;
	}
	
	/* This shouldn't be a valid name */
	init_lsa_String(&account_bad, talloc_asprintf(tctx, "%sXX", account_string));

	r.in.account = &account_bad;
	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);
	if (!NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		printf("ChangePasswordUser3 failed, should have returned WRONG_PASSWORD for invalid username - %s\n",
			nt_errstr(status));
		ret = false;
	}

	E_md4hash(oldpass, old_nt_hash);
	E_md4hash(newpass, new_nt_hash);

	E_deshash(oldpass, old_lm_hash);
	E_deshash(newpass, new_lm_hash);

	encode_pw_buffer(lm_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(lm_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_lm_hash, lm_verifier.hash);

	encode_pw_buffer(nt_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 1;
	r.in.lm_password = &lm_pass;
	r.in.lm_verifier = &lm_verifier;
	r.in.password3 = NULL;
	r.out.dominfo = &dominfo;
	r.out.reject = &reject;

	unix_to_nt_time(&t, time(NULL));

	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);

	if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)
	    && dominfo
	    && reject
	    && handle_reject_reason
	    && (!null_nttime(last_password_change) || !dominfo->min_password_age)) {
		if (dominfo->password_properties & DOMAIN_REFUSE_PASSWORD_CHANGE ) {

			if (reject && (reject->reason != SAMR_REJECT_OTHER)) {
				printf("expected SAMR_REJECT_OTHER (%d), got %d\n", 
					SAMR_REJECT_OTHER, reject->reason);
				return false;
			}
		}

		/* We tested the order of precendence which is as follows:
		
		* pwd min_age 
		* pwd length
		* pwd complexity
		* pwd history

		Guenther */

		if ((dominfo->min_password_age > 0) && !null_nttime(last_password_change) &&
			   (last_password_change + dominfo->min_password_age > t)) {

			if (reject->reason != SAMR_REJECT_OTHER) {
				printf("expected SAMR_REJECT_OTHER (%d), got %d\n", 
					SAMR_REJECT_OTHER, reject->reason);
				return false;
			}

		} else if ((dominfo->min_password_length > 0) &&
			   (strlen(newpass) < dominfo->min_password_length)) {

			if (reject->reason != SAMR_REJECT_TOO_SHORT) {
				printf("expected SAMR_REJECT_TOO_SHORT (%d), got %d\n", 
					SAMR_REJECT_TOO_SHORT, reject->reason);
				return false;
			}

		} else if ((dominfo->password_history_length > 0) &&
			    strequal(oldpass, newpass)) {

			if (reject->reason != SAMR_REJECT_IN_HISTORY) {
				printf("expected SAMR_REJECT_IN_HISTORY (%d), got %d\n", 
					SAMR_REJECT_IN_HISTORY, reject->reason);
				return false;
			}
		} else if (dominfo->password_properties & DOMAIN_PASSWORD_COMPLEX) {

			if (reject->reason != SAMR_REJECT_COMPLEXITY) {
				printf("expected SAMR_REJECT_COMPLEXITY (%d), got %d\n", 
					SAMR_REJECT_COMPLEXITY, reject->reason);
				return false;
			}

		}

		if (reject->reason == SAMR_REJECT_TOO_SHORT) {
			/* retry with adjusted size */
			return test_ChangePasswordUser3(p, tctx, account_string, 
							dominfo->min_password_length,
							password, NULL, 0, false); 

		}

	} else if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		if (reject && reject->reason != SAMR_REJECT_OTHER) {
			printf("expected SAMR_REJECT_OTHER (%d), got %d\n", 
			       SAMR_REJECT_OTHER, reject->reason);
			return false;
		}
		/* Perhaps the server has a 'min password age' set? */

	} else { 
		torture_assert_ntstatus_ok(tctx, status, "ChangePasswordUser3");
		*password = talloc_strdup(tctx, newpass);
	}

	return ret;
}

bool test_ChangePasswordRandomBytes(struct dcerpc_pipe *p, struct torture_context *tctx,
				    const char *account_string,
				    struct policy_handle *handle, 
				    char **password)
{
	NTSTATUS status;
	struct samr_ChangePasswordUser3 r;
	struct samr_SetUserInfo s;
	union samr_UserInfo u;
	DATA_BLOB session_key;
	DATA_BLOB confounded_session_key = data_blob_talloc(tctx, NULL, 16);
	uint8_t confounder[16];
	struct MD5Context ctx;

	bool ret = true;
	struct lsa_String server, account;
	struct samr_CryptPassword nt_pass;
	struct samr_Password nt_verifier;
	DATA_BLOB new_random_pass;
	char *newpass;
	char *oldpass;
	uint8_t old_nt_hash[16], new_nt_hash[16];
	NTTIME t;
	struct samr_DomInfo1 *dominfo = NULL;
	struct samr_ChangeReject *reject = NULL;

	new_random_pass = samr_very_rand_pass(tctx, 128);

	torture_assert(tctx, *password != NULL, 
				   "Failing ChangePasswordUser3 as old password was NULL.  Previous test failed?");

	oldpass = *password;
	server.string = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	init_lsa_String(&account, account_string);

	s.in.user_handle = handle;
	s.in.info = &u;
	s.in.level = 25;

	ZERO_STRUCT(u);

	u.info25.info.fields_present = SAMR_FIELD_NT_PASSWORD_PRESENT;

	set_pw_in_buffer(u.info25.password.data, &new_random_pass);

	status = dcerpc_fetch_session_key(p, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u - no session key - %s\n",
		       s.in.level, nt_errstr(status));
		return false;
	}

	generate_random_buffer((uint8_t *)confounder, 16);

	MD5Init(&ctx);
	MD5Update(&ctx, confounder, 16);
	MD5Update(&ctx, session_key.data, session_key.length);
	MD5Final(confounded_session_key.data, &ctx);

	arcfour_crypt_blob(u.info25.password.data, 516, &confounded_session_key);
	memcpy(&u.info25.password.data[516], confounder, 16);

	torture_comment(tctx, "Testing SetUserInfo level 25 (set password ex) with a password made up of only random bytes\n");

	status = dcerpc_samr_SetUserInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetUserInfo level %u failed - %s\n",
		       s.in.level, nt_errstr(status));
		ret = false;
	}

	torture_comment(tctx, "Testing ChangePasswordUser3 with a password made up of only random bytes\n");

	mdfour(old_nt_hash, new_random_pass.data, new_random_pass.length);

	new_random_pass = samr_very_rand_pass(tctx, 128);

	mdfour(new_nt_hash, new_random_pass.data, new_random_pass.length);

	set_pw_in_buffer(nt_pass.data, &new_random_pass);
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 0;
	r.in.lm_password = NULL;
	r.in.lm_verifier = NULL;
	r.in.password3 = NULL;
	r.out.dominfo = &dominfo;
	r.out.reject = &reject;

	unix_to_nt_time(&t, time(NULL));

	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);

	if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		if (reject && reject->reason != SAMR_REJECT_OTHER) {
			printf("expected SAMR_REJECT_OTHER (%d), got %d\n", 
			       SAMR_REJECT_OTHER, reject->reason);
			return false;
		}
		/* Perhaps the server has a 'min password age' set? */

	} else if (!NT_STATUS_IS_OK(status)) {
		printf("ChangePasswordUser3 failed - %s\n", nt_errstr(status));
		ret = false;
	}
	
	newpass = samr_rand_pass(tctx, 128);

	mdfour(old_nt_hash, new_random_pass.data, new_random_pass.length);

	E_md4hash(newpass, new_nt_hash);

	encode_pw_buffer(nt_pass.data, newpass, STR_UNICODE);
	arcfour_crypt(nt_pass.data, old_nt_hash, 516);
	E_old_pw_hash(new_nt_hash, old_nt_hash, nt_verifier.hash);

	r.in.server = &server;
	r.in.account = &account;
	r.in.nt_password = &nt_pass;
	r.in.nt_verifier = &nt_verifier;
	r.in.lm_change = 0;
	r.in.lm_password = NULL;
	r.in.lm_verifier = NULL;
	r.in.password3 = NULL;
	r.out.dominfo = &dominfo;
	r.out.reject = &reject;

	unix_to_nt_time(&t, time(NULL));

	status = dcerpc_samr_ChangePasswordUser3(p, tctx, &r);

	if (NT_STATUS_EQUAL(status, NT_STATUS_PASSWORD_RESTRICTION)) {
		if (reject && reject->reason != SAMR_REJECT_OTHER) {
			printf("expected SAMR_REJECT_OTHER (%d), got %d\n", 
			       SAMR_REJECT_OTHER, reject->reason);
			return false;
		}
		/* Perhaps the server has a 'min password age' set? */

	} else {
		torture_assert_ntstatus_ok(tctx, status, "ChangePasswordUser3 (on second random password)");
		*password = talloc_strdup(tctx, newpass);
	}

	return ret;
}


static bool test_GetMembersInAlias(struct dcerpc_pipe *p, struct torture_context *tctx,
				  struct policy_handle *alias_handle)
{
	struct samr_GetMembersInAlias r;
	struct lsa_SidArray sids;
	NTSTATUS status;

	torture_comment(tctx, "Testing GetMembersInAlias\n");

	r.in.alias_handle = alias_handle;
	r.out.sids = &sids;

	status = dcerpc_samr_GetMembersInAlias(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "GetMembersInAlias");

	return true;
}

static bool test_AddMemberToAlias(struct dcerpc_pipe *p, struct torture_context *tctx,
				  struct policy_handle *alias_handle,
				  const struct dom_sid *domain_sid)
{
	struct samr_AddAliasMember r;
	struct samr_DeleteAliasMember d;
	NTSTATUS status;
	struct dom_sid *sid;

	sid = dom_sid_add_rid(tctx, domain_sid, 512);

	torture_comment(tctx, "testing AddAliasMember\n");
	r.in.alias_handle = alias_handle;
	r.in.sid = sid;

	status = dcerpc_samr_AddAliasMember(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "AddAliasMember");

	d.in.alias_handle = alias_handle;
	d.in.sid = sid;

	status = dcerpc_samr_DeleteAliasMember(p, tctx, &d);
	torture_assert_ntstatus_ok(tctx, status, "DelAliasMember");

	return true;
}

static bool test_AddMultipleMembersToAlias(struct dcerpc_pipe *p, struct torture_context *tctx,
					   struct policy_handle *alias_handle)
{
	struct samr_AddMultipleMembersToAlias a;
	struct samr_RemoveMultipleMembersFromAlias r;
	NTSTATUS status;
	struct lsa_SidArray sids;

	torture_comment(tctx, "testing AddMultipleMembersToAlias\n");
	a.in.alias_handle = alias_handle;
	a.in.sids = &sids;

	sids.num_sids = 3;
	sids.sids = talloc_array(tctx, struct lsa_SidPtr, 3);

	sids.sids[0].sid = dom_sid_parse_talloc(tctx, "S-1-5-32-1-2-3-1");
	sids.sids[1].sid = dom_sid_parse_talloc(tctx, "S-1-5-32-1-2-3-2");
	sids.sids[2].sid = dom_sid_parse_talloc(tctx, "S-1-5-32-1-2-3-3");

	status = dcerpc_samr_AddMultipleMembersToAlias(p, tctx, &a);
	torture_assert_ntstatus_ok(tctx, status, "AddMultipleMembersToAlias");


	torture_comment(tctx, "testing RemoveMultipleMembersFromAlias\n");
	r.in.alias_handle = alias_handle;
	r.in.sids = &sids;

	status = dcerpc_samr_RemoveMultipleMembersFromAlias(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "RemoveMultipleMembersFromAlias");

	/* strange! removing twice doesn't give any error */
	status = dcerpc_samr_RemoveMultipleMembersFromAlias(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "RemoveMultipleMembersFromAlias");

	/* but removing an alias that isn't there does */
	sids.sids[2].sid = dom_sid_parse_talloc(tctx, "S-1-5-32-1-2-3-4");

	status = dcerpc_samr_RemoveMultipleMembersFromAlias(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, status, NT_STATUS_OBJECT_NAME_NOT_FOUND, "RemoveMultipleMembersFromAlias");

	return true;
}

static bool test_TestPrivateFunctionsUser(struct dcerpc_pipe *p, struct torture_context *tctx,
					    struct policy_handle *user_handle)
{
    	struct samr_TestPrivateFunctionsUser r;
	NTSTATUS status;

	torture_comment(tctx, "Testing TestPrivateFunctionsUser\n");

	r.in.user_handle = user_handle;

	status = dcerpc_samr_TestPrivateFunctionsUser(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, status, NT_STATUS_NOT_IMPLEMENTED, "TestPrivateFunctionsUser");

	return true;
}

static bool test_QueryUserInfo_pwdlastset(struct dcerpc_pipe *p,
					  struct torture_context *tctx,
					  struct policy_handle *handle,
					  bool use_info2,
					  NTTIME *pwdlastset)
{
	NTSTATUS status;
	uint16_t levels[] = { /* 3, */ 5, 21 };
	int i;
	NTTIME pwdlastset3 = 0;
	NTTIME pwdlastset5 = 0;
	NTTIME pwdlastset21 = 0;

	torture_comment(tctx, "Testing QueryUserInfo%s level 5 and 21 call ",
			use_info2 ? "2":"");

	for (i=0; i<ARRAY_SIZE(levels); i++) {

		struct samr_QueryUserInfo r;
		struct samr_QueryUserInfo2 r2;
		union samr_UserInfo *info;

		if (use_info2) {
			r2.in.user_handle = handle;
			r2.in.level = levels[i];
			r2.out.info = &info;
			status = dcerpc_samr_QueryUserInfo2(p, tctx, &r2);

		} else {
			r.in.user_handle = handle;
			r.in.level = levels[i];
			r.out.info = &info;
			status = dcerpc_samr_QueryUserInfo(p, tctx, &r);
		}

		if (!NT_STATUS_IS_OK(status) &&
		    !NT_STATUS_EQUAL(status, NT_STATUS_INVALID_INFO_CLASS)) {
			printf("QueryUserInfo%s level %u failed - %s\n",
			       use_info2 ? "2":"", levels[i], nt_errstr(status));
			return false;
		}

		switch (levels[i]) {
		case 3:
			pwdlastset3 = info->info3.last_password_change;
			break;
		case 5:
			pwdlastset5 = info->info5.last_password_change;
			break;
		case 21:
			pwdlastset21 = info->info21.last_password_change;
			break;
		default:
			return false;
		}
	}
	/* torture_assert_int_equal(tctx, pwdlastset3, pwdlastset5,
				    "pwdlastset mixup"); */
	torture_assert_int_equal(tctx, pwdlastset5, pwdlastset21,
				 "pwdlastset mixup");

	*pwdlastset = pwdlastset21;

	torture_comment(tctx, "(pwdlastset: %lld)\n", *pwdlastset);

	return true;
}

static bool test_SamLogon_Creds(struct dcerpc_pipe *p, struct torture_context *tctx,
				struct cli_credentials *machine_credentials,
				struct cli_credentials *test_credentials,
				struct netlogon_creds_CredentialState *creds,
				NTSTATUS expected_result)
{
	NTSTATUS status;
	struct netr_LogonSamLogon r;
	struct netr_Authenticator auth, auth2;
	union netr_LogonLevel logon;
	union netr_Validation validation;
	uint8_t authoritative;
	struct netr_NetworkInfo ninfo;
	DATA_BLOB names_blob, chal, lm_resp, nt_resp;
	int flags = CLI_CRED_NTLM_AUTH;

	if (lp_client_lanman_auth(tctx->lp_ctx)) {
		flags |= CLI_CRED_LANMAN_AUTH;
	}

	if (lp_client_ntlmv2_auth(tctx->lp_ctx)) {
		flags |= CLI_CRED_NTLMv2_AUTH;
	}

	cli_credentials_get_ntlm_username_domain(test_credentials, tctx,
						 &ninfo.identity_info.account_name.string,
						 &ninfo.identity_info.domain_name.string);

	generate_random_buffer(ninfo.challenge,
			       sizeof(ninfo.challenge));
	chal = data_blob_const(ninfo.challenge,
			       sizeof(ninfo.challenge));

	names_blob = NTLMv2_generate_names_blob(tctx, cli_credentials_get_workstation(machine_credentials),
						cli_credentials_get_domain(machine_credentials));

	status = cli_credentials_get_ntlm_response(test_credentials, tctx,
						   &flags,
						   chal,
						   names_blob,
						   &lm_resp, &nt_resp,
						   NULL, NULL);
	torture_assert_ntstatus_ok(tctx, status, "cli_credentials_get_ntlm_response failed");

	ninfo.lm.data = lm_resp.data;
	ninfo.lm.length = lm_resp.length;

	ninfo.nt.data = nt_resp.data;
	ninfo.nt.length = nt_resp.length;

	ninfo.identity_info.parameter_control =
		MSV1_0_ALLOW_SERVER_TRUST_ACCOUNT |
		MSV1_0_ALLOW_WORKSTATION_TRUST_ACCOUNT;
	ninfo.identity_info.logon_id_low = 0;
	ninfo.identity_info.logon_id_high = 0;
	ninfo.identity_info.workstation.string = cli_credentials_get_workstation(machine_credentials);

	logon.network = &ninfo;

	r.in.server_name = talloc_asprintf(tctx, "\\\\%s", dcerpc_server_name(p));
	r.in.computer_name = cli_credentials_get_workstation(machine_credentials);
	r.in.credential = &auth;
	r.in.return_authenticator = &auth2;
	r.in.logon_level = 2;
	r.in.logon = &logon;
	r.out.validation = &validation;
	r.out.authoritative = &authoritative;

	d_printf("Testing LogonSamLogon with name %s\n", ninfo.identity_info.account_name.string);

	ZERO_STRUCT(auth2);
	netlogon_creds_client_authenticator(creds, &auth);

	r.in.validation_level = 2;

	status = dcerpc_netr_LogonSamLogon(p, tctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		torture_assert_ntstatus_equal(tctx, status, expected_result, "LogonSamLogon failed");
		return true;
	} else {
		torture_assert_ntstatus_ok(tctx, status, "LogonSamLogon failed");
	}

	torture_assert(tctx, netlogon_creds_client_check(creds, &r.out.return_authenticator->cred),
			"Credential chaining failed");

	return true;
}

static bool test_SamLogon(struct torture_context *tctx,
			  struct dcerpc_pipe *p,
			  struct cli_credentials *machine_credentials,
			  struct cli_credentials *test_credentials,
			  NTSTATUS expected_result)
{
	struct netlogon_creds_CredentialState *creds;

	if (!test_SetupCredentials(p, tctx, machine_credentials, &creds)) {
		return false;
	}

	return test_SamLogon_Creds(p, tctx, machine_credentials, test_credentials,
				   creds, expected_result);
}

static bool test_SamLogon_with_creds(struct torture_context *tctx,
				     struct dcerpc_pipe *p,
				     struct cli_credentials *machine_creds,
				     const char *acct_name,
				     char *password,
				     NTSTATUS expected_samlogon_result)
{
	bool ret = true;
	struct cli_credentials *test_credentials;

	test_credentials = cli_credentials_init(tctx);

	cli_credentials_set_workstation(test_credentials,
					TEST_ACCOUNT_NAME_PWD, CRED_SPECIFIED);
	cli_credentials_set_domain(test_credentials,
				   lp_workgroup(tctx->lp_ctx), CRED_SPECIFIED);
	cli_credentials_set_username(test_credentials,
				     acct_name, CRED_SPECIFIED);
	cli_credentials_set_password(test_credentials,
				     password, CRED_SPECIFIED);
	cli_credentials_set_secure_channel_type(test_credentials, SEC_CHAN_BDC);

	printf("testing samlogon as %s@%s password: %s\n",
		acct_name, TEST_ACCOUNT_NAME_PWD, password);

	if (!test_SamLogon(tctx, p, machine_creds, test_credentials,
			   expected_samlogon_result)) {
		torture_warning(tctx, "new password did not work\n");
		ret = false;
	}

	return ret;
}

static bool test_SetPassword_level(struct dcerpc_pipe *p,
				   struct dcerpc_pipe *np,
				   struct torture_context *tctx,
				   struct policy_handle *handle,
				   uint16_t level,
				   uint32_t fields_present,
				   uint8_t password_expired,
				   bool *matched_expected_error,
				   bool use_setinfo2,
				   const char *acct_name,
				   char **password,
				   struct cli_credentials *machine_creds,
				   bool use_queryinfo2,
				   NTTIME *pwdlastset,
				   NTSTATUS expected_samlogon_result)
{
	const char *fields = NULL;
	bool ret = true;

	switch (level) {
	case 21:
	case 23:
	case 25:
		fields = talloc_asprintf(tctx, "(fields_present: 0x%08x)",
					 fields_present);
		break;
	default:
		break;
	}

	torture_comment(tctx, "Testing SetUserInfo%s level %d call "
		"(password_expired: %d) %s\n",
		use_setinfo2 ? "2":"", level, password_expired,
		fields ? fields : "");

	if (!test_SetUserPass_level_ex(p, tctx, handle, level,
				       fields_present,
				       password,
				       password_expired,
				       use_setinfo2,
				       matched_expected_error)) {
		ret = false;
	}

	if (!test_QueryUserInfo_pwdlastset(p, tctx, handle,
					   use_queryinfo2,
					   pwdlastset)) {
		ret = false;
	}

	if (*matched_expected_error == true) {
		return ret;
	}

	if (!test_SamLogon_with_creds(tctx, np,
				      machine_creds,
				      acct_name,
				      *password,
				      expected_samlogon_result)) {
		ret = false;
	}

	return ret;
}

static bool test_SetPassword_pwdlastset(struct dcerpc_pipe *p,
					struct torture_context *tctx,
					uint32_t acct_flags,
					const char *acct_name,
					struct policy_handle *handle,
					char **password,
					struct cli_credentials *machine_credentials)
{
	int s = 0, q = 0, f = 0, l = 0, z = 0;
	bool ret = true;
	int delay = 500000;
	bool set_levels[] = { false, true };
	bool query_levels[] = { false, true };
	uint32_t levels[] = { 18, 21, 23, 24, 25, 26 };
	uint32_t nonzeros[] = { 1, 24 };
	uint32_t fields_present[] = {
		0,
		SAMR_FIELD_EXPIRED_FLAG,
		SAMR_FIELD_LAST_PWD_CHANGE,
		SAMR_FIELD_EXPIRED_FLAG | SAMR_FIELD_LAST_PWD_CHANGE,
		SAMR_FIELD_COMMENT,
		SAMR_FIELD_NT_PASSWORD_PRESENT,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LAST_PWD_CHANGE,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LM_PASSWORD_PRESENT,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LM_PASSWORD_PRESENT | SAMR_FIELD_LAST_PWD_CHANGE,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_EXPIRED_FLAG,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LM_PASSWORD_PRESENT | SAMR_FIELD_EXPIRED_FLAG,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LM_PASSWORD_PRESENT | SAMR_FIELD_LAST_PWD_CHANGE | SAMR_FIELD_EXPIRED_FLAG
	};
	NTSTATUS status;
	struct dcerpc_pipe *np = NULL;

	if (torture_setting_bool(tctx, "samba3", false)) {
		delay = 1000000;
		printf("Samba3 has second granularity, setting delay to: %d\n",
			delay);
	}

	status = torture_rpc_connection(tctx, &np, &ndr_table_netlogon);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	/* set to 1 to enable testing for all possible opcode
	   (SetUserInfo, SetUserInfo2, QueryUserInfo, QueryUserInfo2)
	   combinations */
#if 0
#define TEST_SET_LEVELS 1
#define TEST_QUERY_LEVELS 1
#endif
	for (l=0; l<ARRAY_SIZE(levels); l++) {
	for (z=0; z<ARRAY_SIZE(nonzeros); z++) {
	for (f=0; f<ARRAY_SIZE(fields_present); f++) {
#ifdef TEST_SET_LEVELS
	for (s=0; s<ARRAY_SIZE(set_levels); s++) {
#endif
#ifdef TEST_QUERY_LEVELS
	for (q=0; q<ARRAY_SIZE(query_levels); q++) {
#endif
		NTTIME pwdlastset_old = 0;
		NTTIME pwdlastset_new = 0;
		bool matched_expected_error = false;
		NTSTATUS expected_samlogon_result = NT_STATUS_ACCOUNT_DISABLED;

		torture_comment(tctx, "------------------------------\n"
				"Testing pwdLastSet attribute for flags: 0x%08x "
				"(s: %d (l: %d), q: %d)\n",
				acct_flags, s, levels[l], q);

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if (!((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			      (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT))) {
				expected_samlogon_result = NT_STATUS_WRONG_PASSWORD;
			}
			break;
		}


		/* set #1 */

		/* set a password and force password change (pwdlastset 0) by
		 * setting the password expired flag to a non-0 value */

		if (!test_SetPassword_level(p, np, tctx, handle,
					    levels[l],
					    fields_present[f],
					    nonzeros[z],
					    &matched_expected_error,
					    set_levels[s],
					    acct_name,
					    password,
					    machine_credentials,
					    query_levels[q],
					    &pwdlastset_old,
					    expected_samlogon_result)) {
			ret = false;
		}

		if (matched_expected_error == true) {
			/* skipping on expected failure */
			continue;
		}

		/* pwdlastset must be 0 afterwards, except for a level 21, 23 and 25
		 * set without the SAMR_FIELD_EXPIRED_FLAG */

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if ((pwdlastset_new != 0) &&
			    !(fields_present[f] & SAMR_FIELD_EXPIRED_FLAG)) {
				torture_comment(tctx, "not considering a non-0 "
					"pwdLastSet as a an error as the "
					"SAMR_FIELD_EXPIRED_FLAG has not "
					"been set\n");
				break;
			}
		default:
			if (pwdlastset_new != 0) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected pwdLastSet 0 but got %lld\n",
					pwdlastset_old);
				ret = false;
			}
			break;
		}

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if (((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			     (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)) &&
			     (pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			     (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		default:
			if ((pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			    (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		}

		usleep(delay);

		/* set #2 */

		/* set a password, pwdlastset needs to get updated (increased
		 * value), password_expired value used here is 0 */

		if (!test_SetPassword_level(p, np, tctx, handle,
					    levels[l],
					    fields_present[f],
					    0,
					    &matched_expected_error,
					    set_levels[s],
					    acct_name,
					    password,
					    machine_credentials,
					    query_levels[q],
					    &pwdlastset_new,
					    expected_samlogon_result)) {
			ret = false;
		}

		/* when a password has been changed, pwdlastset must not be 0 afterwards
		 * and must be larger then the old value */

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:

			/* SAMR_FIELD_EXPIRED_FLAG has not been set and no
			 * password has been changed, old and new pwdlastset
			 * need to be the same value */

			if (!(fields_present[f] & SAMR_FIELD_EXPIRED_FLAG) &&
			    !((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			      (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)))
			{
				torture_assert_int_equal(tctx, pwdlastset_old,
					pwdlastset_new, "pwdlastset must be equal");
				break;
			}
		default:
			if (pwdlastset_old >= pwdlastset_new) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected last pwdlastset (%lld) < new pwdlastset (%lld)\n",
					pwdlastset_old, pwdlastset_new);
				ret = false;
			}
			if (pwdlastset_new == 0) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected non-0 pwdlastset, got: %lld\n",
					pwdlastset_new);
				ret = false;
			}
		}

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if (((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			     (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)) &&
			     (pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			     (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		default:
			if ((pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			    (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		}

		pwdlastset_old = pwdlastset_new;

		usleep(delay);

		/* set #2b */

		/* set a password, pwdlastset needs to get updated (increased
		 * value), password_expired value used here is 0 */

		if (!test_SetPassword_level(p, np, tctx, handle,
					    levels[l],
					    fields_present[f],
					    0,
					    &matched_expected_error,
					    set_levels[s],
					    acct_name,
					    password,
					    machine_credentials,
					    query_levels[q],
					    &pwdlastset_new,
					    expected_samlogon_result)) {
			ret = false;
		}

		/* when a password has been changed, pwdlastset must not be 0 afterwards
		 * and must be larger then the old value */

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:

			/* if no password has been changed, old and new pwdlastset
			 * need to be the same value */

			if (!((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			      (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)))
			{
				torture_assert_int_equal(tctx, pwdlastset_old,
					pwdlastset_new, "pwdlastset must be equal");
				break;
			}
		default:
			if (pwdlastset_old >= pwdlastset_new) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected last pwdlastset (%lld) < new pwdlastset (%lld)\n",
					pwdlastset_old, pwdlastset_new);
				ret = false;
			}
			if (pwdlastset_new == 0) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected non-0 pwdlastset, got: %lld\n",
					pwdlastset_new);
				ret = false;
			}
		}

		/* set #3 */

		/* set a password and force password change (pwdlastset 0) by
		 * setting the password expired flag to a non-0 value */

		if (!test_SetPassword_level(p, np, tctx, handle,
					    levels[l],
					    fields_present[f],
					    nonzeros[z],
					    &matched_expected_error,
					    set_levels[s],
					    acct_name,
					    password,
					    machine_credentials,
					    query_levels[q],
					    &pwdlastset_new,
					    expected_samlogon_result)) {
			ret = false;
		}

		/* pwdlastset must be 0 afterwards, except for a level 21, 23 and 25
		 * set without the SAMR_FIELD_EXPIRED_FLAG */

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if ((pwdlastset_new != 0) &&
			    !(fields_present[f] & SAMR_FIELD_EXPIRED_FLAG)) {
				torture_comment(tctx, "not considering a non-0 "
					"pwdLastSet as a an error as the "
					"SAMR_FIELD_EXPIRED_FLAG has not "
					"been set\n");
				break;
			}

			/* SAMR_FIELD_EXPIRED_FLAG has not been set and no
			 * password has been changed, old and new pwdlastset
			 * need to be the same value */

			if (!(fields_present[f] & SAMR_FIELD_EXPIRED_FLAG) &&
			    !((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			      (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)))
			{
				torture_assert_int_equal(tctx, pwdlastset_old,
					pwdlastset_new, "pwdlastset must be equal");
				break;
			}
		default:

			if (pwdlastset_old == pwdlastset_new) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected last pwdlastset (%lld) != new pwdlastset (%lld)\n",
					pwdlastset_old, pwdlastset_new);
				ret = false;
			}

			if (pwdlastset_new != 0) {
				torture_warning(tctx, "pwdLastSet test failed: "
					"expected pwdLastSet 0, got %lld\n",
					pwdlastset_old);
				ret = false;
			}
			break;
		}

		switch (levels[l]) {
		case 21:
		case 23:
		case 25:
			if (((fields_present[f] & SAMR_FIELD_NT_PASSWORD_PRESENT) ||
			     (fields_present[f] & SAMR_FIELD_LM_PASSWORD_PRESENT)) &&
			     (pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			     (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		default:
			if ((pwdlastset_old > 0) && (pwdlastset_new > 0) &&
			    (pwdlastset_old >= pwdlastset_new)) {
				torture_warning(tctx, "pwdlastset not increasing\n");
				ret = false;
			}
			break;
		}

		/* if the level we are testing does not have a fields_present
		 * field, skip all fields present tests by setting f to to
		 * arraysize */
		switch (levels[l]) {
		case 18:
		case 24:
		case 26:
			f = ARRAY_SIZE(fields_present);
			break;
		}

#ifdef TEST_QUERY_LEVELS
	}
#endif
#ifdef TEST_SET_LEVELS
	}
#endif
	} /* fields present */
	} /* nonzeros */
	} /* levels */

#undef TEST_SET_LEVELS
#undef TEST_QUERY_LEVELS

	return ret;
}

static bool test_user_ops(struct dcerpc_pipe *p, 
			  struct torture_context *tctx,
			  struct policy_handle *user_handle, 
			  struct policy_handle *domain_handle, 
			  uint32_t base_acct_flags, 
			  const char *base_acct_name, enum torture_samr_choice which_ops,
			  struct cli_credentials *machine_credentials)
{
	char *password = NULL;
	struct samr_QueryUserInfo q;
	union samr_UserInfo *info;
	NTSTATUS status;

	bool ret = true;
	int i;
	uint32_t rid;
	const uint32_t password_fields[] = {
		SAMR_FIELD_NT_PASSWORD_PRESENT,
		SAMR_FIELD_LM_PASSWORD_PRESENT,
		SAMR_FIELD_NT_PASSWORD_PRESENT | SAMR_FIELD_LM_PASSWORD_PRESENT,
		0
	};
	
	status = test_LookupName(p, tctx, domain_handle, base_acct_name, &rid);
	if (!NT_STATUS_IS_OK(status)) {
		ret = false;
	}

	switch (which_ops) {
	case TORTURE_SAMR_USER_ATTRIBUTES:
		if (!test_QuerySecurity(p, tctx, user_handle)) {
			ret = false;
		}

		if (!test_QueryUserInfo(p, tctx, user_handle)) {
			ret = false;
		}

		if (!test_QueryUserInfo2(p, tctx, user_handle)) {
			ret = false;
		}

		if (!test_SetUserInfo(p, tctx, user_handle, base_acct_flags,
				      base_acct_name)) {
			ret = false;
		}	

		if (!test_GetUserPwInfo(p, tctx, user_handle)) {
			ret = false;
		}

		if (!test_TestPrivateFunctionsUser(p, tctx, user_handle)) {
			ret = false;
		}

		if (!test_SetUserPass(p, tctx, user_handle, &password)) {
			ret = false;
		}
		break;
	case TORTURE_SAMR_PASSWORDS:
		if (base_acct_flags & (ACB_WSTRUST|ACB_DOMTRUST|ACB_SVRTRUST)) {
			char simple_pass[9];
			char *v = generate_random_str(tctx, 1);
			
			ZERO_STRUCT(simple_pass);
			memset(simple_pass, *v, sizeof(simple_pass) - 1);

			printf("Testing machine account password policy rules\n");

			/* Workstation trust accounts don't seem to need to honour password quality policy */
			if (!test_SetUserPassEx(p, tctx, user_handle, true, &password)) {
				ret = false;
			}

			if (!test_ChangePasswordUser2(p, tctx, base_acct_name, &password, simple_pass, false)) {
				ret = false;
			}

			/* reset again, to allow another 'user' password change */
			if (!test_SetUserPassEx(p, tctx, user_handle, true, &password)) {
				ret = false;
			}

			/* Try a 'short' password */
			if (!test_ChangePasswordUser2(p, tctx, base_acct_name, &password, samr_rand_pass(tctx, 4), false)) {
				ret = false;
			}

			/* Try a compleatly random password */
			if (!test_ChangePasswordRandomBytes(p, tctx, base_acct_name, user_handle, &password)) {
				ret = false;
			}
		}

		for (i = 0; password_fields[i]; i++) {
			if (!test_SetUserPass_23(p, tctx, user_handle, password_fields[i], &password)) {
				ret = false;
			}	
		
			/* check it was set right */
			if (!test_ChangePasswordUser3(p, tctx, base_acct_name, 0, &password, NULL, 0, false)) {
				ret = false;
			}
		}		

		for (i = 0; password_fields[i]; i++) {
			if (!test_SetUserPass_25(p, tctx, user_handle, password_fields[i], &password)) {
				ret = false;
			}	
		
			/* check it was set right */
			if (!test_ChangePasswordUser3(p, tctx, base_acct_name, 0, &password, NULL, 0, false)) {
				ret = false;
			}
		}		

		if (!test_SetUserPassEx(p, tctx, user_handle, false, &password)) {
			ret = false;
		}	

		if (!test_ChangePassword(p, tctx, base_acct_name, domain_handle, &password)) {
			ret = false;
		}	

		if (torture_setting_bool(tctx, "samba4", false)) {
			printf("skipping Set Password level 18 and 21 against Samba4\n");
		} else {

			if (!test_SetUserPass_18(p, tctx, user_handle, &password)) {
				ret = false;
			}

			if (!test_ChangePasswordUser3(p, tctx, base_acct_name, 0, &password, NULL, 0, false)) {
				ret = false;
			}

			for (i = 0; password_fields[i]; i++) {

				if (password_fields[i] == SAMR_FIELD_LM_PASSWORD_PRESENT) {
					/* we need to skip as that would break
					 * the ChangePasswordUser3 verify */
					continue;
				}

				if (!test_SetUserPass_21(p, tctx, user_handle, password_fields[i], &password)) {
					ret = false;
				}

				/* check it was set right */
				if (!test_ChangePasswordUser3(p, tctx, base_acct_name, 0, &password, NULL, 0, false)) {
					ret = false;
				}
			}
		}

		q.in.user_handle = user_handle;
		q.in.level = 5;
		q.out.info = &info;
		
		status = dcerpc_samr_QueryUserInfo(p, tctx, &q);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo level %u failed - %s\n", 
			       q.in.level, nt_errstr(status));
			ret = false;
		} else {
			uint32_t expected_flags = (base_acct_flags | ACB_PWNOTREQ | ACB_DISABLED);
			if ((info->info5.acct_flags) != expected_flags) {
				printf("QuerUserInfo level 5 failed, it returned 0x%08x when we expected flags of 0x%08x\n",
				       info->info5.acct_flags,
				       expected_flags);
				ret = false;
			}
			if (info->info5.rid != rid) {
				printf("QuerUserInfo level 5 failed, it returned %u when we expected rid of %u\n",
				       info->info5.rid, rid);

			}
		}

		break;

	case TORTURE_SAMR_PASSWORDS_PWDLASTSET:

		/* test last password change timestamp behaviour */
		if (!test_SetPassword_pwdlastset(p, tctx, base_acct_flags,
						 base_acct_name,
						 user_handle, &password,
						 machine_credentials)) {
			ret = false;
		}

		if (ret == true) {
			torture_comment(tctx, "pwdLastSet test succeeded\n");
		} else {
			torture_warning(tctx, "pwdLastSet test failed\n");
		}

		break;

	case TORTURE_SAMR_OTHER:
		/* We just need the account to exist */
		break;
	}
	return ret;
}

static bool test_alias_ops(struct dcerpc_pipe *p, struct torture_context *tctx,
			   struct policy_handle *alias_handle,
			   const struct dom_sid *domain_sid)
{
	bool ret = true;

	if (!test_QuerySecurity(p, tctx, alias_handle)) {
		ret = false;
	}

	if (!test_QueryAliasInfo(p, tctx, alias_handle)) {
		ret = false;
	}

	if (!test_SetAliasInfo(p, tctx, alias_handle)) {
		ret = false;
	}

	if (!test_AddMemberToAlias(p, tctx, alias_handle, domain_sid)) {
		ret = false;
	}

	if (torture_setting_bool(tctx, "samba4", false)) {
		printf("skipping MultipleMembers Alias tests against Samba4\n");
		return ret;
	}

	if (!test_AddMultipleMembersToAlias(p, tctx, alias_handle)) {
		ret = false;
	}

	return ret;
}


static bool test_DeleteUser(struct dcerpc_pipe *p, struct torture_context *tctx,
				     struct policy_handle *user_handle)
{
    	struct samr_DeleteUser d;
	NTSTATUS status;
	torture_comment(tctx, "Testing DeleteUser\n");

	d.in.user_handle = user_handle;
	d.out.user_handle = user_handle;

	status = dcerpc_samr_DeleteUser(p, tctx, &d);
	torture_assert_ntstatus_ok(tctx, status, "DeleteUser");

	return true;
}

bool test_DeleteUser_byname(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			    struct policy_handle *handle, const char *name)
{
	NTSTATUS status;
	struct samr_DeleteUser d;
	struct policy_handle user_handle;
	uint32_t rid;

	status = test_LookupName(p, mem_ctx, handle, name, &rid);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	status = test_OpenUser_byname(p, mem_ctx, handle, name, &user_handle);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	d.in.user_handle = &user_handle;
	d.out.user_handle = &user_handle;
	status = dcerpc_samr_DeleteUser(p, mem_ctx, &d);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	return true;

failed:
	printf("DeleteUser_byname(%s) failed - %s\n", name, nt_errstr(status));
	return false;
}


static bool test_DeleteGroup_byname(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				    struct policy_handle *handle, const char *name)
{
	NTSTATUS status;
	struct samr_OpenGroup r;
	struct samr_DeleteDomainGroup d;
	struct policy_handle group_handle;
	uint32_t rid;

	status = test_LookupName(p, mem_ctx, handle, name, &rid);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	r.in.domain_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.group_handle = &group_handle;
	status = dcerpc_samr_OpenGroup(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	d.in.group_handle = &group_handle;
	d.out.group_handle = &group_handle;
	status = dcerpc_samr_DeleteDomainGroup(p, mem_ctx, &d);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	return true;

failed:
	printf("DeleteGroup_byname(%s) failed - %s\n", name, nt_errstr(status));
	return false;
}


static bool test_DeleteAlias_byname(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
				   struct policy_handle *domain_handle, const char *name)
{
	NTSTATUS status;
	struct samr_OpenAlias r;
	struct samr_DeleteDomAlias d;
	struct policy_handle alias_handle;
	uint32_t rid;

	printf("testing DeleteAlias_byname\n");

	status = test_LookupName(p, mem_ctx, domain_handle, name, &rid);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	r.in.domain_handle = domain_handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.alias_handle = &alias_handle;
	status = dcerpc_samr_OpenAlias(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	d.in.alias_handle = &alias_handle;
	d.out.alias_handle = &alias_handle;
	status = dcerpc_samr_DeleteDomAlias(p, mem_ctx, &d);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	return true;

failed:
	printf("DeleteAlias_byname(%s) failed - %s\n", name, nt_errstr(status));
	return false;
}

static bool test_DeleteAlias(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
				     struct policy_handle *alias_handle)
{
    	struct samr_DeleteDomAlias d;
	NTSTATUS status;
	bool ret = true;
	printf("Testing DeleteAlias\n");

	d.in.alias_handle = alias_handle;
	d.out.alias_handle = alias_handle;

	status = dcerpc_samr_DeleteDomAlias(p, mem_ctx, &d);
	if (!NT_STATUS_IS_OK(status)) {
		printf("DeleteAlias failed - %s\n", nt_errstr(status));
		ret = false;
	}

	return ret;
}

static bool test_CreateAlias(struct dcerpc_pipe *p, struct torture_context *tctx,
			    struct policy_handle *domain_handle, 
			     struct policy_handle *alias_handle, 
			     const struct dom_sid *domain_sid)
{
	NTSTATUS status;
	struct samr_CreateDomAlias r;
	struct lsa_String name;
	uint32_t rid;
	bool ret = true;

	init_lsa_String(&name, TEST_ALIASNAME);
	r.in.domain_handle = domain_handle;
	r.in.alias_name = &name;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.out.alias_handle = alias_handle;
	r.out.rid = &rid;

	printf("Testing CreateAlias (%s)\n", r.in.alias_name->string);

	status = dcerpc_samr_CreateDomAlias(p, tctx, &r);

	if (dom_sid_equal(domain_sid, dom_sid_parse_talloc(tctx, SID_BUILTIN))) {
		if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED)) {
			printf("Server correctly refused create of '%s'\n", r.in.alias_name->string);
			return true;
		} else {
			printf("Server should have refused create of '%s', got %s instead\n", r.in.alias_name->string, 
			       nt_errstr(status));
			return false;
		}
	}

	if (NT_STATUS_EQUAL(status, NT_STATUS_ALIAS_EXISTS)) {
		if (!test_DeleteAlias_byname(p, tctx, domain_handle, r.in.alias_name->string)) {
			return false;
		}
		status = dcerpc_samr_CreateDomAlias(p, tctx, &r);
	}

	if (!NT_STATUS_IS_OK(status)) {
		printf("CreateAlias failed - %s\n", nt_errstr(status));
		return false;
	}

	if (!test_alias_ops(p, tctx, alias_handle, domain_sid)) {
		ret = false;
	}

	return ret;
}

static bool test_ChangePassword(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				const char *acct_name,
				struct policy_handle *domain_handle, char **password)
{
	bool ret = true;

	if (!*password) {
		return false;
	}

	if (!test_ChangePasswordUser(p, mem_ctx, acct_name, domain_handle, password)) {
		ret = false;
	}

	if (!test_ChangePasswordUser2(p, mem_ctx, acct_name, password, 0, true)) {
		ret = false;
	}

	if (!test_OemChangePasswordUser2(p, mem_ctx, acct_name, domain_handle, password)) {
		ret = false;
	}

	/* test what happens when setting the old password again */
	if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, 0, password, *password, 0, true)) {
		ret = false;
	}

	{
		char simple_pass[9];
		char *v = generate_random_str(mem_ctx, 1);

		ZERO_STRUCT(simple_pass);
		memset(simple_pass, *v, sizeof(simple_pass) - 1);

		/* test what happens when picking a simple password */
		if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, 0, password, simple_pass, 0, true)) {
			ret = false;
		}
	}

	/* set samr_SetDomainInfo level 1 with min_length 5 */
	{
		struct samr_QueryDomainInfo r;
		union samr_DomainInfo *info = NULL;
		struct samr_SetDomainInfo s;
		uint16_t len_old, len;
		uint32_t pwd_prop_old;
		int64_t min_pwd_age_old;
		NTSTATUS status;

		len = 5;

		r.in.domain_handle = domain_handle;
		r.in.level = 1;
		r.out.info = &info;

		printf("testing samr_QueryDomainInfo level 1\n");
		status = dcerpc_samr_QueryDomainInfo(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			return false;
		}

		s.in.domain_handle = domain_handle;
		s.in.level = 1;
		s.in.info = info;

		/* remember the old min length, so we can reset it */
		len_old = s.in.info->info1.min_password_length;
		s.in.info->info1.min_password_length = len;
		pwd_prop_old = s.in.info->info1.password_properties;
		/* turn off password complexity checks for this test */
		s.in.info->info1.password_properties &= ~DOMAIN_PASSWORD_COMPLEX;

		min_pwd_age_old = s.in.info->info1.min_password_age;
		s.in.info->info1.min_password_age = 0;

		printf("testing samr_SetDomainInfo level 1\n");
		status = dcerpc_samr_SetDomainInfo(p, mem_ctx, &s);
		if (!NT_STATUS_IS_OK(status)) {
			return false;
		}

		printf("calling test_ChangePasswordUser3 with too short password\n");

		if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, len - 1, password, NULL, 0, true)) {
			ret = false;
		}

		s.in.info->info1.min_password_length = len_old;
		s.in.info->info1.password_properties = pwd_prop_old;
		s.in.info->info1.min_password_age = min_pwd_age_old;
		
		printf("testing samr_SetDomainInfo level 1\n");
		status = dcerpc_samr_SetDomainInfo(p, mem_ctx, &s);
		if (!NT_STATUS_IS_OK(status)) {
			return false;
		}

	}

	{
		NTSTATUS status;
		struct samr_OpenUser r;
		struct samr_QueryUserInfo q;
		union samr_UserInfo *info;
		struct samr_LookupNames n;
		struct policy_handle user_handle;
		struct samr_Ids rids, types;

		n.in.domain_handle = domain_handle;
		n.in.num_names = 1;
		n.in.names = talloc_array(mem_ctx, struct lsa_String, 1);
		n.in.names[0].string = acct_name; 
		n.out.rids = &rids;
		n.out.types = &types;

		status = dcerpc_samr_LookupNames(p, mem_ctx, &n);
		if (!NT_STATUS_IS_OK(status)) {
			printf("LookupNames failed - %s\n", nt_errstr(status));
			return false;
		}

		r.in.domain_handle = domain_handle;
		r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
		r.in.rid = n.out.rids->ids[0];
		r.out.user_handle = &user_handle;

		status = dcerpc_samr_OpenUser(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("OpenUser(%u) failed - %s\n", n.out.rids->ids[0], nt_errstr(status));
			return false;
		}

		q.in.user_handle = &user_handle;
		q.in.level = 5;
		q.out.info = &info;

		status = dcerpc_samr_QueryUserInfo(p, mem_ctx, &q);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo failed - %s\n", nt_errstr(status));
			return false;
		}

		printf("calling test_ChangePasswordUser3 with too early password change\n");

		if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, 0, password, NULL, 
					      info->info5.last_password_change, true)) {
			ret = false;
		}
	}

	/* we change passwords twice - this has the effect of verifying
	   they were changed correctly for the final call */
	if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, 0, password, NULL, 0, true)) {
		ret = false;
	}

	if (!test_ChangePasswordUser3(p, mem_ctx, acct_name, 0, password, NULL, 0, true)) {
		ret = false;
	}

	return ret;
}

static bool test_CreateUser(struct dcerpc_pipe *p, struct torture_context *tctx,
			    struct policy_handle *domain_handle, 
			    struct policy_handle *user_handle_out,
			    struct dom_sid *domain_sid, 
			    enum torture_samr_choice which_ops,
			    struct cli_credentials *machine_credentials)
{

	TALLOC_CTX *user_ctx;

	NTSTATUS status;
	struct samr_CreateUser r;
	struct samr_QueryUserInfo q;
	union samr_UserInfo *info;
	struct samr_DeleteUser d;
	uint32_t rid;

	/* This call creates a 'normal' account - check that it really does */
	const uint32_t acct_flags = ACB_NORMAL;
	struct lsa_String name;
	bool ret = true;

	struct policy_handle user_handle;
	user_ctx = talloc_named(tctx, 0, "test_CreateUser2 per-user context");
	init_lsa_String(&name, TEST_ACCOUNT_NAME);

	r.in.domain_handle = domain_handle;
	r.in.account_name = &name;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.out.user_handle = &user_handle;
	r.out.rid = &rid;

	printf("Testing CreateUser(%s)\n", r.in.account_name->string);

	status = dcerpc_samr_CreateUser(p, user_ctx, &r);

	if (dom_sid_equal(domain_sid, dom_sid_parse_talloc(tctx, SID_BUILTIN))) {
		if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED) || NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
			printf("Server correctly refused create of '%s'\n", r.in.account_name->string);
			return true;
		} else {
			printf("Server should have refused create of '%s', got %s instead\n", r.in.account_name->string, 
			       nt_errstr(status));
			return false;
		}
	}

	if (NT_STATUS_EQUAL(status, NT_STATUS_USER_EXISTS)) {
		if (!test_DeleteUser_byname(p, user_ctx, domain_handle, r.in.account_name->string)) {
			talloc_free(user_ctx);
			return false;
		}
		status = dcerpc_samr_CreateUser(p, user_ctx, &r);
	}
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(user_ctx);
		printf("CreateUser failed - %s\n", nt_errstr(status));
		return false;
	} else {
		q.in.user_handle = &user_handle;
		q.in.level = 16;
		q.out.info = &info;
		
		status = dcerpc_samr_QueryUserInfo(p, user_ctx, &q);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo level %u failed - %s\n", 
			       q.in.level, nt_errstr(status));
			ret = false;
		} else {
			if ((info->info16.acct_flags & acct_flags) != acct_flags) {
				printf("QuerUserInfo level 16 failed, it returned 0x%08x when we expected flags of 0x%08x\n",
				       info->info16.acct_flags,
				       acct_flags);
				ret = false;
			}
		}
		
		if (!test_user_ops(p, tctx, &user_handle, domain_handle, 
				   acct_flags, name.string, which_ops,
				   machine_credentials)) {
			ret = false;
		}
		
		if (user_handle_out) {
			*user_handle_out = user_handle;
		} else {
			printf("Testing DeleteUser (createuser test)\n");
			
			d.in.user_handle = &user_handle;
			d.out.user_handle = &user_handle;
			
			status = dcerpc_samr_DeleteUser(p, user_ctx, &d);
			if (!NT_STATUS_IS_OK(status)) {
				printf("DeleteUser failed - %s\n", nt_errstr(status));
				ret = false;
			}
		}
		
	}

	talloc_free(user_ctx);
	
	return ret;
}


static bool test_CreateUser2(struct dcerpc_pipe *p, struct torture_context *tctx,
			     struct policy_handle *domain_handle,
			     struct dom_sid *domain_sid,
			     enum torture_samr_choice which_ops,
			     struct cli_credentials *machine_credentials)
{
	NTSTATUS status;
	struct samr_CreateUser2 r;
	struct samr_QueryUserInfo q;
	union samr_UserInfo *info;
	struct samr_DeleteUser d;
	struct policy_handle user_handle;
	uint32_t rid;
	struct lsa_String name;
	bool ret = true;
	int i;

	struct {
		uint32_t acct_flags;
		const char *account_name;
		NTSTATUS nt_status;
	} account_types[] = {
		{ ACB_NORMAL, TEST_ACCOUNT_NAME, NT_STATUS_OK },
		{ ACB_NORMAL | ACB_DISABLED, TEST_ACCOUNT_NAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_NORMAL | ACB_PWNOEXP, TEST_ACCOUNT_NAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_WSTRUST, TEST_MACHINENAME, NT_STATUS_OK },
		{ ACB_WSTRUST | ACB_DISABLED, TEST_MACHINENAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_WSTRUST | ACB_PWNOEXP, TEST_MACHINENAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_SVRTRUST, TEST_MACHINENAME, NT_STATUS_OK },
		{ ACB_SVRTRUST | ACB_DISABLED, TEST_MACHINENAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_SVRTRUST | ACB_PWNOEXP, TEST_MACHINENAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_DOMTRUST, TEST_DOMAINNAME, NT_STATUS_OK },
		{ ACB_DOMTRUST | ACB_DISABLED, TEST_DOMAINNAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_DOMTRUST | ACB_PWNOEXP, TEST_DOMAINNAME, NT_STATUS_INVALID_PARAMETER },
		{ 0, TEST_ACCOUNT_NAME, NT_STATUS_INVALID_PARAMETER },
		{ ACB_DISABLED, TEST_ACCOUNT_NAME, NT_STATUS_INVALID_PARAMETER },
		{ 0, NULL, NT_STATUS_INVALID_PARAMETER }
	};

	for (i = 0; account_types[i].account_name; i++) {
		TALLOC_CTX *user_ctx;
		uint32_t acct_flags = account_types[i].acct_flags;
		uint32_t access_granted;
		user_ctx = talloc_named(tctx, 0, "test_CreateUser2 per-user context");
		init_lsa_String(&name, account_types[i].account_name);

		r.in.domain_handle = domain_handle;
		r.in.account_name = &name;
		r.in.acct_flags = acct_flags;
		r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
		r.out.user_handle = &user_handle;
		r.out.access_granted = &access_granted;
		r.out.rid = &rid;
		
		printf("Testing CreateUser2(%s, 0x%x)\n", r.in.account_name->string, acct_flags);
		
		status = dcerpc_samr_CreateUser2(p, user_ctx, &r);
		
		if (dom_sid_equal(domain_sid, dom_sid_parse_talloc(tctx, SID_BUILTIN))) {
			if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED) || NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
				printf("Server correctly refused create of '%s'\n", r.in.account_name->string);
				continue;
			} else {
				printf("Server should have refused create of '%s', got %s instead\n", r.in.account_name->string, 
				       nt_errstr(status));
				ret = false;
				continue;
			}
		}

		if (NT_STATUS_EQUAL(status, NT_STATUS_USER_EXISTS)) {
			if (!test_DeleteUser_byname(p, user_ctx, domain_handle, r.in.account_name->string)) {
				talloc_free(user_ctx);
				ret = false;
				continue;
			}
			status = dcerpc_samr_CreateUser2(p, user_ctx, &r);

		}
		if (!NT_STATUS_EQUAL(status, account_types[i].nt_status)) {
			printf("CreateUser2 failed gave incorrect error return - %s (should be %s)\n", 
			       nt_errstr(status), nt_errstr(account_types[i].nt_status));
			ret = false;
		}
		
		if (NT_STATUS_IS_OK(status)) {
			q.in.user_handle = &user_handle;
			q.in.level = 5;
			q.out.info = &info;
			
			status = dcerpc_samr_QueryUserInfo(p, user_ctx, &q);
			if (!NT_STATUS_IS_OK(status)) {
				printf("QueryUserInfo level %u failed - %s\n", 
				       q.in.level, nt_errstr(status));
				ret = false;
			} else {
				uint32_t expected_flags = (acct_flags | ACB_PWNOTREQ | ACB_DISABLED);
				if (acct_flags == ACB_NORMAL) {
					expected_flags |= ACB_PW_EXPIRED;
				}
				if ((info->info5.acct_flags) != expected_flags) {
					printf("QuerUserInfo level 5 failed, it returned 0x%08x when we expected flags of 0x%08x\n",
					       info->info5.acct_flags,
					       expected_flags);
					ret = false;
				} 
				switch (acct_flags) {
				case ACB_SVRTRUST:
					if (info->info5.primary_gid != DOMAIN_RID_DCS) {
						printf("QuerUserInfo level 5: DC should have had Primary Group %d, got %d\n", 
						       DOMAIN_RID_DCS, info->info5.primary_gid);
						ret = false;
					}
					break;
				case ACB_WSTRUST:
					if (info->info5.primary_gid != DOMAIN_RID_DOMAIN_MEMBERS) {
						printf("QuerUserInfo level 5: Domain Member should have had Primary Group %d, got %d\n", 
						       DOMAIN_RID_DOMAIN_MEMBERS, info->info5.primary_gid);
						ret = false;
					}
					break;
				case ACB_NORMAL:
					if (info->info5.primary_gid != DOMAIN_RID_USERS) {
						printf("QuerUserInfo level 5: Users should have had Primary Group %d, got %d\n", 
						       DOMAIN_RID_USERS, info->info5.primary_gid);
						ret = false;
					}
					break;
				}
			}
		
			if (!test_user_ops(p, tctx, &user_handle, domain_handle, 
					   acct_flags, name.string, which_ops,
					   machine_credentials)) {
				ret = false;
			}

			printf("Testing DeleteUser (createuser2 test)\n");
		
			d.in.user_handle = &user_handle;
			d.out.user_handle = &user_handle;
			
			status = dcerpc_samr_DeleteUser(p, user_ctx, &d);
			if (!NT_STATUS_IS_OK(status)) {
				printf("DeleteUser failed - %s\n", nt_errstr(status));
				ret = false;
			}
		}
		talloc_free(user_ctx);
	}

	return ret;
}

static bool test_QueryAliasInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryAliasInfo r;
	union samr_AliasInfo *info;
	uint16_t levels[] = {1, 2, 3};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryAliasInfo level %u\n", levels[i]);

		r.in.alias_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryAliasInfo(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryAliasInfo level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_QueryGroupInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryGroupInfo r;
	union samr_GroupInfo *info;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryGroupInfo level %u\n", levels[i]);

		r.in.group_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryGroupInfo(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryGroupInfo level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_QueryGroupMember(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryGroupMember r;
	struct samr_RidTypeArray *rids = NULL;
	bool ret = true;

	printf("Testing QueryGroupMember\n");

	r.in.group_handle = handle;
	r.out.rids = &rids;

	status = dcerpc_samr_QueryGroupMember(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("QueryGroupInfo failed - %s\n", nt_errstr(status));
		ret = false;
	}

	return ret;
}


static bool test_SetGroupInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			      struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryGroupInfo r;
	union samr_GroupInfo *info;
	struct samr_SetGroupInfo s;
	uint16_t levels[] = {1, 2, 3, 4};
	uint16_t set_ok[] = {0, 1, 1, 1};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryGroupInfo level %u\n", levels[i]);

		r.in.group_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryGroupInfo(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryGroupInfo level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}

		printf("Testing SetGroupInfo level %u\n", levels[i]);

		s.in.group_handle = handle;
		s.in.level = levels[i];
		s.in.info = *r.out.info;

#if 0
		/* disabled this, as it changes the name only from the point of view of samr, 
		   but leaves the name from the point of view of w2k3 internals (and ldap). This means
		   the name is still reserved, so creating the old name fails, but deleting by the old name
		   also fails */
		if (s.in.level == 2) {
			init_lsa_String(&s.in.info->string, "NewName");
		}
#endif

		if (s.in.level == 4) {
			init_lsa_String(&s.in.info->description, "test description");
		}

		status = dcerpc_samr_SetGroupInfo(p, mem_ctx, &s);
		if (set_ok[i]) {
			if (!NT_STATUS_IS_OK(status)) {
				printf("SetGroupInfo level %u failed - %s\n", 
				       r.in.level, nt_errstr(status));
				ret = false;
				continue;
			}
		} else {
			if (!NT_STATUS_EQUAL(NT_STATUS_INVALID_INFO_CLASS, status)) {
				printf("SetGroupInfo level %u gave %s - should have been NT_STATUS_INVALID_INFO_CLASS\n", 
				       r.in.level, nt_errstr(status));
				ret = false;
				continue;
			}
		}
	}

	return ret;
}

static bool test_QueryUserInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryUserInfo r;
	union samr_UserInfo *info;
	uint16_t levels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
			   11, 12, 13, 14, 16, 17, 20, 21};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryUserInfo level %u\n", levels[i]);

		r.in.user_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryUserInfo(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_QueryUserInfo2(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryUserInfo2 r;
	union samr_UserInfo *info;
	uint16_t levels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
			   11, 12, 13, 14, 16, 17, 20, 21};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryUserInfo2 level %u\n", levels[i]);

		r.in.user_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryUserInfo2(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo2 level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}

	return ret;
}

static bool test_OpenUser(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			  struct policy_handle *handle, uint32_t rid)
{
	NTSTATUS status;
	struct samr_OpenUser r;
	struct policy_handle user_handle;
	bool ret = true;

	printf("Testing OpenUser(%u)\n", rid);

	r.in.domain_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.user_handle = &user_handle;

	status = dcerpc_samr_OpenUser(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenUser(%u) failed - %s\n", rid, nt_errstr(status));
		return false;
	}

	if (!test_QuerySecurity(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	if (!test_QueryUserInfo(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	if (!test_QueryUserInfo2(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	if (!test_GetUserPwInfo(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	if (!test_GetGroupsForUser(p,mem_ctx, &user_handle)) {
		ret = false;
	}

	if (!test_samr_handle_Close(p, mem_ctx, &user_handle)) {
		ret = false;
	}

	return ret;
}

static bool test_OpenGroup(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			   struct policy_handle *handle, uint32_t rid)
{
	NTSTATUS status;
	struct samr_OpenGroup r;
	struct policy_handle group_handle;
	bool ret = true;

	printf("Testing OpenGroup(%u)\n", rid);

	r.in.domain_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.group_handle = &group_handle;

	status = dcerpc_samr_OpenGroup(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenGroup(%u) failed - %s\n", rid, nt_errstr(status));
		return false;
	}

	if (!test_QuerySecurity(p, mem_ctx, &group_handle)) {
		ret = false;
	}

	if (!test_QueryGroupInfo(p, mem_ctx, &group_handle)) {
		ret = false;
	}

	if (!test_QueryGroupMember(p, mem_ctx, &group_handle)) {
		ret = false;
	}

	if (!test_samr_handle_Close(p, mem_ctx, &group_handle)) {
		ret = false;
	}

	return ret;
}

static bool test_OpenAlias(struct dcerpc_pipe *p, struct torture_context *tctx,
			   struct policy_handle *handle, uint32_t rid)
{
	NTSTATUS status;
	struct samr_OpenAlias r;
	struct policy_handle alias_handle;
	bool ret = true;

	torture_comment(tctx, "Testing OpenAlias(%u)\n", rid);

	r.in.domain_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.alias_handle = &alias_handle;

	status = dcerpc_samr_OpenAlias(p, tctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenAlias(%u) failed - %s\n", rid, nt_errstr(status));
		return false;
	}

	if (!test_QuerySecurity(p, tctx, &alias_handle)) {
		ret = false;
	}

	if (!test_QueryAliasInfo(p, tctx, &alias_handle)) {
		ret = false;
	}

	if (!test_GetMembersInAlias(p, tctx, &alias_handle)) {
		ret = false;
	}

	if (!test_samr_handle_Close(p, tctx, &alias_handle)) {
		ret = false;
	}

	return ret;
}

static bool check_mask(struct dcerpc_pipe *p, struct torture_context *tctx,
		       struct policy_handle *handle, uint32_t rid, 
		       uint32_t acct_flag_mask)
{
	NTSTATUS status;
	struct samr_OpenUser r;
	struct samr_QueryUserInfo q;
	union samr_UserInfo *info;
	struct policy_handle user_handle;
	bool ret = true;

	torture_comment(tctx, "Testing OpenUser(%u)\n", rid);

	r.in.domain_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.rid = rid;
	r.out.user_handle = &user_handle;

	status = dcerpc_samr_OpenUser(p, tctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenUser(%u) failed - %s\n", rid, nt_errstr(status));
		return false;
	}

	q.in.user_handle = &user_handle;
	q.in.level = 16;
	q.out.info = &info;
	
	status = dcerpc_samr_QueryUserInfo(p, tctx, &q);
	if (!NT_STATUS_IS_OK(status)) {
		printf("QueryUserInfo level 16 failed - %s\n", 
		       nt_errstr(status));
		ret = false;
	} else {
		if ((acct_flag_mask & info->info16.acct_flags) == 0) {
			printf("Server failed to filter for 0x%x, allowed 0x%x (%d) on EnumDomainUsers\n",
			       acct_flag_mask, info->info16.acct_flags, rid);
			ret = false;
		}
	}
	
	if (!test_samr_handle_Close(p, tctx, &user_handle)) {
		ret = false;
	}

	return ret;
}

static bool test_EnumDomainUsers(struct dcerpc_pipe *p, struct torture_context *tctx,
				 struct policy_handle *handle)
{
	NTSTATUS status = STATUS_MORE_ENTRIES;
	struct samr_EnumDomainUsers r;
	uint32_t mask, resume_handle=0;
	int i, mask_idx;
	bool ret = true;
	struct samr_LookupNames n;
	struct samr_LookupRids  lr ;
	struct lsa_Strings names;
	struct samr_Ids rids, types;
	struct samr_SamArray *sam = NULL;
	uint32_t num_entries = 0;

	uint32_t masks[] = {ACB_NORMAL, ACB_DOMTRUST, ACB_WSTRUST, 
			    ACB_DISABLED, ACB_NORMAL | ACB_DISABLED, 
			    ACB_SVRTRUST | ACB_DOMTRUST | ACB_WSTRUST, 
			    ACB_PWNOEXP, 0};

	printf("Testing EnumDomainUsers\n");

	for (mask_idx=0;mask_idx<ARRAY_SIZE(masks);mask_idx++) {
		r.in.domain_handle = handle;
		r.in.resume_handle = &resume_handle;
		r.in.acct_flags = mask = masks[mask_idx];
		r.in.max_size = (uint32_t)-1;
		r.out.resume_handle = &resume_handle;
		r.out.num_entries = &num_entries;
		r.out.sam = &sam;

		status = dcerpc_samr_EnumDomainUsers(p, tctx, &r);
		if (!NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES) &&  
		    !NT_STATUS_IS_OK(status)) {
			printf("EnumDomainUsers failed - %s\n", nt_errstr(status));
			return false;
		}
	
		torture_assert(tctx, sam, "EnumDomainUsers failed: r.out.sam unexpectedly NULL");

		if (sam->count == 0) {
			continue;
		}

		for (i=0;i<sam->count;i++) {
			if (mask) {
				if (!check_mask(p, tctx, handle, sam->entries[i].idx, mask)) {
					ret = false;
				}
			} else if (!test_OpenUser(p, tctx, handle, sam->entries[i].idx)) {
				ret = false;
			}
		}
	}

	printf("Testing LookupNames\n");
	n.in.domain_handle = handle;
	n.in.num_names = sam->count;
	n.in.names = talloc_array(tctx, struct lsa_String, sam->count);
	n.out.rids = &rids;
	n.out.types = &types;
	for (i=0;i<sam->count;i++) {
		n.in.names[i].string = sam->entries[i].name.string;
	}
	status = dcerpc_samr_LookupNames(p, tctx, &n);
	if (!NT_STATUS_IS_OK(status)) {
		printf("LookupNames failed - %s\n", nt_errstr(status));
		ret = false;
	}


	printf("Testing LookupRids\n");
	lr.in.domain_handle = handle;
	lr.in.num_rids = sam->count;
	lr.in.rids = talloc_array(tctx, uint32_t, sam->count);
	lr.out.names = &names;
	lr.out.types = &types;
	for (i=0;i<sam->count;i++) {
		lr.in.rids[i] = sam->entries[i].idx;
	}
	status = dcerpc_samr_LookupRids(p, tctx, &lr);
	torture_assert_ntstatus_ok(tctx, status, "LookupRids");

	return ret;	
}

/*
  try blasting the server with a bunch of sync requests
*/
static bool test_EnumDomainUsers_async(struct dcerpc_pipe *p, struct torture_context *tctx, 
				       struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_EnumDomainUsers r;
	uint32_t resume_handle=0;
	int i;
#define ASYNC_COUNT 100
	struct rpc_request *req[ASYNC_COUNT];

	if (!torture_setting_bool(tctx, "dangerous", false)) {
		torture_skip(tctx, "samr async test disabled - enable dangerous tests to use\n");
	}

	torture_comment(tctx, "Testing EnumDomainUsers_async\n");

	r.in.domain_handle = handle;
	r.in.resume_handle = &resume_handle;
	r.in.acct_flags = 0;
	r.in.max_size = (uint32_t)-1;
	r.out.resume_handle = &resume_handle;

	for (i=0;i<ASYNC_COUNT;i++) {
		req[i] = dcerpc_samr_EnumDomainUsers_send(p, tctx, &r);
	}

	for (i=0;i<ASYNC_COUNT;i++) {
		status = dcerpc_ndr_request_recv(req[i]);
		if (!NT_STATUS_IS_OK(status)) {
			printf("EnumDomainUsers[%d] failed - %s\n", 
			       i, nt_errstr(status));
			return false;
		}
	}
	
	torture_comment(tctx, "%d async requests OK\n", i);

	return true;
}

static bool test_EnumDomainGroups(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_EnumDomainGroups r;
	uint32_t resume_handle=0;
	struct samr_SamArray *sam = NULL;
	uint32_t num_entries = 0;
	int i;
	bool ret = true;

	printf("Testing EnumDomainGroups\n");

	r.in.domain_handle = handle;
	r.in.resume_handle = &resume_handle;
	r.in.max_size = (uint32_t)-1;
	r.out.resume_handle = &resume_handle;
	r.out.num_entries = &num_entries;
	r.out.sam = &sam;

	status = dcerpc_samr_EnumDomainGroups(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("EnumDomainGroups failed - %s\n", nt_errstr(status));
		return false;
	}
	
	if (!sam) {
		return false;
	}

	for (i=0;i<sam->count;i++) {
		if (!test_OpenGroup(p, mem_ctx, handle, sam->entries[i].idx)) {
			ret = false;
		}
	}

	return ret;
}

static bool test_EnumDomainAliases(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				   struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_EnumDomainAliases r;
	uint32_t resume_handle=0;
	struct samr_SamArray *sam = NULL;
	uint32_t num_entries = 0;
	int i;
	bool ret = true;

	printf("Testing EnumDomainAliases\n");

	r.in.domain_handle = handle;
	r.in.resume_handle = &resume_handle;
	r.in.max_size = (uint32_t)-1;
	r.out.sam = &sam;
	r.out.num_entries = &num_entries;
	r.out.resume_handle = &resume_handle;

	status = dcerpc_samr_EnumDomainAliases(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("EnumDomainAliases failed - %s\n", nt_errstr(status));
		return false;
	}
	
	if (!sam) {
		return false;
	}

	for (i=0;i<sam->count;i++) {
		if (!test_OpenAlias(p, mem_ctx, handle, sam->entries[i].idx)) {
			ret = false;
		}
	}

	return ret;	
}

static bool test_GetDisplayEnumerationIndex(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
					    struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_GetDisplayEnumerationIndex r;
	bool ret = true;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	uint16_t ok_lvl[] = {1, 1, 1, 0, 0};
	struct lsa_String name;
	uint32_t idx = 0;
	int i;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing GetDisplayEnumerationIndex level %u\n", levels[i]);

		init_lsa_String(&name, TEST_ACCOUNT_NAME);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.in.name = &name;
		r.out.idx = &idx;

		status = dcerpc_samr_GetDisplayEnumerationIndex(p, mem_ctx, &r);

		if (ok_lvl[i] && 
		    !NT_STATUS_IS_OK(status) &&
		    !NT_STATUS_EQUAL(NT_STATUS_NO_MORE_ENTRIES, status)) {
			printf("GetDisplayEnumerationIndex level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}

		init_lsa_String(&name, "zzzzzzzz");

		status = dcerpc_samr_GetDisplayEnumerationIndex(p, mem_ctx, &r);
		
		if (ok_lvl[i] && !NT_STATUS_EQUAL(NT_STATUS_NO_MORE_ENTRIES, status)) {
			printf("GetDisplayEnumerationIndex level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}
	
	return ret;	
}

static bool test_GetDisplayEnumerationIndex2(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
					     struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_GetDisplayEnumerationIndex2 r;
	bool ret = true;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	uint16_t ok_lvl[] = {1, 1, 1, 0, 0};
	struct lsa_String name;
	uint32_t idx = 0;
	int i;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing GetDisplayEnumerationIndex2 level %u\n", levels[i]);

		init_lsa_String(&name, TEST_ACCOUNT_NAME);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.in.name = &name;
		r.out.idx = &idx;

		status = dcerpc_samr_GetDisplayEnumerationIndex2(p, mem_ctx, &r);
		if (ok_lvl[i] && 
		    !NT_STATUS_IS_OK(status) && 
		    !NT_STATUS_EQUAL(NT_STATUS_NO_MORE_ENTRIES, status)) {
			printf("GetDisplayEnumerationIndex2 level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}

		init_lsa_String(&name, "zzzzzzzz");

		status = dcerpc_samr_GetDisplayEnumerationIndex2(p, mem_ctx, &r);
		if (ok_lvl[i] && !NT_STATUS_EQUAL(NT_STATUS_NO_MORE_ENTRIES, status)) {
			printf("GetDisplayEnumerationIndex2 level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}
	
	return ret;	
}

#define STRING_EQUAL_QUERY(s1, s2, user)					\
	if (s1.string == NULL && s2.string != NULL && s2.string[0] == '\0') { \
		/* odd, but valid */						\
	} else if ((s1.string && !s2.string) || (s2.string && !s1.string) || strcmp(s1.string, s2.string)) { \
			printf("%s mismatch for %s: %s != %s (%s)\n", \
			       #s1, user.string,  s1.string, s2.string, __location__);   \
			ret = false; \
	}
#define INT_EQUAL_QUERY(s1, s2, user)		\
		if (s1 != s2) { \
			printf("%s mismatch for %s: 0x%llx != 0x%llx (%s)\n", \
			       #s1, user.string, (unsigned long long)s1, (unsigned long long)s2, __location__); \
			ret = false; \
		}

static bool test_each_DisplayInfo_user(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				       struct samr_QueryDisplayInfo *querydisplayinfo,
				       bool *seen_testuser) 
{
	struct samr_OpenUser r;
	struct samr_QueryUserInfo q;
	union samr_UserInfo *info;
	struct policy_handle user_handle;
	int i, ret = true;
	NTSTATUS status;
	r.in.domain_handle = querydisplayinfo->in.domain_handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	for (i = 0; ; i++) {
		switch (querydisplayinfo->in.level) {
		case 1:
			if (i >= querydisplayinfo->out.info->info1.count) {
				return ret;
			}
			r.in.rid = querydisplayinfo->out.info->info1.entries[i].rid;
			break;
		case 2:
			if (i >= querydisplayinfo->out.info->info2.count) {
				return ret;
			}
			r.in.rid = querydisplayinfo->out.info->info2.entries[i].rid;
			break;
		case 3:
			/* Groups */
		case 4:
		case 5:
			/* Not interested in validating just the account name */
			return true;
		}
			
		r.out.user_handle = &user_handle;
		
		switch (querydisplayinfo->in.level) {
		case 1:
		case 2:
			status = dcerpc_samr_OpenUser(p, mem_ctx, &r);
			if (!NT_STATUS_IS_OK(status)) {
				printf("OpenUser(%u) failed - %s\n", r.in.rid, nt_errstr(status));
				return false;
			}
		}
		
		q.in.user_handle = &user_handle;
		q.in.level = 21;
		q.out.info = &info;
		status = dcerpc_samr_QueryUserInfo(p, mem_ctx, &q);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryUserInfo(%u) failed - %s\n", r.in.rid, nt_errstr(status));
			return false;
		}
		
		switch (querydisplayinfo->in.level) {
		case 1:
			if (seen_testuser && strcmp(info->info21.account_name.string, TEST_ACCOUNT_NAME) == 0) {
				*seen_testuser = true;
			}
			STRING_EQUAL_QUERY(querydisplayinfo->out.info->info1.entries[i].full_name,
					   info->info21.full_name, info->info21.account_name);
			STRING_EQUAL_QUERY(querydisplayinfo->out.info->info1.entries[i].account_name,
					   info->info21.account_name, info->info21.account_name);
			STRING_EQUAL_QUERY(querydisplayinfo->out.info->info1.entries[i].description,
					   info->info21.description, info->info21.account_name);
			INT_EQUAL_QUERY(querydisplayinfo->out.info->info1.entries[i].rid,
					info->info21.rid, info->info21.account_name);
			INT_EQUAL_QUERY(querydisplayinfo->out.info->info1.entries[i].acct_flags,
					info->info21.acct_flags, info->info21.account_name);
			
			break;
		case 2:
			STRING_EQUAL_QUERY(querydisplayinfo->out.info->info2.entries[i].account_name,
					   info->info21.account_name, info->info21.account_name);
			STRING_EQUAL_QUERY(querydisplayinfo->out.info->info2.entries[i].description,
					   info->info21.description, info->info21.account_name);
			INT_EQUAL_QUERY(querydisplayinfo->out.info->info2.entries[i].rid,
					info->info21.rid, info->info21.account_name);
			INT_EQUAL_QUERY((querydisplayinfo->out.info->info2.entries[i].acct_flags & ~ACB_NORMAL),
					info->info21.acct_flags, info->info21.account_name);
			
			if (!(querydisplayinfo->out.info->info2.entries[i].acct_flags & ACB_NORMAL)) {
				printf("Missing ACB_NORMAL in querydisplayinfo->out.info.info2.entries[i].acct_flags on %s\n", 
				       info->info21.account_name.string);
			}

			if (!(info->info21.acct_flags & (ACB_WSTRUST | ACB_SVRTRUST))) {
				printf("Found non-trust account %s in trust account listing: 0x%x 0x%x\n",
				       info->info21.account_name.string,
				       querydisplayinfo->out.info->info2.entries[i].acct_flags,
				       info->info21.acct_flags);
				return false;
			}
			
			break;
		}
		
		if (!test_samr_handle_Close(p, mem_ctx, &user_handle)) {
			return false;
		}
	}
	return ret;
}

static bool test_QueryDisplayInfo(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDisplayInfo r;
	struct samr_QueryDomainInfo dom_info;
	union samr_DomainInfo *info = NULL;
	bool ret = true;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	int i;
	bool seen_testuser = false;
	uint32_t total_size;
	uint32_t returned_size;
	union samr_DispInfo disp_info;


	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryDisplayInfo level %u\n", levels[i]);

		r.in.start_idx = 0;
		status = STATUS_MORE_ENTRIES;
		while (NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES)) {
			r.in.domain_handle = handle;
			r.in.level = levels[i];
			r.in.max_entries = 2;
			r.in.buf_size = (uint32_t)-1;
			r.out.total_size = &total_size;
			r.out.returned_size = &returned_size;
			r.out.info = &disp_info;
			
			status = dcerpc_samr_QueryDisplayInfo(p, mem_ctx, &r);
			if (!NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES) && !NT_STATUS_IS_OK(status)) {
				printf("QueryDisplayInfo level %u failed - %s\n", 
				       levels[i], nt_errstr(status));
				ret = false;
			}
			switch (r.in.level) {
			case 1:
				if (!test_each_DisplayInfo_user(p, mem_ctx, &r, &seen_testuser)) {
					ret = false;
				}
				r.in.start_idx += r.out.info->info1.count;
				break;
			case 2:
				if (!test_each_DisplayInfo_user(p, mem_ctx, &r, NULL)) {
					ret = false;
				}
				r.in.start_idx += r.out.info->info2.count;
				break;
			case 3:
				r.in.start_idx += r.out.info->info3.count;
				break;
			case 4:
				r.in.start_idx += r.out.info->info4.count;
				break;
			case 5:
				r.in.start_idx += r.out.info->info5.count;
				break;
			}
		}
		dom_info.in.domain_handle = handle;
		dom_info.in.level = 2;
		dom_info.out.info = &info;

		/* Check number of users returned is correct */
		status = dcerpc_samr_QueryDomainInfo(p, mem_ctx, &dom_info);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDomainInfo level %u failed - %s\n", 
			       r.in.level, nt_errstr(status));
				ret = false;
				break;
		}
		switch (r.in.level) {
		case 1:
		case 4:
			if (info->general.num_users < r.in.start_idx) {
				printf("QueryDomainInfo indicates that QueryDisplayInfo returned more users (%d/%d) than the domain %s is said to contain!\n",
				       r.in.start_idx, info->general.num_groups,
				       info->general.domain_name.string);
				ret = false;
			}
			if (!seen_testuser) {
				struct policy_handle user_handle;
				if (NT_STATUS_IS_OK(test_OpenUser_byname(p, mem_ctx, handle, TEST_ACCOUNT_NAME, &user_handle))) {
					printf("Didn't find test user " TEST_ACCOUNT_NAME " in enumeration of %s\n", 
					       info->general.domain_name.string);
					ret = false;
					test_samr_handle_Close(p, mem_ctx, &user_handle);
				}
			}
			break;
		case 3:
		case 5:
			if (info->general.num_groups != r.in.start_idx) {
				printf("QueryDomainInfo indicates that QueryDisplayInfo didn't return all (%d/%d) the groups in %s\n",
				       r.in.start_idx, info->general.num_groups,
				       info->general.domain_name.string);
				ret = false;
			}
			
			break;
		}

	}
	
	return ret;	
}

static bool test_QueryDisplayInfo2(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDisplayInfo2 r;
	bool ret = true;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	int i;
	uint32_t total_size;
	uint32_t returned_size;
	union samr_DispInfo info;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryDisplayInfo2 level %u\n", levels[i]);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.in.start_idx = 0;
		r.in.max_entries = 1000;
		r.in.buf_size = (uint32_t)-1;
		r.out.total_size = &total_size;
		r.out.returned_size = &returned_size;
		r.out.info = &info;

		status = dcerpc_samr_QueryDisplayInfo2(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDisplayInfo2 level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}
	
	return ret;	
}

static bool test_QueryDisplayInfo3(struct dcerpc_pipe *p, struct torture_context *tctx,
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDisplayInfo3 r;
	bool ret = true;
	uint16_t levels[] = {1, 2, 3, 4, 5};
	int i;
	uint32_t total_size;
	uint32_t returned_size;
	union samr_DispInfo info;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		torture_comment(tctx, "Testing QueryDisplayInfo3 level %u\n", levels[i]);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.in.start_idx = 0;
		r.in.max_entries = 1000;
		r.in.buf_size = (uint32_t)-1;
		r.out.total_size = &total_size;
		r.out.returned_size = &returned_size;
		r.out.info = &info;

		status = dcerpc_samr_QueryDisplayInfo3(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDisplayInfo3 level %u failed - %s\n", 
			       levels[i], nt_errstr(status));
			ret = false;
		}
	}
	
	return ret;	
}


static bool test_QueryDisplayInfo_continue(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
					   struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDisplayInfo r;
	bool ret = true;
	uint32_t total_size;
	uint32_t returned_size;
	union samr_DispInfo info;

	printf("Testing QueryDisplayInfo continuation\n");

	r.in.domain_handle = handle;
	r.in.level = 1;
	r.in.start_idx = 0;
	r.in.max_entries = 1;
	r.in.buf_size = (uint32_t)-1;
	r.out.total_size = &total_size;
	r.out.returned_size = &returned_size;
	r.out.info = &info;

	do {
		status = dcerpc_samr_QueryDisplayInfo(p, mem_ctx, &r);
		if (NT_STATUS_IS_OK(status) && *r.out.returned_size != 0) {
			if (r.out.info->info1.entries[0].idx != r.in.start_idx + 1) {
				printf("expected idx %d but got %d\n",
				       r.in.start_idx + 1,
				       r.out.info->info1.entries[0].idx);
				break;
			}
		}
		if (!NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES) &&
		    !NT_STATUS_IS_OK(status)) {
			printf("QueryDisplayInfo level %u failed - %s\n", 
			       r.in.level, nt_errstr(status));
			ret = false;
			break;
		}
		r.in.start_idx++;
	} while ((NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES) ||
		  NT_STATUS_IS_OK(status)) &&
		 *r.out.returned_size != 0);
	
	return ret;	
}

static bool test_QueryDomainInfo(struct dcerpc_pipe *p, struct torture_context *tctx,
				 struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDomainInfo r;
	union samr_DomainInfo *info = NULL;
	struct samr_SetDomainInfo s;
	uint16_t levels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13};
	uint16_t set_ok[] = {1, 0, 1, 1, 0, 1, 1, 0, 1,  0,  1,  0};
	int i;
	bool ret = true;
	const char *domain_comment = talloc_asprintf(tctx, 
				  "Tortured by Samba4 RPC-SAMR: %s", 
				  timestring(tctx, time(NULL)));

	s.in.domain_handle = handle;
	s.in.level = 4;
	s.in.info = talloc(tctx, union samr_DomainInfo);
	
	s.in.info->oem.oem_information.string = domain_comment;
	status = dcerpc_samr_SetDomainInfo(p, tctx, &s);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SetDomainInfo level %u (set comment) failed - %s\n", 
		       r.in.level, nt_errstr(status));
		return false;
	}

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		torture_comment(tctx, "Testing QueryDomainInfo level %u\n", levels[i]);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryDomainInfo(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDomainInfo level %u failed - %s\n", 
			       r.in.level, nt_errstr(status));
			ret = false;
			continue;
		}

		switch (levels[i]) {
		case 2:
			if (strcmp(info->general.oem_information.string, domain_comment) != 0) {
				printf("QueryDomainInfo level %u returned different oem_information (comment) (%s, expected %s)\n",
				       levels[i], info->general.oem_information.string, domain_comment);
				ret = false;
			}
			if (!info->general.primary.string) {
				printf("QueryDomainInfo level %u returned no PDC name\n",
				       levels[i]);
				ret = false;
			} else if (info->general.role == SAMR_ROLE_DOMAIN_PDC) {
				if (dcerpc_server_name(p) && strcasecmp_m(dcerpc_server_name(p), info->general.primary.string) != 0) {
					printf("QueryDomainInfo level %u returned different PDC name (%s) compared to server name (%s), despite claiming to be the PDC\n",
					       levels[i], info->general.primary.string, dcerpc_server_name(p));
				}
			}
			break;
		case 4:
			if (strcmp(info->oem.oem_information.string, domain_comment) != 0) {
				printf("QueryDomainInfo level %u returned different oem_information (comment) (%s, expected %s)\n",
				       levels[i], info->oem.oem_information.string, domain_comment);
				ret = false;
			}
			break;
		case 6:
			if (!info->info6.primary.string) {
				printf("QueryDomainInfo level %u returned no PDC name\n",
				       levels[i]);
				ret = false;
			}
			break;
		case 11:
			if (strcmp(info->general2.general.oem_information.string, domain_comment) != 0) {
				printf("QueryDomainInfo level %u returned different comment (%s, expected %s)\n",
				       levels[i], info->general2.general.oem_information.string, domain_comment);
				ret = false;
			}
			break;
		}

		torture_comment(tctx, "Testing SetDomainInfo level %u\n", levels[i]);

		s.in.domain_handle = handle;
		s.in.level = levels[i];
		s.in.info = info;

		status = dcerpc_samr_SetDomainInfo(p, tctx, &s);
		if (set_ok[i]) {
			if (!NT_STATUS_IS_OK(status)) {
				printf("SetDomainInfo level %u failed - %s\n", 
				       r.in.level, nt_errstr(status));
				ret = false;
				continue;
			}
		} else {
			if (!NT_STATUS_EQUAL(NT_STATUS_INVALID_INFO_CLASS, status)) {
				printf("SetDomainInfo level %u gave %s - should have been NT_STATUS_INVALID_INFO_CLASS\n", 
				       r.in.level, nt_errstr(status));
				ret = false;
				continue;
			}
		}

		status = dcerpc_samr_QueryDomainInfo(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDomainInfo level %u failed - %s\n", 
			       r.in.level, nt_errstr(status));
			ret = false;
			continue;
		}
	}

	return ret;	
}


static bool test_QueryDomainInfo2(struct dcerpc_pipe *p, struct torture_context *tctx,
				  struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_QueryDomainInfo2 r;
	union samr_DomainInfo *info = NULL;
	uint16_t levels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13};
	int i;
	bool ret = true;

	for (i=0;i<ARRAY_SIZE(levels);i++) {
		printf("Testing QueryDomainInfo2 level %u\n", levels[i]);

		r.in.domain_handle = handle;
		r.in.level = levels[i];
		r.out.info = &info;

		status = dcerpc_samr_QueryDomainInfo2(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("QueryDomainInfo2 level %u failed - %s\n", 
			       r.in.level, nt_errstr(status));
			ret = false;
			continue;
		}
	}

	return true;	
}

/* Test whether querydispinfo level 5 and enumdomgroups return the same
   set of group names. */
static bool test_GroupList(struct dcerpc_pipe *p, struct torture_context *tctx,
			   struct policy_handle *handle)
{
	struct samr_EnumDomainGroups q1;
	struct samr_QueryDisplayInfo q2;
	NTSTATUS status;
	uint32_t resume_handle=0;
	struct samr_SamArray *sam = NULL;
	uint32_t num_entries = 0;
	int i;
	bool ret = true;
	uint32_t total_size;
	uint32_t returned_size;
	union samr_DispInfo info;

	int num_names = 0;
	const char **names = NULL;

	torture_comment(tctx, "Testing coherency of querydispinfo vs enumdomgroups\n");

	q1.in.domain_handle = handle;
	q1.in.resume_handle = &resume_handle;
	q1.in.max_size = 5;
	q1.out.resume_handle = &resume_handle;
	q1.out.num_entries = &num_entries;
	q1.out.sam = &sam;

	status = STATUS_MORE_ENTRIES;
	while (NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES)) {
		status = dcerpc_samr_EnumDomainGroups(p, tctx, &q1);

		if (!NT_STATUS_IS_OK(status) &&
		    !NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES))
			break;

		for (i=0; i<*q1.out.num_entries; i++) {
			add_string_to_array(tctx,
					    sam->entries[i].name.string,
					    &names, &num_names);
		}
	}

	torture_assert_ntstatus_ok(tctx, status, "EnumDomainGroups");
	
	torture_assert(tctx, sam, "EnumDomainGroups failed to return sam");

	q2.in.domain_handle = handle;
	q2.in.level = 5;
	q2.in.start_idx = 0;
	q2.in.max_entries = 5;
	q2.in.buf_size = (uint32_t)-1;
	q2.out.total_size = &total_size;
	q2.out.returned_size = &returned_size;
	q2.out.info = &info;

	status = STATUS_MORE_ENTRIES;
	while (NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES)) {
		status = dcerpc_samr_QueryDisplayInfo(p, tctx, &q2);

		if (!NT_STATUS_IS_OK(status) &&
		    !NT_STATUS_EQUAL(status, STATUS_MORE_ENTRIES))
			break;

		for (i=0; i<q2.out.info->info5.count; i++) {
			int j;
			const char *name = q2.out.info->info5.entries[i].account_name.string;
			bool found = false;
			for (j=0; j<num_names; j++) {
				if (names[j] == NULL)
					continue;
				if (strequal(names[j], name)) {
					names[j] = NULL;
					found = true;
					break;
				}
			}

			if (!found) {
				printf("QueryDisplayInfo gave name [%s] that EnumDomainGroups did not\n",
				       name);
				ret = false;
			}
		}
		q2.in.start_idx += q2.out.info->info5.count;
	}

	if (!NT_STATUS_IS_OK(status)) {
		printf("QueryDisplayInfo level 5 failed - %s\n",
		       nt_errstr(status));
		ret = false;
	}

	for (i=0; i<num_names; i++) {
		if (names[i] != NULL) {
			printf("EnumDomainGroups gave name [%s] that QueryDisplayInfo did not\n",
			       names[i]);
			ret = false;
		}
	}

	return ret;
}

static bool test_DeleteDomainGroup(struct dcerpc_pipe *p, struct torture_context *tctx,
				   struct policy_handle *group_handle)
{
    	struct samr_DeleteDomainGroup d;
	NTSTATUS status;

	torture_comment(tctx, "Testing DeleteDomainGroup\n");

	d.in.group_handle = group_handle;
	d.out.group_handle = group_handle;

	status = dcerpc_samr_DeleteDomainGroup(p, tctx, &d);
	torture_assert_ntstatus_ok(tctx, status, "DeleteDomainGroup");

	return true;
}

static bool test_TestPrivateFunctionsDomain(struct dcerpc_pipe *p, struct torture_context *tctx,
					    struct policy_handle *domain_handle)
{
    	struct samr_TestPrivateFunctionsDomain r;
	NTSTATUS status;
	bool ret = true;

	torture_comment(tctx, "Testing TestPrivateFunctionsDomain\n");

	r.in.domain_handle = domain_handle;

	status = dcerpc_samr_TestPrivateFunctionsDomain(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, NT_STATUS_NOT_IMPLEMENTED, status, "TestPrivateFunctionsDomain");

	return ret;
}

static bool test_RidToSid(struct dcerpc_pipe *p, struct torture_context *tctx,
			  struct dom_sid *domain_sid,
			  struct policy_handle *domain_handle)
{
    	struct samr_RidToSid r;
	NTSTATUS status;
	bool ret = true;
	struct dom_sid *calc_sid, *out_sid;
	int rids[] = { 0, 42, 512, 10200 };
	int i;

	for (i=0;i<ARRAY_SIZE(rids);i++) {
		torture_comment(tctx, "Testing RidToSid\n");
		
		calc_sid = dom_sid_dup(tctx, domain_sid);
		r.in.domain_handle = domain_handle;
		r.in.rid = rids[i];
		r.out.sid = &out_sid;
		
		status = dcerpc_samr_RidToSid(p, tctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("RidToSid for %d failed - %s\n", rids[i], nt_errstr(status));
			ret = false;
		} else {
			calc_sid = dom_sid_add_rid(calc_sid, calc_sid, rids[i]);

			if (!dom_sid_equal(calc_sid, out_sid)) {
				printf("RidToSid for %d failed - got %s, expected %s\n", rids[i], 
				       dom_sid_string(tctx, out_sid),
				       dom_sid_string(tctx, calc_sid));
				ret = false;
			}
		}
	}

	return ret;
}

static bool test_GetBootKeyInformation(struct dcerpc_pipe *p, struct torture_context *tctx,
				       struct policy_handle *domain_handle)
{
	struct samr_GetBootKeyInformation r;
	NTSTATUS status;
	bool ret = true;
	uint32_t unknown = 0;

	torture_comment(tctx, "Testing GetBootKeyInformation\n");

	r.in.domain_handle = domain_handle;
	r.out.unknown = &unknown;

	status = dcerpc_samr_GetBootKeyInformation(p, tctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		/* w2k3 seems to fail this sometimes and pass it sometimes */
		torture_comment(tctx, "GetBootKeyInformation (ignored) - %s\n", nt_errstr(status));
	}

	return ret;
}

static bool test_AddGroupMember(struct dcerpc_pipe *p, struct torture_context *tctx, 
				struct policy_handle *domain_handle,
				struct policy_handle *group_handle)
{
	NTSTATUS status;
	struct samr_AddGroupMember r;
	struct samr_DeleteGroupMember d;
	struct samr_QueryGroupMember q;
	struct samr_RidTypeArray *rids = NULL;
	struct samr_SetMemberAttributesOfGroup s;
	uint32_t rid;

	status = test_LookupName(p, tctx, domain_handle, TEST_ACCOUNT_NAME, &rid);
	torture_assert_ntstatus_ok(tctx, status, "test_AddGroupMember looking up name " TEST_ACCOUNT_NAME);

	r.in.group_handle = group_handle;
	r.in.rid = rid;
	r.in.flags = 0; /* ??? */

	torture_comment(tctx, "Testing AddGroupMember and DeleteGroupMember\n");

	d.in.group_handle = group_handle;
	d.in.rid = rid;

	status = dcerpc_samr_DeleteGroupMember(p, tctx, &d);
	torture_assert_ntstatus_equal(tctx, NT_STATUS_MEMBER_NOT_IN_GROUP, status, "DeleteGroupMember");

	status = dcerpc_samr_AddGroupMember(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "AddGroupMember");

	status = dcerpc_samr_AddGroupMember(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, NT_STATUS_MEMBER_IN_GROUP, status, "AddGroupMember");

	if (torture_setting_bool(tctx, "samba4", false)) {
		torture_comment(tctx, "skipping SetMemberAttributesOfGroup test against Samba4\n");
	} else {
		/* this one is quite strange. I am using random inputs in the
		   hope of triggering an error that might give us a clue */

		s.in.group_handle = group_handle;
		s.in.unknown1 = random();
		s.in.unknown2 = random();

		status = dcerpc_samr_SetMemberAttributesOfGroup(p, tctx, &s);
		torture_assert_ntstatus_ok(tctx, status, "SetMemberAttributesOfGroup");
	}

	q.in.group_handle = group_handle;
	q.out.rids = &rids;

	status = dcerpc_samr_QueryGroupMember(p, tctx, &q);
	torture_assert_ntstatus_ok(tctx, status, "QueryGroupMember");

	status = dcerpc_samr_DeleteGroupMember(p, tctx, &d);
	torture_assert_ntstatus_ok(tctx, status, "DeleteGroupMember");

	status = dcerpc_samr_AddGroupMember(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "AddGroupMember");

	return true;
}


static bool test_CreateDomainGroup(struct dcerpc_pipe *p, 
								   struct torture_context *tctx, 
				   struct policy_handle *domain_handle, 
				   struct policy_handle *group_handle,
				   struct dom_sid *domain_sid)
{
	NTSTATUS status;
	struct samr_CreateDomainGroup r;
	uint32_t rid;
	struct lsa_String name;
	bool ret = true;

	init_lsa_String(&name, TEST_GROUPNAME);

	r.in.domain_handle = domain_handle;
	r.in.name = &name;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.out.group_handle = group_handle;
	r.out.rid = &rid;

	printf("Testing CreateDomainGroup(%s)\n", r.in.name->string);

	status = dcerpc_samr_CreateDomainGroup(p, tctx, &r);

	if (dom_sid_equal(domain_sid, dom_sid_parse_talloc(tctx, SID_BUILTIN))) {
		if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED)) {
			torture_comment(tctx, "Server correctly refused create of '%s'\n", r.in.name->string);
			return true;
		} else {
			printf("Server should have refused create of '%s', got %s instead\n", r.in.name->string, 
			       nt_errstr(status));
			return false;
		}
	}

	if (NT_STATUS_EQUAL(status, NT_STATUS_GROUP_EXISTS)) {
		if (!test_DeleteGroup_byname(p, tctx, domain_handle, r.in.name->string)) {
			printf("CreateDomainGroup failed: Could not delete domain group %s - %s\n", r.in.name->string, 
			       nt_errstr(status));
			return false;
		}
		status = dcerpc_samr_CreateDomainGroup(p, tctx, &r);
	}
	if (NT_STATUS_EQUAL(status, NT_STATUS_USER_EXISTS)) {
		if (!test_DeleteUser_byname(p, tctx, domain_handle, r.in.name->string)) {
			
			printf("CreateDomainGroup failed: Could not delete user %s - %s\n", r.in.name->string, 
			       nt_errstr(status));
			return false;
		}
		status = dcerpc_samr_CreateDomainGroup(p, tctx, &r);
	}
	torture_assert_ntstatus_ok(tctx, status, "CreateDomainGroup");

	if (!test_AddGroupMember(p, tctx, domain_handle, group_handle)) {
		printf("CreateDomainGroup failed - %s\n", nt_errstr(status));
		ret = false;
	}

	if (!test_SetGroupInfo(p, tctx, group_handle)) {
		ret = false;
	}

	return ret;
}


/*
  its not totally clear what this does. It seems to accept any sid you like.
*/
static bool test_RemoveMemberFromForeignDomain(struct dcerpc_pipe *p, 
					       struct torture_context *tctx,
					       struct policy_handle *domain_handle)
{
	NTSTATUS status;
	struct samr_RemoveMemberFromForeignDomain r;

	r.in.domain_handle = domain_handle;
	r.in.sid = dom_sid_parse_talloc(tctx, "S-1-5-32-12-34-56-78");

	status = dcerpc_samr_RemoveMemberFromForeignDomain(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "RemoveMemberFromForeignDomain");

	return true;
}



static bool test_Connect(struct dcerpc_pipe *p, struct torture_context *tctx,
			 struct policy_handle *handle);

static bool test_OpenDomain(struct dcerpc_pipe *p, struct torture_context *tctx, 
			    struct policy_handle *handle, struct dom_sid *sid,
			    enum torture_samr_choice which_ops,
			    struct cli_credentials *machine_credentials)
{
	NTSTATUS status;
	struct samr_OpenDomain r;
	struct policy_handle domain_handle;
	struct policy_handle alias_handle;
	struct policy_handle user_handle;
	struct policy_handle group_handle;
	bool ret = true;

	ZERO_STRUCT(alias_handle);
	ZERO_STRUCT(user_handle);
	ZERO_STRUCT(group_handle);
	ZERO_STRUCT(domain_handle);

	torture_comment(tctx, "Testing OpenDomain of %s\n", dom_sid_string(tctx, sid));

	r.in.connect_handle = handle;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.in.sid = sid;
	r.out.domain_handle = &domain_handle;

	status = dcerpc_samr_OpenDomain(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "OpenDomain");

	/* run the domain tests with the main handle closed - this tests
	   the servers reference counting */
	ret &= test_samr_handle_Close(p, tctx, handle);

	switch (which_ops) {
	case TORTURE_SAMR_USER_ATTRIBUTES:
	case TORTURE_SAMR_PASSWORDS:
		ret &= test_CreateUser2(p, tctx, &domain_handle, sid, which_ops, NULL);
		ret &= test_CreateUser(p, tctx, &domain_handle, &user_handle, sid, which_ops, NULL);
		/* This test needs 'complex' users to validate */
		ret &= test_QueryDisplayInfo(p, tctx, &domain_handle);
		if (!ret) {
			printf("Testing PASSWORDS or ATTRIBUTES on domain %s failed!\n", dom_sid_string(tctx, sid));
		}
		break;
	case TORTURE_SAMR_PASSWORDS_PWDLASTSET:
		if (!torture_setting_bool(tctx, "samba3", false)) {
			ret &= test_CreateUser2(p, tctx, &domain_handle, sid, which_ops, machine_credentials);
		}
		ret &= test_CreateUser(p, tctx, &domain_handle, &user_handle, sid, which_ops, machine_credentials);
		if (!ret) {
			printf("Testing PASSWORDS PWDLASTSET on domain %s failed!\n", dom_sid_string(tctx, sid));
		}
		break;
	case TORTURE_SAMR_OTHER:
		ret &= test_CreateUser(p, tctx, &domain_handle, &user_handle, sid, which_ops, NULL);
		if (!ret) {
			printf("Failed to CreateUser in SAMR-OTHER on domain %s!\n", dom_sid_string(tctx, sid));
		}
		ret &= test_QuerySecurity(p, tctx, &domain_handle);
		ret &= test_RemoveMemberFromForeignDomain(p, tctx, &domain_handle);
		ret &= test_CreateAlias(p, tctx, &domain_handle, &alias_handle, sid);
		ret &= test_CreateDomainGroup(p, tctx, &domain_handle, &group_handle, sid);
		ret &= test_QueryDomainInfo(p, tctx, &domain_handle);
		ret &= test_QueryDomainInfo2(p, tctx, &domain_handle);
		ret &= test_EnumDomainUsers(p, tctx, &domain_handle);
		ret &= test_EnumDomainUsers_async(p, tctx, &domain_handle);
		ret &= test_EnumDomainGroups(p, tctx, &domain_handle);
		ret &= test_EnumDomainAliases(p, tctx, &domain_handle);
		ret &= test_QueryDisplayInfo2(p, tctx, &domain_handle);
		ret &= test_QueryDisplayInfo3(p, tctx, &domain_handle);
		ret &= test_QueryDisplayInfo_continue(p, tctx, &domain_handle);
		
		if (torture_setting_bool(tctx, "samba4", false)) {
			torture_comment(tctx, "skipping GetDisplayEnumerationIndex test against Samba4\n");
		} else {
			ret &= test_GetDisplayEnumerationIndex(p, tctx, &domain_handle);
			ret &= test_GetDisplayEnumerationIndex2(p, tctx, &domain_handle);
		}
		ret &= test_GroupList(p, tctx, &domain_handle);
		ret &= test_TestPrivateFunctionsDomain(p, tctx, &domain_handle);
		ret &= test_RidToSid(p, tctx, sid, &domain_handle);
		ret &= test_GetBootKeyInformation(p, tctx, &domain_handle);
		if (!ret) {
			torture_comment(tctx, "Testing SAMR-OTHER on domain %s failed!\n", dom_sid_string(tctx, sid));
		}
		break;
	}

	if (!policy_handle_empty(&user_handle) &&
	    !test_DeleteUser(p, tctx, &user_handle)) {
		ret = false;
	}

	if (!policy_handle_empty(&alias_handle) &&
	    !test_DeleteAlias(p, tctx, &alias_handle)) {
		ret = false;
	}

	if (!policy_handle_empty(&group_handle) &&
	    !test_DeleteDomainGroup(p, tctx, &group_handle)) {
		ret = false;
	}

	ret &= test_samr_handle_Close(p, tctx, &domain_handle);

	/* reconnect the main handle */
	ret &= test_Connect(p, tctx, handle);

	if (!ret) {
		printf("Testing domain %s failed!\n", dom_sid_string(tctx, sid));
	}

	return ret;
}

static bool test_LookupDomain(struct dcerpc_pipe *p, struct torture_context *tctx,
			      struct policy_handle *handle, const char *domain,
			      enum torture_samr_choice which_ops,
			      struct cli_credentials *machine_credentials)
{
	NTSTATUS status;
	struct samr_LookupDomain r;
	struct dom_sid2 *sid = NULL;
	struct lsa_String n1;
	struct lsa_String n2;
	bool ret = true;

	torture_comment(tctx, "Testing LookupDomain(%s)\n", domain);

	/* check for correct error codes */
	r.in.connect_handle = handle;
	r.in.domain_name = &n2;
	r.out.sid = &sid;
	n2.string = NULL;

	status = dcerpc_samr_LookupDomain(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, NT_STATUS_INVALID_PARAMETER, status, "LookupDomain expected NT_STATUS_INVALID_PARAMETER");

	init_lsa_String(&n2, "xxNODOMAINxx");

	status = dcerpc_samr_LookupDomain(p, tctx, &r);
	torture_assert_ntstatus_equal(tctx, NT_STATUS_NO_SUCH_DOMAIN, status, "LookupDomain expected NT_STATUS_NO_SUCH_DOMAIN");

	r.in.connect_handle = handle;

	init_lsa_String(&n1, domain);
	r.in.domain_name = &n1;

	status = dcerpc_samr_LookupDomain(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "LookupDomain");

	if (!test_GetDomPwInfo(p, tctx, &n1)) {
		ret = false;
	}

	if (!test_OpenDomain(p, tctx, handle, *r.out.sid, which_ops,
			     machine_credentials)) {
		ret = false;
	}

	return ret;
}


static bool test_EnumDomains(struct dcerpc_pipe *p, struct torture_context *tctx,
			     struct policy_handle *handle, enum torture_samr_choice which_ops,
			     struct cli_credentials *machine_credentials)
{
	NTSTATUS status;
	struct samr_EnumDomains r;
	uint32_t resume_handle = 0;
	uint32_t num_entries = 0;
	struct samr_SamArray *sam = NULL;
	int i;
	bool ret = true;

	r.in.connect_handle = handle;
	r.in.resume_handle = &resume_handle;
	r.in.buf_size = (uint32_t)-1;
	r.out.resume_handle = &resume_handle;
	r.out.num_entries = &num_entries;
	r.out.sam = &sam;

	status = dcerpc_samr_EnumDomains(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "EnumDomains");

	if (!*r.out.sam) {
		return false;
	}

	for (i=0;i<sam->count;i++) {
		if (!test_LookupDomain(p, tctx, handle, 
				       sam->entries[i].name.string, which_ops,
				       machine_credentials)) {
			ret = false;
		}
	}

	status = dcerpc_samr_EnumDomains(p, tctx, &r);
	torture_assert_ntstatus_ok(tctx, status, "EnumDomains");

	return ret;
}


static bool test_Connect(struct dcerpc_pipe *p, struct torture_context *tctx,
			 struct policy_handle *handle)
{
	NTSTATUS status;
	struct samr_Connect r;
	struct samr_Connect2 r2;
	struct samr_Connect3 r3;
	struct samr_Connect4 r4;
	struct samr_Connect5 r5;
	union samr_ConnectInfo info;
	struct policy_handle h;
	uint32_t level_out = 0;
	bool ret = true, got_handle = false;

	torture_comment(tctx, "testing samr_Connect\n");

	r.in.system_name = 0;
	r.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r.out.connect_handle = &h;

	status = dcerpc_samr_Connect(p, tctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		torture_comment(tctx, "Connect failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		got_handle = true;
		*handle = h;
	}

	torture_comment(tctx, "testing samr_Connect2\n");

	r2.in.system_name = NULL;
	r2.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r2.out.connect_handle = &h;

	status = dcerpc_samr_Connect2(p, tctx, &r2);
	if (!NT_STATUS_IS_OK(status)) {
		torture_comment(tctx, "Connect2 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		if (got_handle) {
			test_samr_handle_Close(p, tctx, handle);
		}
		got_handle = true;
		*handle = h;
	}

	torture_comment(tctx, "testing samr_Connect3\n");

	r3.in.system_name = NULL;
	r3.in.unknown = 0;
	r3.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r3.out.connect_handle = &h;

	status = dcerpc_samr_Connect3(p, tctx, &r3);
	if (!NT_STATUS_IS_OK(status)) {
		printf("Connect3 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		if (got_handle) {
			test_samr_handle_Close(p, tctx, handle);
		}
		got_handle = true;
		*handle = h;
	}

	torture_comment(tctx, "testing samr_Connect4\n");

	r4.in.system_name = "";
	r4.in.client_version = 0;
	r4.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r4.out.connect_handle = &h;

	status = dcerpc_samr_Connect4(p, tctx, &r4);
	if (!NT_STATUS_IS_OK(status)) {
		printf("Connect4 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		if (got_handle) {
			test_samr_handle_Close(p, tctx, handle);
		}
		got_handle = true;
		*handle = h;
	}

	torture_comment(tctx, "testing samr_Connect5\n");

	info.info1.client_version = 0;
	info.info1.unknown2 = 0;

	r5.in.system_name = "";
	r5.in.access_mask = SEC_FLAG_MAXIMUM_ALLOWED;
	r5.in.level_in = 1;
	r5.out.level_out = &level_out;
	r5.in.info_in = &info;
	r5.out.info_out = &info;
	r5.out.connect_handle = &h;

	status = dcerpc_samr_Connect5(p, tctx, &r5);
	if (!NT_STATUS_IS_OK(status)) {
		printf("Connect5 failed - %s\n", nt_errstr(status));
		ret = false;
	} else {
		if (got_handle) {
			test_samr_handle_Close(p, tctx, handle);
		}
		got_handle = true;
		*handle = h;
	}

	return ret;
}


bool torture_rpc_samr(struct torture_context *torture)
{
	NTSTATUS status;
	struct dcerpc_pipe *p;
	bool ret = true;
	struct policy_handle handle;

	status = torture_rpc_connection(torture, &p, &ndr_table_samr);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	ret &= test_Connect(p, torture, &handle);

	ret &= test_QuerySecurity(p, torture, &handle);

	ret &= test_EnumDomains(p, torture, &handle, TORTURE_SAMR_OTHER, NULL);

	ret &= test_SetDsrmPassword(p, torture, &handle);

	ret &= test_Shutdown(p, torture, &handle);

	ret &= test_samr_handle_Close(p, torture, &handle);

	return ret;
}


bool torture_rpc_samr_users(struct torture_context *torture)
{
	NTSTATUS status;
	struct dcerpc_pipe *p;
	bool ret = true;
	struct policy_handle handle;

	status = torture_rpc_connection(torture, &p, &ndr_table_samr);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	ret &= test_Connect(p, torture, &handle);

	ret &= test_QuerySecurity(p, torture, &handle);

	ret &= test_EnumDomains(p, torture, &handle, TORTURE_SAMR_USER_ATTRIBUTES, NULL);

	ret &= test_SetDsrmPassword(p, torture, &handle);

	ret &= test_Shutdown(p, torture, &handle);

	ret &= test_samr_handle_Close(p, torture, &handle);

	return ret;
}


bool torture_rpc_samr_passwords(struct torture_context *torture)
{
	NTSTATUS status;
	struct dcerpc_pipe *p;
	bool ret = true;
	struct policy_handle handle;

	status = torture_rpc_connection(torture, &p, &ndr_table_samr);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	ret &= test_Connect(p, torture, &handle);

	ret &= test_EnumDomains(p, torture, &handle, TORTURE_SAMR_PASSWORDS, NULL);

	ret &= test_samr_handle_Close(p, torture, &handle);

	return ret;
}

static bool torture_rpc_samr_pwdlastset(struct torture_context *torture,
					struct dcerpc_pipe *p2,
					struct cli_credentials *machine_credentials)
{
	NTSTATUS status;
	struct dcerpc_pipe *p;
	bool ret = true;
	struct policy_handle handle;

	status = torture_rpc_connection(torture, &p, &ndr_table_samr);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	ret &= test_Connect(p, torture, &handle);

	ret &= test_EnumDomains(p, torture, &handle,
				TORTURE_SAMR_PASSWORDS_PWDLASTSET,
				machine_credentials);

	ret &= test_samr_handle_Close(p, torture, &handle);

	return ret;
}

struct torture_suite *torture_rpc_samr_passwords_pwdlastset(TALLOC_CTX *mem_ctx)
{
	struct torture_suite *suite = torture_suite_create(mem_ctx, "SAMR-PASSWORDS-PWDLASTSET");
	struct torture_rpc_tcase *tcase;

	tcase = torture_suite_add_machine_rpc_iface_tcase(suite, "samr",
							  &ndr_table_samr,
							  TEST_ACCOUNT_NAME_PWD);

	torture_rpc_tcase_add_test_creds(tcase, "pwdLastSet",
					 torture_rpc_samr_pwdlastset);

	return suite;
}
