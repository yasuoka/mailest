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
NOGCCERROR=	#
LOCALBASE=	/usr/pkg
MANDIR=		${LOCALBASE}/man
CFLAGS+=	-DBSD_COMPAT
CFLAGS+=	-DHAVE_STRLCPY -DHAVE_STRLCAT -DHAVE_SYS_TREE_H
CFLAGS+=	-I${LOCALBASE}/include
LDFLAGS+=	-Wl,-rpath=${LOCALBASE}/lib
SRCS+=		reallocarray.c strtonum.c open_memstream.c
.PATH:		${.CURDIR}/../replace
.endif
.endif
