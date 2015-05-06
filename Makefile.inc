PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

CFLAGS+=	-I${.CURDIR}/..  -I/usr/local/include
LIBESTRAIER=	/usr/local/lib/libestraier.a

.include "${.CURDIR}/../compat.mk"

.PATH: ${.CURDIR}/..
