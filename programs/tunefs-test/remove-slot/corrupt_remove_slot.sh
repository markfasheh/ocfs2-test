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
# This script will do some corruption test which will patch tunefs.ocfs2
# with the specified "corrupt_remove_slot.patch" in this directory,
# let the new tunefs.ocfs2 abort at the specified place and check
# whether fsck.ocfs2 can fix all the problems.
#
################################################################

MKFS_BIN=`which mkfs.ocfs2`
FSCK_BIN=`which fsck.ocfs2`
DEBUGFS_BIN=`which debugfs.ocfs2`
TUNEFS_BIN=`which tunefs.ocfs2`
CORRUPT_SRC=
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
	      --with-mkfs=PROGRAM          use the PROGRAM as mkfs.ocfs2 
	      --with-debugfs=PROGRAM       use the PROGRAM as debugfs.ocfs2
	      --with-corrupt-src=DIRECTORY use the DIRECTORY to patch the corrupt code .

	Examples:

	  $script --with-corrupt-src=/home/test/ocfs2-tools --log-dir=/tmp /dev/sdd1
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

function patch_tunefs()
{
	local pwd=$PWD

	if [ -z $CORRUPT_SRC ]; then
		echo "The ocfs2-tools for test corruption doesn't exist, so we assume that you have patch it by yourself."
		return
	fi

	cd $CORRUPT_SRC
	patch -p1 < $pwd/corrupt_remove_slot.patch
	exit_if_bad $? 0 "Can't patch the src directory."
	./autogen.sh
	make
	if [ ! -x "$CORRUPT_SRC/tunefs.ocfs2/tunefs.ocfs2" ];then
		echo "Can't build the specified tunefs.ocfs2."
	fi

	TUNEFS_BIN="$CORRUPT_SRC/tunefs.ocfs2/tunefs.ocfs2"
	cd $pwd
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
		mkdir "$dir/$prefix"
		total_files=$RANDOM
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

function check_volume()
{
	local mount_slot=$1
	local mounted_slot="0"
	local dir=$2
	local lineno=$3

	# we shouldn't find any error during a second fsck.
	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors in the second fsck." $lineno

	# we should be able to mount the volume in the preferred slot
	# without any problem.
	mount -t ocfs2 -o preferred_slot=$mount_slot $DEVICE $dir
	exit_if_bad $? "0" "fail to mount $DEVICE at $dir in slot $mount_slot" $lineno
	mounted_slot=`dmesg|tail -n 1|sed -e 's/^.*slot \(.*\))/\1/'`
	[ $mounted_slot = $mount_slot ]
	exit_if_bad $? "0" "we should succeed to mount $DEVICE at $dir in slot $mount_slot" $LINENO
	umount $dir

}

# The following definition is defined according to the corrupt place
# we define in corrupt.patch.
RS_AFTER_RELINK_EXTENT_ALLOC=1
RS_AFTER_RELINK_INODE_ALLOC=2
RS_AFTER_TRUNCATE_ORPHAN=3
RS_AFTER_TRUNCATE_JOURNAL=4
RS_AFTER_WRITE_SUPER=5
RS_AFTER_REMOVE_ONE_SLOT=6
RS_AFTER_MOVE_SOME_REC=7
RS_AFTER_CHANGE_SUB_ALLOC=8
RS_AFTER_LINK_GROUP=9
RS_AFTER_MOVE_ONE_GROUP=10
RS_AFTER_EMPTY_JOURNAL=11

function corrupt_test()
{
	local -i corrupt_place=1
	local -i slot_num=8
	local -i mount_slot=`expr $slot_num - 1`
	local -i mounted_slot=0
	local -i new_slot=4
	local -i removed_slot=`expr $slot_num - 1`
	local removed_slot_string=`printf "%04d" $removed_slot`
	local result=""
	local dir="/tmp/test$RANDOM"

	mkdir $dir

	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	#save the perfect fsck output first for our future use.
	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT_STANDARD

	create_test_files $slot_num

	#RS_AFTER_RELINK_EXTENT_ALLOC = 1
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_RELINK_EXTENT_ALLOC -N $new_slot $DEVICE

	$FSCK_BIN -fy $DEVICE>/dev/null

	check_volume $mount_slot $dir $LINENO

	# RS_AFTER_RELINK_INODE_ALLOC = 2
	# the initial inode shouldn't be empty for our test.
	result=`echo "stat //inode_alloc:$removed_slot_string"|$DEBUGFS_BIN $DEVICE|grep "Size:"|awk '{print $8}'`
	[ $result != "0" ]
	exit_if_bad $? 0 "The inode alloc for removed slot shouldn't be empty." $LINENO

	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_RELINK_INODE_ALLOC -N $new_slot $DEVICE

	$FSCK_BIN -fy $DEVICE>/dev/null

	check_volume $mount_slot $dir $LINENO

	# after relink inode alloc, the inode stored in inode_alloc:removed_slot
	# should be moved to other groups.
	result=`echo "stat //inode_alloc:$removed_slot_string"|$DEBUGFS_BIN $DEVICE|grep "Size:"|awk '{print $8}'`
	exit_if_bad $result "0" "The inode alloc for removed slot should be empty." $LINENO

	#RS_AFTER_TRUNCATE_ORPHAN = 3
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_TRUNCATE_ORPHAN -N $new_slot $DEVICE

	# the orphan dir is empty, so fsck.ocfs2 should create another one.
	$FSCK_BIN -fy $DEVICE|grep "ORPHAN_DIR_MISSING"
	exit_if_bad $? "0" "The orphan dir should be created." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_TRUNCATE_JOURNAL = 4
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_TRUNCATE_JOURNAL -N $new_slot $DEVICE

	# the journal is truncated, so fsck.ocfs2 should create another one.
	$FSCK_BIN -fy $DEVICE|grep "JOURNAL_FILE_INVALID"
	exit_if_bad $? "0" "The journal file should be created." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_WRITE_SUPER = 5
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_WRITE_SUPER -N $new_slot $DEVICE

	# the function is almost finished, system dir's count problem should be fixed.
	$FSCK_BIN -fy $DEVICE|grep "INODE_COUNT"
	exit_if_bad $? "0" "The inode count should be fixed ." $LINENO

	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors in the second fsck." $LINENO

	# the slot have been removed, we shouldn't mount on the removed slot,
	# so ocfs2 will select another slot to mount the volume.
	mount -t ocfs2 -o preferred_slot=$mount_slot $DEVICE $dir
	mounted_slot=`dmesg|tail -n 1|sed -e 's/^.*slot \(.*\))/\1/'`
	[ $mounted_slot != $mount_slot ]
	exit_if_bad $? "0" "we shouldn't succeed to mount $DEVICE at $dir in slot $mount_slot" $LINENO
	umount $dir

	#RS_AFTER_REMOVE_ONE_SLOT = 6
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_REMOVE_ONE_SLOT -N $new_slot $DEVICE
	
	# all the work for removing 1 slot is done, no error should be found.
	$FSCK_BIN -fy $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors after the fsck." $LINENO

	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT
	diff $FSCK_OUTPUT $FSCK_OUTPUT_STANDARD
	exit_if_bad $? "0" "fsck find errors in the second fsck." $LINENO

	# Now the slot shrink is finished, so we have to recreate the test environment.
	slot_num=8
	dd if=/dev/zero of=$DEVICE bs=4096 count=3
	$MKFS_BIN -b 4K -C 8K -N $slot_num $DEVICE

	#save the perfect fsck output first for our future use.
	$FSCK_BIN -f $DEVICE|sed -e '/slots/ d'>$FSCK_OUTPUT_STANDARD

	create_test_files $slot_num

	#RS_AFTER_MOVE_SOME_REC = 7
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_MOVE_SOME_REC -N $new_slot $DEVICE
	
	$FSCK_BIN -fy $DEVICE|grep "CHAIN_EMPTY"
	exit_if_bad $? "0" "The error 'CHAIN_EMPTY' should be found." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_CHANGE_SUB_ALLOC = 8
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_CHANGE_SUB_ALLOC -N $new_slot $DEVICE
	
	$FSCK_BIN -fy $DEVICE|grep "INODE_SUBALLOC"
	exit_if_bad $? "0" "The error 'INODE_SUBALLOC' should be found." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_LINK_GROUP = 9
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_LINK_GROUP -N $new_slot $DEVICE

	$FSCK_BIN -fy $DEVICE|grep "GROUP_PARENT"
	exit_if_bad $? "0" "The error 'GROUP_PARENT' should be found." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_MOVE_ONE_GROUP = 10
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_MOVE_ONE_GROUP -N $new_slot $DEVICE

	$FSCK_BIN -fy $DEVICE|grep "GROUP_DUPLICATE"
	exit_if_bad $? "0" "The error 'GROUP_DUPLICATE' should be found." $LINENO

	check_volume $mount_slot $dir $LINENO

	#RS_AFTER_EMPTY_JOURNAL
	echo "y"|$TUNEFS_BIN --corrupt $RS_AFTER_EMPTY_JOURNAL -N $new_slot $DEVICE

	# the journal is truncated, so fsck.ocfs2 should create another one.
	$FSCK_BIN -fy $DEVICE|grep "JOURNAL_FILE_INVALID"
	exit_if_bad $? "0" "The journal file should be created." $LINENO

	check_volume $mount_slot $dir $LINENO

	rmdir $dir
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
	"--with-corrupt-src="*)
		CORRUPT_SRC="${1#--with-corrupt-src=}"
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

set_log_file

#from now on all the command and log will be recorded to the logfile.
set -x

patch_tunefs

corrupt_test

exit 0
