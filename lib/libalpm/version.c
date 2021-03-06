/*
 *  Copyright (c) 2006-2010 Pacman Development Team <pacman-dev@archlinux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>
#include <ctype.h>

/* libalpm */
#include "util.h"

/** Compare two version strings and determine which one is 'newer'.
 * Returns a value comparable to the way strcmp works. Returns 1
 * if a is newer than b, 0 if a and b are the same version, or -1
 * if b is newer than a.
 *
 * This function has been adopted from the rpmvercmp function located
 * at lib/rpmvercmp.c, and was most recently updated against rpm
 * version 4.4.2.3. Small modifications have been made to make it more
 * consistent with the libalpm coding style.
 *
 * Keep in mind that the pkgrel is only compared if it is available
 * on both versions handed to this function. For example, comparing
 * 1.5-1 and 1.5 will yield 0; comparing 1.5-1 and 1.5-2 will yield
 * -1 as expected. This is mainly for supporting versioned dependencies
 * that do not include the pkgrel.
 */
int SYMEXPORT alpm_pkg_vercmp(const char *a, const char *b)
{
	char oldch1, oldch2;
	char *str1, *str2;
	char *ptr1, *ptr2;
	char *one, *two;
	int rc;
	int isnum;
	int ret = 0;

	/* libalpm added code. ensure our strings are not null */
	if(!a) {
		if(!b) return(0);
		return(-1);
	}
	if(!b) return(1);

	/* easy comparison to see if versions are identical */
	if(strcmp(a, b) == 0) return(0);

	str1 = strdup(a);
	str2 = strdup(b);

	one = str1;
	two = str2;

	/* loop through each version segment of str1 and str2 and compare them */
	while(*one && *two) {
		while(*one && !isalnum((int)*one)) one++;
		while(*two && !isalnum((int)*two)) two++;

		/* If we ran to the end of either, we are finished with the loop */
		if(!(*one && *two)) break;

		ptr1 = one;
		ptr2 = two;

		/* grab first completely alpha or completely numeric segment */
		/* leave one and two pointing to the start of the alpha or numeric */
		/* segment and walk ptr1 and ptr2 to end of segment */
		if(isdigit((int)*ptr1)) {
			while(*ptr1 && isdigit((int)*ptr1)) ptr1++;
			while(*ptr2 && isdigit((int)*ptr2)) ptr2++;
			isnum = 1;
		} else {
			while(*ptr1 && isalpha((int)*ptr1)) ptr1++;
			while(*ptr2 && isalpha((int)*ptr2)) ptr2++;
			isnum = 0;
		}

		/* save character at the end of the alpha or numeric segment */
		/* so that they can be restored after the comparison */
		oldch1 = *ptr1;
		*ptr1 = '\0';
		oldch2 = *ptr2;
		*ptr2 = '\0';

		/* this cannot happen, as we previously tested to make sure that */
		/* the first string has a non-null segment */
		if (one == ptr1) {
			ret = -1;	/* arbitrary */
			goto cleanup;
		}

		/* take care of the case where the two version segments are */
		/* different types: one numeric, the other alpha (i.e. empty) */
		/* numeric segments are always newer than alpha segments */
		/* XXX See patch #60884 (and details) from bugzilla #50977. */
		if (two == ptr2) {
			ret = isnum ? 1 : -1;
			goto cleanup;
		}

		if (isnum) {
			/* this used to be done by converting the digit segments */
			/* to ints using atoi() - it's changed because long  */
			/* digit segments can overflow an int - this should fix that. */

			/* throw away any leading zeros - it's a number, right? */
			while (*one == '0') one++;
			while (*two == '0') two++;

			/* whichever number has more digits wins */
			if (strlen(one) > strlen(two)) {
				ret = 1;
				goto cleanup;
			}
			if (strlen(two) > strlen(one)) {
				ret = -1;
				goto cleanup;
			}
		}

		/* strcmp will return which one is greater - even if the two */
		/* segments are alpha or if they are numeric.  don't return  */
		/* if they are equal because there might be more segments to */
		/* compare */
		rc = strcmp(one, two);
		if (rc) {
			ret = rc < 1 ? -1 : 1;
			goto cleanup;
		}

		/* restore character that was replaced by null above */
		*ptr1 = oldch1;
		one = ptr1;
		*ptr2 = oldch2;
		two = ptr2;

		/* libalpm added code. check if version strings have hit the pkgrel
		 * portion. depending on which strings have hit, take correct action.
		 * this is all based on the premise that we only have one dash in
		 * the version string, and it separates pkgver from pkgrel. */
		if(*ptr1 == '-' && *ptr2 == '-') {
			/* no-op, continue comparing since we are equivalent throughout */
		} else if(*ptr1 == '-') {
			/* ptr1 has hit the pkgrel and ptr2 has not. continue version
			 * comparison after stripping the pkgrel from ptr1. */
			*ptr1 = '\0';
		} else if(*ptr2 == '-') {
			/* ptr2 has hit the pkgrel and ptr1 has not. continue version
			 * comparison after stripping the pkgrel from ptr2. */
			*ptr2 = '\0';
		}
	}

	/* this catches the case where all numeric and alpha segments have */
	/* compared identically but the segment separating characters were */
	/* different */
	if ((!*one) && (!*two)) {
		ret = 0;
		goto cleanup;
	}

	/* the final showdown. we never want a remaining alpha string to
	 * beat an empty string. the logic is a bit weird, but:
	 * - if one is empty and two is not an alpha, two is newer.
	 * - if one is an alpha, two is newer.
	 * - otherwise one is newer.
	 * */
	if ( ( !*one && !isalpha((int)*two) )
			|| isalpha((int)*one) ) {
		ret = -1;
	} else {
		ret = 1;
	}

cleanup:
	free(str1);
	free(str2);
	return(ret);
}

/* vim: set ts=2 sw=2 noet: */
