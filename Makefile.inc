CPPFLAGS+=	-I${.CURDIR}/..
LIBESTRAIER=	/usr/local/lib/libestraier.a
CDIAGFLAGS+=	-Wall -Wextra -Werror -Wno-unused-parameter

WARNS=		1
DEBUG=		-g -O0

.PATH: ${.CURDIR}/..
