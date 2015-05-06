#include <sys/types.h>
#include <sys/cdefs.h>

#ifndef HAVE_STRLCPY
size_t		 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCAT
size_t		 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRTONUM
long long	 strtonum(const char *, long long, long long, const char **);
#endif

#ifndef HAVE_OPEN_MEMSTREAM
#include <stdio.h>
FILE		*open_memstream(char **, size_t *);
#endif

#ifndef HAVE_REALLOCARRAY
void		*reallocarray(void *, size_t, size_t);
#endif

#ifndef __syslog__
#define __syslog__	__printf__
#endif
#ifndef __used
#define	__used		__attribute__((__used__))
#endif

#ifndef RB_FOREACH_SAFE
#define RB_FOREACH_SAFE(x, name, head, y)			\
	for ((x) = RB_MIN(name, head);				\
	     (x) != NULL && ((y) = name##_RB_NEXT(x), 1);	\
	     (x) = (y))
#endif
