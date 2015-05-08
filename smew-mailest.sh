#!/bin/sh
#
# wrapper script for mew
#

MAILESTCTL=${MAILESTCTL:-mailestctl}

msgid=$1
mydir=$3

exec $MAILESTCTL smew "$msgid" "$mydir"
