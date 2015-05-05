#ifdef HAVE_CONFIG_H
#include "config.h"
#include "replace/replace.h"
#endif

#include <sys/cdefs.h>

#ifndef __syslog__
#define __syslog__	__printf__
#endif
#ifndef __used
#define	__used		__attribute__((__used__))
#endif

#ifdef __FreeBSD__
/*
 * __FreeBSD_version >= 1100072 has reallocarray() delete this when all
 * the old versions are gone.
 */
#include <sys/types.h>
void	*reallocarray(void *, size_t, size_t);
#endif
