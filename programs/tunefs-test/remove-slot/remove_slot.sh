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
# This script will check the remove slot function for tunefs.ocfs2.
#
# precondition_test:
# check whether tunefs.ocfs2 abort its operation when the precondition
# isn't satisfied. The extra program "remove_slot" is needed to create
# some files under some specified location.
#
# normal_test:
# This program will do the normal test for removing slots. It includes
# creating files using the specified slots and shrinking the slots. 
#

MKFS_BIN=`which mkfs.ocfs2`
FSCK_BIN=`which fsck.ocfs2`
DEBUGFS_BIN=`which debugfs.ocfs2`
TUNEFS_BIN=`which tunefs.ocfs2`
TEST_AUX_BIN=
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
	      --with-tunefs=PROGRAM        use the PROGRAM as tunefs.ocfs2
	      --with-fsck=PROGRAM          use the PROGRAM as fsck.ocfs2
	      --with-mkfs=PROGRAM          use the PROGRAM as mkfs.ocfs2
	      --with-debugfs=PROGRAM       use the PROGRAM as debugfs.ocfs2
	      --with-aux=PROGRAM           use the PROGRAM as auxiliary tool.

	Examples:

	  $script --with-aux=./remove_slot --log-dir=/tmp /dev/sdd1
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
	return 1
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

function precondition_test()
{
	echo "Orphan dir test: Create a file under the directory and tunefs.ocfs2 should fail."
	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	slot_num=4
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	slot_num=`expr $slot_num - 1`
	$TEST_AUX_BIN -n $slot_num -o $DEVICE
	exit_if_bad $? "0" "fail to create orphan file for slot $slot_num" $LINENO

	echo "y"|$TUNEFS_BIN -N $slot_num $DEVICE
	exit_if_bad $? "1" "tunefs.ocfs2 should fail while there is some orphan file for slot $slot_num" $LINENO

	echo "local alloc test: Alloc some bits to local alloc and tunefs.ocfs2 should fail."
	slot_num=4
	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	slot_num=`expr $slot_num - 1`
	$TEST_AUX_BIN -n $slot_num -l $DEVICE
	exit_if_bad $? "0" "fail to allocate local alloc file for slot $slot_num" $LINENO

	echo "y"|$TUNEFS_BIN -N $slot_num $DEVICE
	exit_if_bad $? "1" "tunefs.ocfs2 should fail while local alloc file for slot $slot_num isn't empty." $LINENO

	echo "truncate log test: Alloc some bits to truncate log and tunefs.ocfs2 should fail."
	slot_num=4
	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	slot_num=`expr $slot_num - 1`
	$TEST_AUX_BIN -n $slot_num -l $DEVICE
	exit_if_bad $? "0" "fail to allocate truncate log file for slot $slot_num" $LINENO

	echo "y"|$TUNEFS_BIN -N $slot_num $DEVICE
	exit_if_bad $? "1" "tunefs.ocfs2 should fail while truncate log for slot $slot_num isn't empty." $LINENO
}

# create some test files in all the slots
function create_test_files()
{
	local max_slots=$1
	local dir="/tmp/test$RANDOM"
	local -i i=0
	local -i j=0
	local prefix=$RANDOM
	local total_files=0
	local -i mount_slot=0
	local -i mounted_slot=0

	mkdir $dir

	for((mount_slot=0;mount_slot<$max_slots;mount_slot++))
	do
		mount -t ocfs2 -o preferred_slot=$mount_slot $DEVICE $dir
		exit_if_bad $? "0" "fail to mount $DEVICE at $dir in slot $mount_slot" $LINENO

		# check whether the device is mounted in the preferred slot.
		# now I use dmesg log to find the mounted slot, I am not sure
		# whether there is another place in the system for me to read
		# the mounted slot.
		mounted_slot=`dmesg|tail -n 1|sed -e 's/^.*slot \(.*\))/\1/'`
		[ $mounted_slot = $mount_slot ]
		exit_if_bad $? "0" "we should mount $DEVICE in slot $mount_slot, not $mounted_slot" $LINENO

		prefix=$RANDOM
		total_files=$RANDOM
		mkdir "$dir/$prefix"
		echo "Create $total_files test files using slot $mount_slot in dir $dir/$prefix."

		# we close xtrace since there are too many commands.
		set +x

		#create some inodes at the specified inodes.
		for((i=1;i<$total_files;i++))
		do
			touch "$dir/$prefix/ino$i"
		done

		#reopen xtrace
		set -x
		umount $dir
		exit_if_bad $? "0" "fail to umount $DEVICE at $dir" $LINENO
	done
	rm -r $dir

}

function normal_test()
{
	local -i slot_num=8
	local -i result=0

	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	#save the perfect fsck output first for our future use.
	$FSCK_BIN -fy $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT_STANDARD

	# First remove the slot once at a time, from 8->7->..->3->2.
	while((slot_num>=2))
	do
		create_test_files $slot_num

		slot_num=`expr $slot_num - 1`
		# Decrease the size.
		echo "y"|$TUNEFS_BIN -N $slot_num $DEVICE

		result=`$DEBUGFS_BIN -R "stats" $DEVICE|grep "Slots"|awk '{print $4}'`
		exit_if_bad $result $slot_num "fsck find errors after decrease slot to $slot_num." $LINENO

		$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
		diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
		exit_if_bad $? "0" "fsck find errors after decrease slot to $slot_num." $LINENO

	done

	#shrink the slot from 8 to 2 directly.
	slot_num=8
	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	#save the perfect fsck output first for our future use.
	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT_STANDARD

	create_test_files $slot_num

	slot_num=2
	# Decrease the size.
	echo "y"|$TUNEFS_BIN -N $slot_num $DEVICE

	result=`$DEBUGFS_BIN -R "stats" $DEVICE|grep "Slots"|awk '{print $4}'`
	exit_if_bad $result $slot_num "fsck find errors after decrease slot to $slot_num." $LINENO

	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors after decrease slot to $slot_num." $LINENO
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
	"--with-tunefs="*)
		TUNEFS_BIN="${1#--with-tunefs=}"
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
	"--with-aux="*)
		TEST_AUX_BIN="${1#--with-aux=}"
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

which $TEST_AUX_BIN
if [ "$?" != "0" ]; then
	echo "$TEST_AUX_BIN not exist, can't go on the test."
	usage
	exit 1
fi

set_log_file

#from now on all the command and log will be recorded to the logfile.
set -x

precondition_test

normal_test

exit 0
