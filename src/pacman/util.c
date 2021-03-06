/*
 *  util.c
 *
 *  Copyright (c) 2006-2010 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <wchar.h>

#include <alpm.h>
#include <alpm_list.h>

/* pacman */
#include "util.h"
#include "conf.h"
#include "callback.h"


int trans_init(pmtransflag_t flags)
{
	int ret;
	if(config->print) {
		ret = alpm_trans_init(flags, NULL, NULL, NULL);
	} else {
		ret = alpm_trans_init(flags, cb_trans_evt, cb_trans_conv,
				cb_trans_progress);
	}

	if(ret == -1) {
		pm_fprintf(stderr, PM_LOG_ERROR, _("failed to init transaction (%s)\n"),
				alpm_strerrorlast());
		if(pm_errno == PM_ERR_HANDLE_LOCK) {
			fprintf(stderr, _("  if you're sure a package manager is not already\n"
						"  running, you can remove %s\n"), alpm_option_get_lockfile());
		}
		return(-1);
	}
	return(0);
}

int trans_release(void)
{
	if(alpm_trans_release() == -1) {
		pm_fprintf(stderr, PM_LOG_ERROR, _("failed to release transaction (%s)\n"),
				alpm_strerrorlast());
		return(-1);
	}
	return(0);
}

int needs_root(void)
{
	switch(config->op) {
		case PM_OP_DATABASE:
			return(1);
		case PM_OP_UPGRADE:
		case PM_OP_REMOVE:
			return(!config->print);
		case PM_OP_SYNC:
			return(config->op_s_clean || config->op_s_sync ||
					(!config->group && !config->op_s_info && !config->op_q_list &&
					 !config->op_s_search && !config->print));
		default:
			return(0);
	}
}

/* gets the current screen column width */
int getcols(void)
{
#ifdef TIOCGSIZE
	struct ttysize win;
	if(ioctl(1, TIOCGSIZE, &win) == 0) {
		return win.ts_cols;
	}
#elif defined(TIOCGWINSZ)
	struct winsize win;
	if(ioctl(1, TIOCGWINSZ, &win) == 0) {
		return win.ws_col;
	}
#endif
	return 0;
}

/* does the same thing as 'rm -rf' */
int rmrf(const char *path)
{
	int errflag = 0;
	struct dirent *dp;
	DIR *dirp;

	if(!unlink(path)) {
		return(0);
	} else {
		if(errno == ENOENT) {
			return(0);
		} else if(errno == EPERM) {
			/* fallthrough */
		} else if(errno == EISDIR) {
			/* fallthrough */
		} else if(errno == ENOTDIR) {
			return(1);
		} else {
			/* not a directory */
			return(1);
		}

		dirp = opendir(path);
		if(!dirp) {
			return(1);
		}
		for(dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
			if(dp->d_ino) {
				char name[PATH_MAX];
				sprintf(name, "%s/%s", path, dp->d_name);
				if(strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0) {
					errflag += rmrf(name);
				}
			}
		}
		closedir(dirp);
		if(rmdir(path)) {
			errflag++;
		}
		return(errflag);
	}
}

/** Parse the basename of a program from a path.
* Grabbed from the uClibc source.
* @param path path to parse basename from
*
* @return everything following the final '/'
*/
char *mbasename(const char *path)
{
	const char *s;
	const char *p;

	p = s = path;

	while (*s) {
		if (*s++ == '/') {
			p = s;
		}
	}

	return (char *)p;
}

/** Parse the dirname of a program from a path.
* The path returned should be freed.
* @param path path to parse dirname from
*
* @return everything preceding the final '/'
*/
char *mdirname(const char *path)
{
	char *ret, *last;

	/* null or empty path */
	if(path == NULL || path == '\0') {
		return(strdup("."));
	}

	ret = strdup(path);
	last = strrchr(ret, '/');

	if(last != NULL) {
		/* we found a '/', so terminate our string */
		*last = '\0';
		return(ret);
	}
	/* no slash found */
	free(ret);
	return(strdup("."));
}

/* output a string, but wrap words properly with a specified indentation
 */
void indentprint(const char *str, int indent)
{
	wchar_t *wcstr;
	const wchar_t *p;
	int len, cidx, cols;

	if(!str) {
		return;
	}

	cols = getcols();

	/* if we're not a tty, print without indenting */
	if(cols == 0) {
		printf("%s", str);
		return;
	}

	len = strlen(str) + 1;
	wcstr = calloc(len, sizeof(wchar_t));
	len = mbstowcs(wcstr, str, len);
	p = wcstr;
	cidx = indent;

	if(!p || !len) {
		return;
	}

	while(*p) {
		if(*p == L' ') {
			const wchar_t *q, *next;
			p++;
			if(p == NULL || *p == L' ') continue;
			next = wcschr(p, L' ');
			if(next == NULL) {
				next = p + wcslen(p);
			}
			/* len captures # cols */
			len = 0;
			q = p;
			while(q < next) {
				len += wcwidth(*q++);
			}
			if(len > (cols - cidx - 1)) {
				/* wrap to a newline and reindent */
				printf("\n%-*s", indent, "");
				cidx = indent;
			} else {
				printf(" ");
				cidx++;
			}
			continue;
		}
		printf("%lc", (wint_t)*p);
		cidx += wcwidth(*p);
		p++;
	}
	free(wcstr);
}

/* Convert a string to uppercase
 */
char *strtoupper(char *str)
{
	char *ptr = str;

	while(*ptr) {
		(*ptr) = toupper((unsigned char)*ptr);
		ptr++;
	}
	return str;
}

/* Trim whitespace and newlines from a string
 */
char *strtrim(char *str)
{
	char *pch = str;

	if(str == NULL || *str == '\0') {
		/* string is empty, so we're done. */
		return(str);
	}

	while(isspace((unsigned char)*pch)) {
		pch++;
	}
	if(pch != str) {
		memmove(str, pch, (strlen(pch) + 1));
	}

	/* check if there wasn't anything but whitespace in the string. */
	if(*str == '\0') {
		return(str);
	}

	pch = (str + (strlen(str) - 1));
	while(isspace((unsigned char)*pch)) {
		pch--;
	}
	*++pch = '\0';

	return(str);
}

/* Replace all occurances of 'needle' with 'replace' in 'str', returning
 * a new string (must be free'd) */
char *strreplace(const char *str, const char *needle, const char *replace)
{
	const char *p = NULL, *q = NULL;
	char *newstr = NULL, *newp = NULL;
	alpm_list_t *i = NULL, *list = NULL;
	size_t needlesz = strlen(needle), replacesz = strlen(replace);
	size_t newsz;

	if(!str) {
		return(NULL);
	}

	p = str;
	q = strstr(p, needle);
	while(q) {
		list = alpm_list_add(list, (char *)q);
		p = q + needlesz;
		q = strstr(p, needle);
	}

	/* no occurences of needle found */
	if(!list) {
		return(strdup(str));
	}
	/* size of new string = size of old string + "number of occurences of needle"
	 * x "size difference between replace and needle" */
	newsz = strlen(str) + 1 +
		alpm_list_count(list) * (replacesz - needlesz);
	newstr = malloc(newsz);
	if(!newstr) {
		return(NULL);
	}
	*newstr = '\0';

	p = str;
	newp = newstr;
	for(i = list; i; i = alpm_list_next(i)) {
		q = alpm_list_getdata(i);
		if(q > p){
			/* add chars between this occurence and last occurence, if any */
			strncpy(newp, p, q - p);
			newp += q - p;
		}
		strncpy(newp, replace, replacesz);
		newp += replacesz;
		p = q + needlesz;
	}
	alpm_list_free(list);

	if(*p) {
		/* add the rest of 'p' */
		strcpy(newp, p);
		newp += strlen(p);
	}
	*newp = '\0';

	return(newstr);
}

/** Splits a string into a list of strings using the chosen character as
 * a delimiter.
 *
 * @param str the string to split
 * @param splitchar the character to split at
 *
 * @return a list containing the duplicated strings
 */
alpm_list_t *strsplit(const char *str, const char splitchar)
{
	alpm_list_t *list = NULL;
	const char *prev = str;
	char *dup = NULL;

	while((str = strchr(str, splitchar))) {
		dup = strndup(prev, str - prev);
		if(dup == NULL) {
			return(NULL);
		}
		list = alpm_list_add(list, dup);

		str++;
		prev = str;
	}

	dup = strdup(prev);
	if(dup == NULL) {
		return(NULL);
	}
	list = alpm_list_add(list, dup);

	return(list);
}

static int string_length(const char *s)
{
	int len;
	wchar_t *wcstr;

	if(!s) {
		return(0);
	}
	/* len goes from # bytes -> # chars -> # cols */
	len = strlen(s) + 1;
	wcstr = calloc(len, sizeof(wchar_t));
	len = mbstowcs(wcstr, s, len);
	len = wcswidth(wcstr, len);
	free(wcstr);

	return(len);
}

void string_display(const char *title, const char *string)
{
	int len = 0;

	if(title) {
		printf("%s ", title);
	}
	if(string == NULL || string[0] == '\0') {
		printf(_("None"));
	} else {
		/* compute the length of title + a space */
		len = string_length(title) + 1;
		indentprint(string, len);
	}
	printf("\n");
}

void list_display(const char *title, const alpm_list_t *list)
{
	const alpm_list_t *i;
	int cols, len = 0;

	if(title) {
		len = string_length(title) + 1;
		printf("%s ", title);
	}

	if(!list) {
		printf("%s\n", _("None"));
	} else {
		for(i = list, cols = len; i; i = alpm_list_next(i)) {
			char *str = alpm_list_getdata(i);
			int s = string_length(str);
			int maxcols = getcols();
			if(maxcols > 0 && (cols + s + 2) >= maxcols) {
				int j;
				cols = len;
				printf("\n");
				for (j = 1; j <= len; j++) {
					printf(" ");
				}
			} else if (cols != len) {
				/* 2 spaces are added if this is not the first element on a line. */
				printf("  ");
				cols += 2;
			}
			printf("%s", str);
			cols += s;
		}
		printf("\n");
	}
}

void list_display_linebreak(const char *title, const alpm_list_t *list)
{
	const alpm_list_t *i;
	int len = 0;

	if(title) {
		len = string_length(title) + 1;
		printf("%s ", title);
	}

	if(!list) {
		printf("%s\n", _("None"));
	} else {
		/* Print the first element */
		indentprint((const char *) alpm_list_getdata(list), len);
		printf("\n");
		/* Print the rest */
		for(i = alpm_list_next(list); i; i = alpm_list_next(i)) {
			int j;
			for(j = 1; j <= len; j++) {
				printf(" ");
			}
			indentprint((const char *) alpm_list_getdata(i), len);
			printf("\n");
		}
	}
}
/* prepare a list of pkgs to display */
void display_targets(const alpm_list_t *pkgs, int install)
{
	char *str;
	const alpm_list_t *i;
	off_t isize = 0, dlsize = 0;
	double mbisize = 0.0, mbdlsize = 0.0;
	alpm_list_t *targets = NULL;

	if(!pkgs) {
		return;
	}

	printf("\n");
	for(i = pkgs; i; i = alpm_list_next(i)) {
		pmpkg_t *pkg = alpm_list_getdata(i);

		dlsize += alpm_pkg_download_size(pkg);
		isize += alpm_pkg_get_isize(pkg);

		/* print the package size with the output if ShowSize option set */
		if(config->showsize) {
			double mbsize = 0.0;
			mbsize = alpm_pkg_get_size(pkg) / (1024.0 * 1024.0);

			pm_asprintf(&str, "%s-%s [%.2f MB]", alpm_pkg_get_name(pkg),
					alpm_pkg_get_version(pkg), mbsize);
		} else {
			pm_asprintf(&str, "%s-%s", alpm_pkg_get_name(pkg),
					alpm_pkg_get_version(pkg));
		}
		targets = alpm_list_add(targets, str);
	}

	/* Convert byte sizes to MB */
	mbdlsize = dlsize / (1024.0 * 1024.0);
	mbisize = isize / (1024.0 * 1024.0);

	if(install) {
		pm_asprintf(&str, _("Targets (%d):"), alpm_list_count(targets));
		list_display(str, targets);
		free(str);
		printf("\n");

		printf(_("Total Download Size:    %.2f MB\n"), mbdlsize);
		if(!(config->flags & PM_TRANS_FLAG_DOWNLOADONLY)) {
			printf(_("Total Installed Size:   %.2f MB\n"), mbisize);
		}
	} else {
		pm_asprintf(&str, _("Remove (%d):"), alpm_list_count(targets));
		list_display(str, targets);
		free(str);
		printf("\n");

		printf(_("Total Removed Size:   %.2f MB\n"), mbisize);
	}

	FREELIST(targets);
}

static off_t pkg_get_size(pmpkg_t *pkg)
{
	switch(config->op) {
		case PM_OP_SYNC:
			return(alpm_pkg_download_size(pkg));
		case PM_OP_UPGRADE:
			return(alpm_pkg_get_size(pkg));
		default:
			return(alpm_pkg_get_isize(pkg));
	}
}

static char *pkg_get_location(pmpkg_t *pkg)
{
	pmdb_t *db;
	const char *dburl;
	char *string;
	switch(config->op) {
		case PM_OP_SYNC:
			db = alpm_pkg_get_db(pkg);
			dburl = alpm_db_get_url(db);
			if(dburl) {
				char *pkgurl = NULL;
				pm_asprintf(&pkgurl, "%s/%s", dburl, alpm_pkg_get_filename(pkg));
				return(pkgurl);
			}
		case PM_OP_UPGRADE:
			return(strdup(alpm_pkg_get_filename(pkg)));
		default:
			string = NULL;
			pm_asprintf(&string, "%s-%s", alpm_pkg_get_name(pkg), alpm_pkg_get_version(pkg));
			return(string);
	}
}

void print_packages(const alpm_list_t *packages)
{
	const alpm_list_t *i;
	if(!config->print_format) {
		config->print_format = strdup("%l");
	}
	for(i = packages; i; i = alpm_list_next(i)) {
		pmpkg_t *pkg = alpm_list_getdata(i);
		char *string = strdup(config->print_format);
		char *temp = string;
		/* %n : pkgname */
		if(strstr(temp,"%n")) {
			string = strreplace(temp, "%n", alpm_pkg_get_name(pkg));
			free(temp);
			temp = string;
		}
		/* %v : pkgver */
		if(strstr(temp,"%v")) {
			string = strreplace(temp, "%v", alpm_pkg_get_version(pkg));
			free(temp);
			temp = string;
		}
		/* %l : location */
		if(strstr(temp,"%l")) {
			char *pkgloc = pkg_get_location(pkg);
			string = strreplace(temp, "%l", pkgloc);
			free(pkgloc);
			free(temp);
			temp = string;
		}
		/* %r : repo */
		if(strstr(temp,"%r")) {
			const char *repo = "local";
			pmdb_t *db = alpm_pkg_get_db(pkg);
			if(db) {
				repo = alpm_db_get_name(db);
			}
			string = strreplace(temp, "%r", repo);
			free(temp);
			temp = string;
		}
		/* %s : size */
		if(strstr(temp,"%s")) {
			char *size;
			double mbsize = 0.0;
			mbsize = pkg_get_size(pkg) / (1024.0 * 1024.0);
			pm_asprintf(&size, "%.2f", mbsize);
			string = strreplace(temp, "%s", size);
			free(size);
			free(temp);
			temp = string;
		}
		printf("%s\n",string);
		free(string);
	}
}

/* Helper function for comparing strings using the
 * alpm "compare func" signature */
int str_cmp(const void *s1, const void *s2)
{
	return(strcmp(s1, s2));
}

void display_new_optdepends(pmpkg_t *oldpkg, pmpkg_t *newpkg)
{
	alpm_list_t *old = alpm_pkg_get_optdepends(oldpkg);
	alpm_list_t *new = alpm_pkg_get_optdepends(newpkg);
	alpm_list_t *optdeps = alpm_list_diff(new,old,str_cmp);
	if(optdeps) {
		printf(_("New optional dependencies for %s\n"), alpm_pkg_get_name(newpkg));
		list_display_linebreak("   ", optdeps);
	}
	alpm_list_free(optdeps);
}

void display_optdepends(pmpkg_t *pkg)
{
	alpm_list_t *optdeps = alpm_pkg_get_optdepends(pkg);
	if(optdeps) {
		printf(_("Optional dependencies for %s\n"), alpm_pkg_get_name(pkg));
		list_display_linebreak("   ", optdeps);
	}
}

/* presents a prompt and gets a Y/N answer */
static int question(short preset, char *fmt, va_list args)
{
	char response[32];
	FILE *stream;

	if(config->noconfirm) {
		stream = stdout;
	} else {
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	vfprintf(stream, fmt, args);

	if(preset) {
		fprintf(stream, " %s ", _("[Y/n]"));
	} else {
		fprintf(stream, " %s ", _("[y/N]"));
	}

	if(config->noconfirm) {
		fprintf(stream, "\n");
		return(preset);
	}

	if(fgets(response, sizeof(response), stdin)) {
		strtrim(response);
		if(strlen(response) == 0) {
			return(preset);
		}

		if(strcasecmp(response, _("Y")) == 0 || strcasecmp(response, _("YES")) == 0) {
			return(1);
		} else if (strcasecmp(response, _("N")) == 0 || strcasecmp(response, _("NO")) == 0) {
			return(0);
		}
	}
	return(0);
}

int yesno(char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = question(1, fmt, args);
	va_end(args);

	return(ret);
}

int noyes(char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = question(0, fmt, args);
	va_end(args);

	return(ret);
}

int pm_printf(pmloglevel_t level, const char *format, ...)
{
	int ret;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	ret = pm_vfprintf(stdout, level, format, args);
	va_end(args);

	return(ret);
}

int pm_fprintf(FILE *stream, pmloglevel_t level, const char *format, ...)
{
	int ret;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	ret = pm_vfprintf(stream, level, format, args);
	va_end(args);

	return(ret);
}

int pm_asprintf(char **string, const char *format, ...)
{
	int ret = 0;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	if(vasprintf(string, format, args) == -1) {
		pm_fprintf(stderr, PM_LOG_ERROR,  _("failed to allocate string\n"));
		ret = -1;
	}
	va_end(args);

	return(ret);
}

int pm_vasprintf(char **string, pmloglevel_t level, const char *format, va_list args)
{
	int ret = 0;
	char *msg = NULL;

	/* if current logmask does not overlap with level, do not print msg */
	if(!(config->logmask & level)) {
		return ret;
	}

	/* print the message using va_arg list */
	ret = vasprintf(&msg, format, args);

	/* print a prefix to the message */
	switch(level) {
		case PM_LOG_DEBUG:
			pm_asprintf(string, "debug: %s", msg);
			break;
		case PM_LOG_ERROR:
			pm_asprintf(string, _("error: %s"), msg);
			break;
		case PM_LOG_WARNING:
			pm_asprintf(string, _("warning: %s"), msg);
			break;
		case PM_LOG_FUNCTION:
			pm_asprintf(string, _("function: %s"), msg);
			break;
		default:
			break;
	}
	free(msg);

	return(ret);
}

int pm_vfprintf(FILE *stream, pmloglevel_t level, const char *format, va_list args)
{
	int ret = 0;

	/* if current logmask does not overlap with level, do not print msg */
	if(!(config->logmask & level)) {
		return ret;
	}

#if defined(PACMAN_DEBUG)
	/* If debug is on, we'll timestamp the output */
	if(config->logmask & PM_LOG_DEBUG) {
		time_t t;
		struct tm *tmp;
		char timestr[10] = {0};

		t = time(NULL);
		tmp = localtime(&t);
		strftime(timestr, 9, "%H:%M:%S", tmp);
		timestr[8] = '\0';

		printf("[%s] ", timestr);
	}
#endif

	/* print a prefix to the message */
	switch(level) {
		case PM_LOG_DEBUG:
			fprintf(stream, "debug: ");
			break;
		case PM_LOG_ERROR:
			fprintf(stream, _("error: "));
			break;
		case PM_LOG_WARNING:
			fprintf(stream, _("warning: "));
			break;
		case PM_LOG_FUNCTION:
		  /* TODO we should increase the indent level when this occurs so we can see
			 * program flow easier.  It'll be fun */
			fprintf(stream, _("function: "));
			break;
		default:
			break;
	}

	/* print the message using va_arg list */
	ret = vfprintf(stream, format, args);
	return(ret);
}

#ifndef HAVE_STRNDUP
/* A quick and dirty implementation derived from glibc */
static size_t strnlen(const char *s, size_t max)
{
    register const char *p;
    for(p = s; *p && max--; ++p);
    return(p - s);
}

char *strndup(const char *s, size_t n)
{
  size_t len = strnlen(s, n);
  char *new = (char *) malloc(len + 1);

  if (new == NULL)
    return NULL;

  new[len] = '\0';
  return (char *) memcpy(new, s, len);
}
#endif

/* vim: set ts=2 sw=2 noet: */
