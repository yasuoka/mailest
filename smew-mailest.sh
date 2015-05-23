#!/bin/sh
#
# wrapper script for mew
#

MAILESTCTL=${MAILESTCTL:-mailestctl}

usage() {
	echo "usage: smew-mailestd [-cph] msgid id.db mydir" >&2
}

cmd=smew
while getopts "pch" ch "$@"; do
	case $ch in
	'p')	cmd='message-id'
		flags='-max 1'
		;;
	'c')	cmd='parent-id'
		flags='-max 1'
		;;
	'h')	usage
		exit 64
		;;
	esac
done

shift $(($OPTIND - 1))
if [ $# -le 0 ]; then
	usage
	exit 64
fi
msgid=$1
mydir=$3

if [ $cmd = 'smew' ]; then
	exec $MAILESTCTL $cmd "$msgid" "$mydir"
else
	exec $MAILESTCTL $cmd $flags "$msgid"
fi
