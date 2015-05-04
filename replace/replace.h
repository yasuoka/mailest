#define	__syslog__	__printf__
#define	__used		__attribute__((__used__))

#include <sys/types.h>

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
FILE		*open_memstream(char **, size_t *);
#endif

#ifndef HAVE_REALLOCARRAY
void		*reallocarray(void *, size_t, size_t);
#endif
