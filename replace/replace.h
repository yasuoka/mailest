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

#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>

#ifndef RB_FOREACH_SAFE
#define RB_FOREACH_SAFE(x, name, head, y)			\
	for ((x) = RB_MIN(name, head);				\
	     (x) != NULL && ((y) = name##_RB_NEXT(x), 1);	\
	     (x) = (y))
#endif
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifndef timespeccmp
#define timespeccmp(_tsa, _tsb, _cmp)				\
	(((_tsa)->tv_sec == (_tsb)->tv_sec)			\
	    ? ((_tsa)->tv_nsec _cmp (_tsb)->tv_nsec)		\
	    : ((_tsa)->tv_sec _cmp (_tsb)->tv_sec))
#endif
#ifndef timespecsub
#define timespecsub(_tsa, _tsb, _tsr)				\
	do {							\
		if ((_tsa)->tv_nsec < (_tsb)->tv_nsec) {	\
			(_tsr)->tv_nsec = (_tsa)->tv_nsec	\
			    + 1000000000L - (_tsb)->tv_nsec;	\
			(_tsr)->tv_sec = (_tsa)->tv_sec		\
			    - (_tsb)->tv_sec - 1;		\
		} else {					\
			(_tsr)->tv_nsec = (_tsa)->tv_nsec	\
			    - (_tsb)->tv_nsec;			\
			(_tsr)->tv_sec = (_tsa)->tv_sec		\
			    - (_tsb)->tv_sec;			\
		}						\
	} while (0 /*CONSTCOND*/)
#endif

#ifndef timespecclear
#define timespecclear(_ts)			\
	(_ts)->tv_sec = (_ts)->tv_nsec = 0
#endif
