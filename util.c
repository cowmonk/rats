/* Repackaged small subset of libutil/%.c to one util.c */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* needed for strdup */
#endif

#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *argv0 = "origo";

void
xvprintf(const char *fmt, va_list ap)
{
	if (argv0 && strncmp(fmt, "usage", strlen("usage")))
		fprintf(stderr, "%s: ", argv0);

	vfprintf(stderr, fmt, ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	}
}

void
eprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xvprintf(fmt, ap);
	va_end(ap);

	exit(1);
}

void
weprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xvprintf(fmt, ap);
	va_end(ap);
}

void
enprintf(int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xvprintf(fmt, ap);
	va_end(ap);

	exit(status);
}


void *
enmalloc(int status, size_t size)
{
	void *p;

	p = malloc(size);
	if (!p)
		enprintf(status, "malloc: out of memory\n");
	return p;
}

void *
emalloc(size_t size)
{
	return enmalloc(1, size);
}

void *
encalloc(int status, size_t nmemb, size_t size)
{
	void *p;

	p = calloc(nmemb, size);
	if (!p)
		enprintf(status, "calloc: out of memory\n");
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	return encalloc(1, nmemb, size);
}

void *
enrealloc(int status, void *p, size_t size)
{
	p = realloc(p, size);
	if (!p)
		enprintf(status, "realloc: out of memory\n");
	return p;
}

void *
reallocarray(void *optr, size_t nmemb, size_t size)
{
                if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
                                            nmemb > 0 && SIZE_MAX / nmemb < size) {
                                        errno = ENOMEM;
                                                        return NULL;
                                                                }
                        return realloc(optr, size * nmemb);
}

void *
erealloc(void *p, size_t size)
{
	return enrealloc(1, p, size);
}

void *
enreallocarray(int status, void *optr, size_t nmemb, size_t size)
{
	void *p;

	if (!(p = reallocarray(optr, nmemb, size)))
		enprintf(status, "reallocarray: out of memory\n");

	return p;
}

void *
ereallocarray(void *optr, size_t nmemb, size_t size)
{
	return enreallocarray(1, optr, nmemb, size);
}

char *
enstrdup(int status, const char *s)
{
	char *p;

	p = strdup(s);
	if (!p)
		enprintf(status, "strdup: out of memory\n");
	return p;
}

char *
estrdup(const char *s)
{
	return enstrdup(1, s);
}

char *
enstrndup(int status, const char *s, size_t n)
{
	char *p;

	p = strndup(s, n);
	if (!p)
		enprintf(status, "strndup: out of memory\n");
	return p;
}

size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}
	if (n == 0) {
		if (siz != 0)
			*d = '\0'; /* NUL-terminate dst */
		while (*s++)
			;
	}
	return(s - src - 1); /* count does not include NUL */
}


size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;
	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;
	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';
	return(dlen + (s - src)); /* count does not include NUL */
}

#define	INVALID		1
#define	TOOSMALL	2
#define	TOOLARGE	3

long long
strtonum(const char *numstr, long long minval, long long maxval,
         const char **errstrp)
{
	long long ll = 0;
	int error = 0;
	char *ep;
	struct errval {
		const char *errstr;
		int err;
	} ev[4] = {
		{ NULL,		0 },
		{ "invalid",	EINVAL },
		{ "too small",	ERANGE },
		{ "too large",	ERANGE },
	};

	ev[0].err = errno;
	errno = 0;
	if (minval > maxval) {
		error = INVALID;
	} else {
		ll = strtoll(numstr, &ep, 10);
		if (numstr == ep || *ep != '\0')
			error = INVALID;
		else if ((ll == LLONG_MIN && errno == ERANGE) || ll < minval)
			error = TOOSMALL;
		else if ((ll == LLONG_MAX && errno == ERANGE) || ll > maxval)
			error = TOOLARGE;
	}
	if (errstrp != NULL)
		*errstrp = ev[error].errstr;
	errno = ev[error].err;
	if (error)
		ll = 0;

	return (ll);
}

long long
enstrtonum(int status, const char *numstr, long long minval, long long maxval)
{
	const char *errstr;
	long long ll;

	ll = strtonum(numstr, minval, maxval, &errstr);
	if (errstr)
		enprintf(status, "strtonum %s: %s\n", numstr, errstr);
	return ll;
}

long long
estrtonum(const char *numstr, long long minval, long long maxval)
{
	return enstrtonum(1, numstr, minval, maxval);
}

ssize_t
writeall(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len) {
		n = write(fd, p, len);
		if (n <= 0)
			return n;
		p += n;
		len -= n;
	}

	return p - (const char *)buf;
}

int
fshut(FILE *fp, const char *fname)
{
	int ret = 0;

	/* fflush() is undefined for input streams by ISO C,
	 * but not POSIX 2008 if you ignore ISO C overrides.
	 * Leave it unchecked and rely on the following
	 * functions to detect errors.
	 */
	fflush(fp);

	if (ferror(fp) && !ret) {
		weprintf("ferror %s:", fname);
		ret = 1;
	}

	if (fclose(fp) && !ret) {
		weprintf("fclose %s:", fname);
		ret = 1;
	}

	return ret;
}

