/* 
   Unix SMB/CIFS implementation.
   Samba utility functions
   Copyright (C) Andrew Tridgell 1992-2001
   Copyright (C) Simo Sorce 2001
   
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
#include "system/locale.h"
#include "dynconfig/dynconfig.h"
#include "param/param.h"

/**
 * @file
 * @brief Unicode string manipulation
 */

/* these 2 tables define the unicode case handling.  They are loaded
   at startup either via mmap() or read() from the lib directory */
static void *upcase_table;
static void *lowcase_table;


/*******************************************************************
load the case handling tables
********************************************************************/
void load_case_tables(void)
{
	TALLOC_CTX *mem_ctx;

	mem_ctx = talloc_init("load_case_tables");
	if (!mem_ctx) {
		smb_panic("No memory for case_tables");
	}
	upcase_table = map_file(talloc_asprintf(mem_ctx, "%s/upcase.dat", dyn_DATADIR), 0x20000);
	lowcase_table = map_file(talloc_asprintf(mem_ctx, "%s/lowcase.dat", dyn_DATADIR), 0x20000);
	talloc_free(mem_ctx);
	if (upcase_table == NULL) {
		/* try also under codepages for testing purposes */
		upcase_table = map_file("codepages/upcase.dat", 0x20000);
		if (upcase_table == NULL) {
			upcase_table = (void *)-1;
		}
	}
	if (lowcase_table == NULL) {
		/* try also under codepages for testing purposes */
		lowcase_table = map_file("codepages/lowcase.dat", 0x20000);
		if (lowcase_table == NULL) {
			lowcase_table = (void *)-1;
		}
	}
}

/**
 Convert a codepoint_t to upper case.
**/
_PUBLIC_ codepoint_t toupper_w(codepoint_t val)
{
	if (val < 128) {
		return toupper(val);
	}
	if (upcase_table == NULL) {
		load_case_tables();
	}
	if (upcase_table == (void *)-1) {
		return val;
	}
	if (val & 0xFFFF0000) {
		return val;
	}
	return SVAL(upcase_table, val*2);
}

/**
 Convert a codepoint_t to lower case.
**/
_PUBLIC_ codepoint_t tolower_w(codepoint_t val)
{
	if (val < 128) {
		return tolower(val);
	}
	if (lowcase_table == NULL) {
		load_case_tables();
	}
	if (lowcase_table == (void *)-1) {
		return val;
	}
	if (val & 0xFFFF0000) {
		return val;
	}
	return SVAL(lowcase_table, val*2);
}

/**
  compare two codepoints case insensitively
*/
_PUBLIC_ int codepoint_cmpi(codepoint_t c1, codepoint_t c2)
{
	if (c1 == c2 ||
	    toupper_w(c1) == toupper_w(c2)) {
		return 0;
	}
	return c1 - c2;
}

/**
 Case insensitive string compararison
**/
_PUBLIC_ int strcasecmp_m(const char *s1, const char *s2)
{
	codepoint_t c1=0, c2=0;
	size_t size1, size2;
	struct smb_iconv_convenience *iconv_convenience = lp_iconv_convenience(global_loadparm);

	/* handle null ptr comparisons to simplify the use in qsort */
	if (s1 == s2) return 0;
	if (s1 == NULL) return -1;
	if (s2 == NULL) return 1;

	while (*s1 && *s2) {
		c1 = next_codepoint(iconv_convenience, s1, &size1);
		c2 = next_codepoint(iconv_convenience, s2, &size2);

		s1 += size1;
		s2 += size2;

		if (c1 == c2) {
			continue;
		}

		if (c1 == INVALID_CODEPOINT ||
		    c2 == INVALID_CODEPOINT) {
			/* what else can we do?? */
			return strcasecmp(s1, s2);
		}

		if (toupper_w(c1) != toupper_w(c2)) {
			return c1 - c2;
		}
	}

	return *s1 - *s2;
}

/**
 * Get the next token from a string, return False if none found.
 * Handles double-quotes.
 * 
 * Based on a routine by GJC@VILLAGE.COM. 
 * Extensively modified by Andrew.Tridgell@anu.edu.au
 **/
_PUBLIC_ bool next_token(const char **ptr,char *buff, const char *sep, size_t bufsize)
{
	const char *s;
	bool quoted;
	size_t len=1;

	if (!ptr)
		return false;

	s = *ptr;

	/* default to simple separators */
	if (!sep)
		sep = " \t\n\r";

	/* find the first non sep char */
	while (*s && strchr_m(sep,*s))
		s++;
	
	/* nothing left? */
	if (!*s)
		return false;
	
	/* copy over the token */
	for (quoted = false; len < bufsize && *s && (quoted || !strchr_m(sep,*s)); s++) {
		if (*s == '\"') {
			quoted = !quoted;
		} else {
			len++;
			*buff++ = *s;
		}
	}
	
	*ptr = (*s) ? s+1 : s;  
	*buff = 0;
	
	return true;
}

/**
 Case insensitive string compararison, length limited
**/
_PUBLIC_ int strncasecmp_m(const char *s1, const char *s2, size_t n)
{
	codepoint_t c1=0, c2=0;
	size_t size1, size2;
	struct smb_iconv_convenience *iconv_convenience = lp_iconv_convenience(global_loadparm);

	/* handle null ptr comparisons to simplify the use in qsort */
	if (s1 == s2) return 0;
	if (s1 == NULL) return -1;
	if (s2 == NULL) return 1;

	while (*s1 && *s2 && n) {
		n--;

		c1 = next_codepoint(iconv_convenience, s1, &size1);
		c2 = next_codepoint(iconv_convenience, s2, &size2);

		s1 += size1;
		s2 += size2;

		if (c1 == c2) {
			continue;
		}

		if (c1 == INVALID_CODEPOINT ||
		    c2 == INVALID_CODEPOINT) {
			/* what else can we do?? */
			return strcasecmp(s1, s2);
		}

		if (toupper_w(c1) != toupper_w(c2)) {
			return c1 - c2;
		}
	}

	if (n == 0) {
		return 0;
	}

	return *s1 - *s2;
}

/**
 * Compare 2 strings.
 *
 * @note The comparison is case-insensitive.
 **/
_PUBLIC_ bool strequal_w(const char *s1, const char *s2)
{
	return strcasecmp_m(s1,s2) == 0;
}

/**
 Compare 2 strings (case sensitive).
**/
_PUBLIC_ bool strcsequal_w(const char *s1,const char *s2)
{
	if (s1 == s2)
		return true;
	if (!s1 || !s2)
		return false;
	
	return strcmp(s1,s2) == 0;
}


/**
 String replace.
 NOTE: oldc and newc must be 7 bit characters
**/
_PUBLIC_ void string_replace_w(char *s, char oldc, char newc)
{
	while (s && *s) {
		size_t size;
		codepoint_t c = next_codepoint(lp_iconv_convenience(global_loadparm), s, &size);
		if (c == oldc) {
			*s = newc;
		}
		s += size;
	}
}

/**
 Paranoid strcpy into a buffer of given length (includes terminating
 zero. Strips out all but 'a-Z0-9' and the character in other_safe_chars
 and replaces with '_'. Deliberately does *NOT* check for multibyte
 characters. Don't change it !
**/

_PUBLIC_ char *alpha_strcpy(char *dest, const char *src, const char *other_safe_chars, size_t maxlength)
{
	size_t len, i;

	if (maxlength == 0) {
		/* can't fit any bytes at all! */
		return NULL;
	}

	if (!dest) {
		DEBUG(0,("ERROR: NULL dest in alpha_strcpy\n"));
		return NULL;
	}

	if (!src) {
		*dest = 0;
		return dest;
	}  

	len = strlen(src);
	if (len >= maxlength)
		len = maxlength - 1;

	if (!other_safe_chars)
		other_safe_chars = "";

	for(i = 0; i < len; i++) {
		int val = (src[i] & 0xff);
		if (isupper(val) || islower(val) || isdigit(val) || strchr_m(other_safe_chars, val))
			dest[i] = src[i];
		else
			dest[i] = '_';
	}

	dest[i] = '\0';

	return dest;
}

/**
 Count the number of UCS2 characters in a string. Normally this will
 be the same as the number of bytes in a string for single byte strings,
 but will be different for multibyte.
**/
_PUBLIC_ size_t strlen_m(const char *s)
{
	size_t count = 0;

	if (!s) {
		return 0;
	}

	while (*s && !(((uint8_t)*s) & 0x80)) {
		s++;
		count++;
	}

	if (!*s) {
		return count;
	}

	while (*s) {
		size_t c_size;
		codepoint_t c = next_codepoint(lp_iconv_convenience(global_loadparm), s, &c_size);
		if (c < 0x10000) {
			count += 1;
		} else {
			count += 2;
		}
		s += c_size;
	}

	return count;
}

/**
   Work out the number of multibyte chars in a string, including the NULL
   terminator.
**/
_PUBLIC_ size_t strlen_m_term(const char *s)
{
	if (!s) {
		return 0;
	}

	return strlen_m(s) + 1;
}

/**
 Strchr and strrchr_m are a bit complex on general multi-byte strings. 
**/
_PUBLIC_ char *strchr_m(const char *s, char c)
{
	if (s == NULL) {
		return NULL;
	}
	/* characters below 0x3F are guaranteed to not appear in
	   non-initial position in multi-byte charsets */
	if ((c & 0xC0) == 0) {
		return strchr(s, c);
	}

	while (*s) {
		size_t size;
		codepoint_t c2 = next_codepoint(lp_iconv_convenience(global_loadparm), s, &size);
		if (c2 == c) {
			return discard_const_p(char, s);
		}
		s += size;
	}

	return NULL;
}

/**
 * Multibyte-character version of strrchr
 */
_PUBLIC_ char *strrchr_m(const char *s, char c)
{
	char *ret = NULL;

	if (s == NULL) {
		return NULL;
	}

	/* characters below 0x3F are guaranteed to not appear in
	   non-initial position in multi-byte charsets */
	if ((c & 0xC0) == 0) {
		return strrchr(s, c);
	}

	while (*s) {
		size_t size;
		codepoint_t c2 = next_codepoint(lp_iconv_convenience(global_loadparm), s, &size);
		if (c2 == c) {
			ret = discard_const_p(char, s);
		}
		s += size;
	}

	return ret;
}

/**
  return True if any (multi-byte) character is lower case
*/
_PUBLIC_ bool strhaslower(const char *string)
{
	while (*string) {
		size_t c_size;
		codepoint_t s;
		codepoint_t t;

		s = next_codepoint(lp_iconv_convenience(global_loadparm), string, &c_size);
		string += c_size;

		t = toupper_w(s);

		if (s != t) {
			return true; /* that means it has lower case chars */
		}
	}

	return false;
} 

/**
  return True if any (multi-byte) character is upper case
*/
_PUBLIC_ bool strhasupper(const char *string)
{
	while (*string) {
		size_t c_size;
		codepoint_t s;
		codepoint_t t;

		s = next_codepoint(lp_iconv_convenience(global_loadparm), string, &c_size);
		string += c_size;

		t = tolower_w(s);

		if (s != t) {
			return true; /* that means it has upper case chars */
		}
	}

	return false;
} 

/**
 Convert a string to lower case, allocated with talloc
**/
_PUBLIC_ char *strlower_talloc(TALLOC_CTX *ctx, const char *src)
{
	size_t size=0;
	char *dest;
	struct smb_iconv_convenience *iconv_convenience = lp_iconv_convenience(global_loadparm);

	/* this takes advantage of the fact that upper/lower can't
	   change the length of a character by more than 1 byte */
	dest = talloc_array(ctx, char, 2*(strlen(src))+1);
	if (dest == NULL) {
		return NULL;
	}

	while (*src) {
		size_t c_size;
		codepoint_t c = next_codepoint(iconv_convenience, src, &c_size);
		src += c_size;

		c = tolower_w(c);

		c_size = push_codepoint(iconv_convenience, dest+size, c);
		if (c_size == -1) {
			talloc_free(dest);
			return NULL;
		}
		size += c_size;
	}

	dest[size] = 0;

	/* trim it so talloc_append_string() works */
	dest = talloc_realloc(ctx, dest, char, size+1);

	talloc_set_name_const(dest, dest);

	return dest;
}

/**
 Convert a string to UPPER case, allocated with talloc
 source length limited to n bytes
**/
_PUBLIC_ char *strupper_talloc_n(TALLOC_CTX *ctx, const char *src, size_t n)
{
	size_t size=0;
	char *dest;
	struct smb_iconv_convenience *iconv_convenience = lp_iconv_convenience(global_loadparm);
	
	if (!src) {
		return NULL;
	}

	/* this takes advantage of the fact that upper/lower can't
	   change the length of a character by more than 1 byte */
	dest = talloc_array(ctx, char, 2*(n+1));
	if (dest == NULL) {
		return NULL;
	}

	while (*src && n--) {
		size_t c_size;
		codepoint_t c = next_codepoint(iconv_convenience, src, &c_size);
		src += c_size;

		c = toupper_w(c);

		c_size = push_codepoint(iconv_convenience, dest+size, c);
		if (c_size == -1) {
			talloc_free(dest);
			return NULL;
		}
		size += c_size;
	}

	dest[size] = 0;

	/* trim it so talloc_append_string() works */
	dest = talloc_realloc(ctx, dest, char, size+1);

	talloc_set_name_const(dest, dest);

	return dest;
}

/**
 Convert a string to UPPER case, allocated with talloc
**/
_PUBLIC_ char *strupper_talloc(TALLOC_CTX *ctx, const char *src)
{
	return strupper_talloc_n(ctx, src, src?strlen(src):0);
}

/**
 talloc_strdup() a unix string to upper case.
**/
_PUBLIC_ char *talloc_strdup_upper(TALLOC_CTX *ctx, const char *src)
{
	return strupper_talloc(ctx, src);
}

/**
 Convert a string to lower case.
**/
_PUBLIC_ void strlower_m(char *s)
{
	char *d;
	struct smb_iconv_convenience *iconv_convenience;

	/* this is quite a common operation, so we want it to be
	   fast. We optimise for the ascii case, knowing that all our
	   supported multi-byte character sets are ascii-compatible
	   (ie. they match for the first 128 chars) */
	while (*s && !(((uint8_t)*s) & 0x80)) {
		*s = tolower((uint8_t)*s);
		s++;
	}

	if (!*s)
		return;

	iconv_convenience = lp_iconv_convenience(global_loadparm);

	d = s;

	while (*s) {
		size_t c_size, c_size2;
		codepoint_t c = next_codepoint(iconv_convenience, s, &c_size);
		c_size2 = push_codepoint(iconv_convenience, d, tolower_w(c));
		if (c_size2 > c_size) {
			DEBUG(0,("FATAL: codepoint 0x%x (0x%x) expanded from %d to %d bytes in strlower_m\n",
				 c, tolower_w(c), (int)c_size, (int)c_size2));
			smb_panic("codepoint expansion in strlower_m\n");
		}
		s += c_size;
		d += c_size2;
	}
	*d = 0;
}

/**
 Convert a string to UPPER case.
**/
_PUBLIC_ void strupper_m(char *s)
{
	char *d;
	struct smb_iconv_convenience *iconv_convenience;

	/* this is quite a common operation, so we want it to be
	   fast. We optimise for the ascii case, knowing that all our
	   supported multi-byte character sets are ascii-compatible
	   (ie. they match for the first 128 chars) */
	while (*s && !(((uint8_t)*s) & 0x80)) {
		*s = toupper((uint8_t)*s);
		s++;
	}

	if (!*s)
		return;

	iconv_convenience = lp_iconv_convenience(global_loadparm);

	d = s;

	while (*s) {
		size_t c_size, c_size2;
		codepoint_t c = next_codepoint(iconv_convenience, s, &c_size);
		c_size2 = push_codepoint(iconv_convenience, d, toupper_w(c));
		if (c_size2 > c_size) {
			DEBUG(0,("FATAL: codepoint 0x%x (0x%x) expanded from %d to %d bytes in strupper_m\n",
				 c, toupper_w(c), (int)c_size, (int)c_size2));
			smb_panic("codepoint expansion in strupper_m\n");
		}
		s += c_size;
		d += c_size2;
	}
	*d = 0;
}


/**
 Find the number of 'c' chars in a string
**/
_PUBLIC_ size_t count_chars_w(const char *s, char c)
{
	size_t count = 0;

	while (*s) {
		size_t size;
		codepoint_t c2 = next_codepoint(lp_iconv_convenience(global_loadparm), s, &size);
		if (c2 == c) count++;
		s += size;
	}

	return count;
}

