#!/bin/bash
#
# Copyright (C) 2007 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#

# This script will test whether the bug 849 is fixed. For more details, please
# see http://oss.oracle.com/bugzilla/show_bug.cgi?id=849

MKFS_BIN=`which mkfs.ocfs2`
FSCK_BIN=`which fsck.ocfs2`
DEBUGFS_BIN=`which debugfs.ocfs2`

FILE1="aa"
FILE2="bb"

#
# usage			Display help information and exit.
#
function usage()
{
	local script="${0##*/}"
	cat <<-EOF
	Usage: $script [options] device

	Options:
	      --help                       display this help and exit
	      --with-fsck=PROGRAM          use the PROGRAM as fsck.ocfs2
	      --with-mkfs=PROGRAM          use the PROGRAM as mkfs.ocfs2
	      --with-debugfs=PROGRAM       use the PROGRAM as debugfs.ocfs2

	EOF
}

#
# main
#
if [ "$#" -eq "0" ]
then
	usage
	exit 255
fi

while [ "$#" -gt "0" ]
do
	case "$1" in
	"--help")
		usage
		exit 255
		;;
	"--with-fsck="*)
		FSCK_BIN="${1#--with-fsck=}"
		;;
	"--with-mkfs="*)
		MKFS_BIN="${1#--with-mkfs=}"
		;;
	"--with-debugfs="*)
		DEBUGFS_BIN="${1#--with-debugfs=}"
		;;
	*)
		DEVICE="$1"
		;;
	esac
	shift
done

if [ ! -b "$DEVICE" ]; then
	echo "invalid block device - $DEVICE"
	usage
	exit 1
fi

# 1. Make a ocfs2 volume.
dd if=/dev/zero of=$DEVICE bs=4096 count=3
$MKFS_BIN -b 512 -C 4K $DEVICE

# 2. mount the volume. We may create a tmp directory first.
TMPFILE=`mktemp -t test.XXXXXXXXXX` || exit 1
rm -f $TMPFILE
TMPDIR=$TMPFILE
mkdir $TMPDIR
mount -t ocfs2 $DEVICE $TMPDIR || exit 1

# 3. Create 2 files both have a tree depth 2.
for((i=1;i<540;i++))
do
dd if=/dev/zero of="$TMPDIR/$FILE1" bs=4096 count=1 seek=$i
sync
dd if=/dev/zero of="$TMPDIR/$FILE2" bs=4096 count=1 seek=$i
sync
done

umount $TMPDIR || exit 1
rm -rf $TMPDIR

# 4. remember the last extent block number stored in "$FILE1".
TMP_LINE=`echo "stat $FILE1"|$DEBUGFS_BIN $DEVICE|grep Extblk`
LAST_BLKNO=`echo $TMP_LINE|awk '{print $3}'`

# 5. run the fsck.ocfs2
$FSCK_BIN -fy $DEVICE

# 6. get the last extent block information again and check whether they are the
#    same.
TMP_LINE=`echo "stat $FILE1"|$DEBUGFS_BIN $DEVICE|grep Extblk`
LAST_BLKNO_NEW=`echo $TMP_LINE|awk '{print $3}'`

if [ $LAST_BLKNO != $LAST_BLKNO_NEW ]; then
	echo "FAIL: bug 849 still exists."
	exit 1
fi

echo "SUCCEED: bug 849 is fixed."
exit 0
