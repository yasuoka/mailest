#USE_BSDMAKE=	#

HOSTTYPE!=	uname -s

.ifndef USE_BSDMAKE
.if (${HOSTTYPE} != "OpenBSD")
.error	Do "./configure && gmake"
.endif
.else
.if ${HOSTTYPE} == "FreeBSD"
#
# FreeBSD
#
CFLAGS+=	-DBSD_COMPAT
CFLAGS+=	-DHAVE_STRLCPY -DHAVE_STRLCAT
CFLAGS+=	-DHAVE_STRTONUM -DHAVE_OPEN_MEMSTREAM
.PATH:		${.CURDIR}/../replace
SRCS+=		reallocarray.c
.elif ${HOSTTYPE} == "NetBSD"
#
# NetBSD
#
MANDIR=		${PREFIX}/man
NOGCCERROR=	#
CFLAGS+=	-DBSD_COMPAT
CFLAGS+=	-DHAVE_STRLCPY -DHAVE_STRLCAT
CFLAGS+=	-I/usr/pkg/include
LIBESTRAIER=	/usr/pkg/lib/libestraier.a
CFLAGS+=	-I/usr/pkg/include
LDFLAGS=	-L/usr/pkg/lib -Wl,-rpath=/usr/pkg/lib
SRCS+=		reallocarray.c strtonum.c open_memstream.c
.PATH:		${.CURDIR}/../replace
.endif
.endif
