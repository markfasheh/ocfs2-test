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

################################################################

#
# This script will check the function of ocfs2_truncate in ocfs2-tools.
#
# The main test script is truncate_test and it works like this:
# 1. Create a file with the specified tree depth.

# 2. Truncate the file to some size.
#
# 3. Using fsck.ocfs2 to check whether the volume is corrupted.
#
# Please Note that this script much have the --with-truncate option to
# do the truncate test.
#

MKFS_BIN=`which mkfs.ocfs2`
FSCK_BIN=`which fsck.ocfs2`
DEBUGFS_BIN=`which debugfs.ocfs2`
TRUNCATE_BIN=
LOG_DIR=$PWD

BLOCKDEV=`which blockdev`
DEVICE=""

FSCK_OUTPUT="/tmp/fsck.ocfs2.output"
FSCK_OUTPUT_STANDARD="/tmp/fsck.ocfs2.output.std"

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
	      --log-dir=DIRECTORY          use the DIRECTORY to store the log
	      --with-fsck=PROGRAM          use the PROGRAM as fsck.ocfs2
	      --with-mkfs=PROGRAM          use the PROGRAM as fswreck
	      --with-debugfs=PROGRAM       use the PROGRAM as mkfs.ocfs2
	      --with-truncate=PROGRAM      use the PROGRAM as truncate tools

	Examples:

	  $script --with-truncate=./test_truncate --log-dir=/tmp /dev/sdd1
	EOF
}

#
# warn_if_bad
#
#	$1	the result to check
#	$2	the result we want
#	$3	the error messge
#	$4	the line num of the caller
#
#
function warn_if_bad()
{
	local -i rc="$1"
	local -i wanted="$2"
	local script="${0##*/}"

	# Ignore if no problems
	[ "$rc" -eq "$wanted" ] && return 0

	# Broken
	shift
	echo "$script: $@">&2
	echo "$script: $@">&3
	return "$rc"
}

#
# exit_if_bad		Put out error message(s) if $1 has bad RC.
#
#	$1	the result to check
#	$2	the result we want
#	$3	the error messge
#	$4	the line num of the caller
#
#       Exits with 1 unless $1 is 0
#
function exit_if_bad()
{
	warn_if_bad "$@" || exit 1
	return 0
}

function set_log_file()
{
	mkdir -p $LOG_DIR
	if [ ! -d $LOG_DIR ]; then
		echo "log_dir[$LOG_DIR] not exist, use [$PWD] instead."
		LOG_DIR=$PWD
	fi
	
	output_log="$LOG_DIR/`date +%F-%H-%M-%S`-output.log"
	exec 3>&1
	exec 1>$output_log 2>&1
}

function truncate_test()
{
	local tree_depth="$1"

	test_file="test$RANDOM"
	$TRUNCATE_BIN -c $tree_depth -f $test_file $DEVICE

	$FSCK_BIN -fy $DEVICE>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors when creating file $test_file." $LINENO

	# get the file size first.
	new_size=`echo "stat $test_file"|$DEBUGFS_BIN $DEVICE|grep "Size:"|awk '{print $8}'`

	# truncate the file for 5 times, every time truncate
	# it to half size.
	for((i=0;i<5;i++))
	do
		new_size=`expr $new_size / 2`
		$TRUNCATE_BIN -f $test_file -s $new_size $DEVICE

		$FSCK_BIN -fy $DEVICE>$FSCK_OUTPUT
		diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
		exit_if_bad $? "0" "fail to truncate file to size $new_size." $LINENO
	done

	#truncate the file to 0.
	$TRUNCATE_BIN -f $test_file -s 0 $DEVICE

	$FSCK_BIN -fy $DEVICE>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fail to truncate file to size $new_size." $LINENO
}

function normal_test()
{
	local test_file=""
	local new_size=""
	local -i i=0

	for blocksize in 512 1024 2048 4096
	do
		for clustersize in \
			4096 8192 16384 32768 65536 131072 262144 524288 1048576
		do

			dd if=/dev/zero of=$DEVICE bs=4096 count=3
			$MKFS_BIN -b $blocksize -C $clustersize $DEVICE

			# In some instance, fsck.ocfs2 will find "CHAIN_CPG"
			# for a volume. So we fix it first.
			$FSCK_BIN -fy $DEVICE>$FSCK_OUTPUT_STANDARD

			#save the perfect fsck output first for our future use.
			$FSCK_BIN -f $DEVICE>$FSCK_OUTPUT_STANDARD

			truncate_test 0
			truncate_test 1
		done
	done
}

################################################################

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
	"--log-dir="*)
		LOG_DIR="${1#--log-dir=}"
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

which $TRUNCATE_BIN
if [ "$?" != "0" ]; then
	echo "$TRUNCATE_BIN not exist, can't go on the test."
	usage
	exit 1
fi

set_log_file

#from now on all the command and log will be recorded to the logfile.
set -x

normal_test

exit 0
