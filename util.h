/* util.h: minimal utility wrappers for origo. */

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);

void eprintf(const char *, ...);
void weprintf(const char *, ...);

void *emalloc(size_t);
void *ecalloc(size_t, size_t);
void *erealloc(void *, size_t);
void *ereallocarray(void *, size_t, size_t);
char *estrdup(const char *);
char *estrndup(const char *, size_t);

long long estrtonum(const char *, long long, long long);

ssize_t writeall(int, const void *, size_t);
int fshut(FILE *, const char *);

#define LEN(a)     (sizeof(a) / sizeof((a)[0]))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))
#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MUL_NO_OVERFLOW (1UL << (sizeof(size_t) * 4))

extern char *argv0;
