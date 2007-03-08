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

# This script will test whether the bug 856 is fixed. For more details, please
# see http://oss.oracle.com/bugzilla/show_bug.cgi?id=856
# we may need debug_truncate to go on our test. It can be compiled by ocfs2-tools
# with option enable-debugexe=yes. It should be under directory libocfs2/.

MKFS_BIN="mkfs.ocfs2"
FSCK_BIN="fsck.ocfs2"
DEBUGFS_BIN="debugfs.ocfs2"
TRUNCATE_BIN="debug_truncate"
FSCK_OUTPUT="/tmp/fsck.ocfs2.output"
FSCK_OUTPUT_STANDARD="/tmp/fsck.ocfs2.output.std"

FILE1="aa"
FILE2="bb"
TMP_LINE=""
BLKNO=""
MAX_TRUNCATE_TIME="300"
MAX_TIMES="600"
MAX_FILE_SIZE=""
CLUSTER_SIZE="4096"
BLOCK_SIZE="512"
RAND="1"

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
	      --with-truncate=PROGRAM      use the PROGRAM as the truncate tool

	EOF
}

function check_executes()
{
	echo "checking the programs we need in the test..."
	for PROGRAM in $MKFS_BIN $FSCK_BIN $DEBUGFS_BIN $TRUNCATE_BIN
	do
		which $PROGRAM
		if [ "$?" != "0" ]; then
			echo "$PROGRAM not exist"
			usage
			exit 1
		fi
	done
}

#
# get a random integer in [min, max).
# $1 is the min
# $2 is the max
# RAND= min + (rand() % (max - min));
#
function get_rand()
{
	local -i min="$1"
	local -i max="$2"
	local -i ret="0"
	local -i ret1="1"
	local -i rand=""
	
	ret=`expr $max - $min`
	
	# RANDOM in shell range from 0 to 32767
	if [ $ret -le 32767 ]; then
		ret=`expr $RANDOM % $ret`
		RAND=`expr $ret + $1`
	else
		ret1=`expr $ret / 32767`
		rand=$RANDOM
		RAND=`expr $rand \* $ret1 + $RANDOM`
	fi
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
	"--with-truncate="*)
		TRUNCATE_BIN="${1#--with-truncate=}"
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

check_executes

# 1. Make a ocfs2 volume.
dd if=/dev/zero of=$DEVICE bs=4096 count=3
$MKFS_BIN -b $BLOCK_SIZE -C $CLUSTER_SIZE $DEVICE
$FSCK_BIN -f $DEVICE>$FSCK_OUTPUT_STANDARD

# 2. mount the volume. We may create a tmp directory first.
TMPDIR="/tmp/test$RANDOM"
mkdir $TMPDIR
mount -t ocfs2 $DEVICE $TMPDIR || exit 1

# 3. Create 2 files both have a tree depth 2.
# We need two files so that all the clusters are allocated
# and inserted into different extent records. 
#
MAX_WRITE=`expr $CLUSTER_SIZE \* 10`
for((i=1;i<$MAX_TIMES;i++))
do
dd if=/dev/zero of="$TMPDIR/$FILE1" bs=$MAX_WRITE count=1 seek=$i
sync
dd if=/dev/zero of="$TMPDIR/$FILE2" bs=$MAX_WRITE count=1 seek=$i
sync
done
MAX_FILE_SIZE=`expr $MAX_TIMES \* $MAX_WRITE`

umount $TMPDIR || exit 1
rm -rf $TMPDIR

# 4. use truncate to truncate the file and see if any error
#    happens to the volume.
TMP_LINE=`echo "ls"|$DEBUGFS_BIN $DEVICE|grep "$FILE1"`
BLKNO=`echo $TMP_LINE|awk '{print $1}'`

for((i=0;i<$MAX_TRUNCATE_TIME;i++))
do
	get_rand 1 $MAX_FILE_SIZE
	NEW_SIZE=$RAND
	
	echo "$TRUNCATE_BIN -i $BLKNO -s $NEW_SIZE $DEVICE"
	$TRUNCATE_BIN -i $BLKNO -s $NEW_SIZE $DEVICE
	
	$FSCK_BIN -fy $DEVICE>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD>/dev/null
	if [ $? != "0" ]; then
		echo "Something error found in fsck.ocfs2."
		echo "FAIL: bug 856 still exists."
		exit 1
	fi

	# check whether the cluster in the file is really truncated.
	TMP_LINE=`echo "stat <$BLKNO>"|$DEBUGFS_BIN $DEVICE|grep "Links"`
	CLUSTER_NUM=`echo $TMP_LINE|awk '{print $4}'`
	CLUSTER_DESIRED=`expr $NEW_SIZE / $CLUSTER_SIZE + 1`
	if [ $CLUSTER_NUM != $CLUSTER_DESIRED ]; then
		echo "Something error found in inode <$BLKNO>."
		echo "FAIL: bug 856 still exists."
		exit 1
	fi
done

echo "SUCCEED: bug 856 is fixed."
exit 0
