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

#
# This script will check the whole mechanism of backup super in ocfs2-tools.
#
# The whole test script works like this:
# 1. Iterate all the block size and cluster size and do the normal test.
# 2. The normal test will calucate out some boudary volume size and test
#    whether mkfs.ocfs2, fsck.ocfs2, debugfs.ocfs2 and tunefs.ocfs2 can
#    work suitable with backup superblocks. It will create an ocfs2 
#    volume, corrupt the super block and recover it using the specified
#    backup superblock.
# 3. It also includes another test which will format a volume with no backup
#    superblocks and use tunefs.ocfs2 to add them to test whether tunefs.ocfs2
#    can work properly for old-formatted volumes.
#
# In order to make the test runs, the volume must have at least +1G size to
# have a space for a backup superblock since the first backup super block
# will resides at 1G byte offset.
#

MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
FSCK_BIN="`which sudo` -u root `which fsck.ocfs2`"
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
TUNEFS_BIN="`which sudo` -u root `which tunefs.ocfs2`"
DD_BIN="`which sudo` -u root `which dd`"
LOG_DIR=$PWD

BLOCKDEV="`which sudo` -u root `which blockdev`"
DEVICE=""
LOGFILE=""
FIRST_BACKUP_OFF=1073741824	#1G
MAX_NUM=6

blocksize=
clustersize=

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
	      --with-debugfs=PROGRAM        use the PROGRAM as mkfs.ocfs2
	      --with-tunefs=PROGRAM        use the PROGRAM as tunefs.ocfs2
	      --block-size=blocksize       block size
	      --cluster-size=clustersize   cluster size

	Examples:

	  $script --with-debugfs=../debugfs.ocfs2/debugfs.ocfs2 --block-size=4096 --clustersize=32768 /dev/sde2
	  $script --with-mkfs=/sbin/mkfs.ocfs2 --log-dir=/tmp --block-size=4096 --clustersize=32768 /dev/sde2
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
	echo "$script: $@" |tee -a ${LOGFILE}
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
	if [ ! -d $LOG_DIR ]; then
		echo "log_dir[$LOG_DIR] not exist, use [$PWD] instead."
		LOG_DIR=$PWD
	fi
	
	LOGFILE="$LOG_DIR/`date +%F-%H-%M-%S`-backup_super_test_output.log"
}

#clear all the backup blocks in the device
function clear_backup_blocks()
{
	backup_off=$FIRST_BACKUP_OFF

	while [ `expr $backup_off + 512` -le `expr $byte_total` ];
	do
		#clear the last blocksize
		seek_block=`expr $backup_off / 512`
		${DD_BIN} if=/dev/zero of=$DEVICE bs=512 count=1 seek=$seek_block

		backup_off=`expr $backup_off \* 4`
	done
}

##################################
#
# Test mkfs and debugfs.
# The volume size will be smaller than 1G, 1G and larger than 1G
# to see whether it works. The same goes with 4G, 16G...
#
##################################
function test_mkfs()
{
	echo "testing mkfs.ocfs2..." |tee -a ${LOGFILE}
	#mkfs and debugfs test.
	#vol_byte_size is always set to be one of the backup
	#superblock offset, say 1G, 4G, 16G...
	#see the main loop for more details.

	#in order to speed up the process of mkfs,
	#we empty the first block first.
	#we also need to clear all the backup blocks in the device
	#in case they are written by prevoius format.
	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	msg="debugfs shouldn't be sucess"
	msg1="debugfs should be sucess"

	blkcount=`expr $vol_byte_size / $blocksize`
	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M \
 --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount
	#first check whether mkfs is success
	echo "ls //"|${DEBUGFS_BIN} ${DEVICE}|grep global_bitmap
	exit_if_bad $? 0 $msg $LINENO

	#this time there is the block represented by the last_backup_num isn't
	#in the volume, so we can't open the device by the num.
	echo "ls //"|${DEBUGFS_BIN} ${DEVICE} -s $last_backup_num|grep global_bitmap
	exit_if_bad $? 1 $msg1 $LINENO

	#increase the blkcount so that the blocks represented by the last_backup_num
	#can be used to store the backup block.
	bpc=`expr $clustersize / $blocksize`
	blkcount=`expr $blkcount + $bpc`

	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks
	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M \
		--cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount
	#first check whether mkfs is success
	echo "ls //"|${DEBUGFS_BIN} ${DEVICE}|grep global_bitmap
	exit_if_bad $? 0 $msg1 $LINENO

	#check whether all the backup blocks including last_backup_num
	#can be opened.
	#i=1
	#while [ `expr $i` -le `expr $last_backup_num` ];
	for((i=1;$i<=$last_backup_num;i++))
	do
		cmd="${DEBUGFS_BIN} ${DEVICE} -s $i"
		echo "ls //"|$cmd|grep global_bitmap
		exit_if_bad $? 0 $msg1 $LINENO
		echo $cmd " is ok." |tee -a ${LOGFILE}
	#	i=`expr $i + 1`
	done
}

##################################
#
#test whether "fsck.ocfs2 -r" can recover the volume
#
##################################
function test_fsck()
{
	echo "testing fsck.ocfs2..." |tee -a ${LOGFILE}

	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M \
 --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount
	#corrupt the superblock
	${DD_BIN} if=/dev/zero of=${DEVICE} bs=$blocksize count=3
	${FSCK_BIN} -fy ${DEVICE}	#This should failed.
	exit_if_bad $? 8 "fsck.ocfs2" $LINENO

	#recover the superblock
	cmd="${FSCK_BIN} -y -r $last_backup_num ${DEVICE}"
	$cmd
	exit_if_bad $? 0 "fsck.ocfs2" $LINENO
	echo $cmd "is ok" |tee -a ${LOGFILE}

	#go on the normal process to see whether the recovery is sucess.
	${FSCK_BIN} -fy ${DEVICE}
	exit_if_bad $? 0 "fsck.ocfs2" $LINENO
}

##################################
#
#test whether tunefs will add new backup superblocks during resizing
#
##################################
function test_tunefs_resize()
{
	echo "test tunefs resize..." |tee -a ${LOGFILE}

	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	#mkfs a volume with no backup superblock
	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M \
		    --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount

	local bpc=`expr $clustersize / $blocksize`
	local blkcount=`expr $blkcount + $bpc`

	#we can't open it by the last_backup_num now.
	cmd="${DEBUGFS_BIN} ${DEVICE} -s $last_backup_num"
	echo "ls //"|$cmd|grep global_bitmap
	exit_if_bad $? 1 "tunefs.ocfs2" $LINENO

	#tunefs a volume to add a cluster which will hold a backup superblock.
	cmd="${TUNEFS_BIN} -S ${DEVICE} $blkcount"
	echo "y"|$cmd
	exit_if_bad $? 0 "tunefs.ocfs2" $LINENO
	echo $cmd "is ok" |tee -a ${LOGFILE}

	#test whether the new backup superblock works.
	cmd="${DEBUGFS_BIN} ${DEVICE} -s $last_backup_num"
	echo "ls //"|$cmd|grep global_bitmap
	exit_if_bad $? 0 "tunefs.ocfs2" $LINENO	#we can open with a backup block
	echo $cmd "is ok." |tee -a ${LOGFILE}
}

##################################
#
#test whether tunefs will add backup superblocks for an old ocfs2 volume
#
##################################
function test_tunefs_add_backup()
{
	echo "test tunefs and backup..." |tee -a ${LOGFILE}

	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	#mkfs a volume with no backup superblock supported
	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M --no-backup-super \
 --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount

	#We can't open the volume by backup superblock now
	echo "ls //"|${DEBUGFS_BIN} ${DEVICE} -s 1|grep global_bitmap
	exit_if_bad $? 1 "tunefs.ocfs2" $LINENO

	#tunefs a volume to add backup superblocks
	cmd="${TUNEFS_BIN} --backup-super ${DEVICE}"
	echo "y"|$cmd
	exit_if_bad $? 0 "tunefs.ocfs2" LINENO
	echo $cmd "is ok" |tee -a ${LOGFILE}

	#We can open the volume now with a backup block
	echo "ls //"|${DEBUGFS_BIN} ${DEVICE} -s 1|grep global_bitmap
	exit_if_bad $? 0 "tunefs.ocfs2" $LINENO
}

function check_vol()
{
	fsck_result=`${DEBUGFS_BIN} -R "stats" ${DEVICE}|grep Label`
	label_name=`echo $fsck_result | awk '{print $2}'`

	echo "label=$label_name, wanted=$1"
	if [ $label_name != $1 ]; then
		echo "check volume name [$1]failed" |tee -a ${LOGFILE}
		exit 1
	fi
}

##################################
#
# test whether tunefs will refresh backup block accordingly when the superblock
# has been updated.
#
##################################
function test_tunefs_refresh()
{
	echo "test tunefs refresh..." |tee -a ${LOGFILE}

	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	local old_vol_name="old_ocfs2"
	local new_vol_name="new_ocfs2"
	echo "y" |${MKFS_BIN} -b $blocksize -C $clustersize -N 4  -J size=64M -L $old_vol_name \
		--cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} ${DEVICE} $blkcount
	check_vol $old_vol_name

	#change the volume name
	echo "y"|${TUNEFS_BIN} -L $new_vol_name ${DEVICE}
	#corrupt the superblock
	${DD_BIN} if=/dev/zero of=${DEVICE} bs=$blocksize count=3
	cmd="${FSCK_BIN} -fy -r $last_backup_num ${DEVICE}"
	echo "y"|$cmd
	exit_if_bad $? 0 $cmd $LINENO
	echo $cmd " is ok" |tee -a ${LOGFILE}
	#check whether the recover superblock has the new volume name
	check_vol $new_vol_name
}

##################################
#
# Test whether tunefs would return fail if a old-formated volume has no space
# for even one backup superblock(less than 1G size) and then a user wants to
# add a backup superblock for it.
#
##################################
function volume_small_test()
{
	${DD_BIN} if=/dev/zero of=$DEVICE bs=4096 count=3
	clear_backup_blocks

	#generate a tmp vol size which is less than 1G

	local tmp_vol_size=`expr $RANDOM \* $FIRST_BACKUP_OFF / 32767`
	local tmp_block_count=`expr $tmp_vol_size / 1024`
	#If block count is too small, mkfs will failed.
	if [ `expr $tmp_block_count` -lt 20000 ]; then
		tmp_block_count=20000
	fi

	# Since tunefs will return 0, we need to grep
	# the output of stderr and find what we want.
	echo "y" |${MKFS_BIN} -b 1K -C 4K ${DEVICE} -N 4 --no-backup-super $tmp_block_count \
 --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME}
	err=`${TUNEFS_BIN} --backup-super ${DEVICE} 2>&1`
	echo $err|grep "too small to contain backup superblocks"
	exit_if_bad $? 0 "tunefs.ocfs2" $LINENO
}


##################################
#
# Backup superblock test. We will make the volume smaller than 1G, 1G and
# larger than 1G and use debugfs to ensure that the backup super was created (or not).
# The same step will goes to 4G, 16G, etc, till the size of the volume. 
#
##################################
function normal_test()
{
	if [ "$blocksize" != "NONE" ];then
		bslist="$blocksize"
	else
		bslist="512 4096"
	fi
	if [ "$clustersize" != "NONE" ];then
		cslist="$clustersize"
	else
		cslist="4096 32768 1048576"
	fi
	for blocksize in $(echo "$bslist")
	do
		for clustersize in \
			$(echo "$cslist")
		do

			vol_byte_size=$FIRST_BACKUP_OFF
			#last_backup_num is set according to the vol_byte_size
			#for 1 for 1G, and 2 for 4G, 3 for 16G etc.
			last_backup_num=1

			while [ `expr $vol_byte_size` -le `expr $byte_total` ] ;
			do
				echo "vol_size = $vol_byte_size, blocksize = $blocksize,"	\
				     " clustersize = $clustersize" |tee -a ${LOGFILE}

				bpc=`expr $clustersize / $blocksize`

				test_mkfs

				#for fsck, we must increase vol_byte_size to include
				#the backup block.
				blkcount=`expr $vol_byte_size / $blocksize + $bpc`
				test_fsck
	
				#for resize, the initial blkcount should not include
				#the backup block.
				blkcount=`expr $vol_byte_size / $blocksize`
				test_tunefs_resize
	
				#for add backup blocks, we must increase vol_byte_size
				#to include the backup block.
				blkcount=`expr $vol_byte_size / $blocksize + $bpc`
				test_tunefs_add_backup
	
				#for refresh backup blocks, we must increase
				#vol_byte_size to include the backup block.
				blkcount=`expr $vol_byte_size / $blocksize + $bpc`
				test_tunefs_refresh
	
			        vol_byte_size=`expr $vol_byte_size \* 4`
				last_backup_num=`expr $last_backup_num + 1`
	
				if [ `expr $last_backup_num` -gt `expr $MAX_NUM` ]; then
					break
				fi
			done
		done
	done
}

################################################################

#
# main
#
. `dirname ${0}`/config.sh
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
	"--cluster-stack="*)
		CLUSTER_STACK="${1#--cluster-stack=}"
		;;
	"--cluster-name="*)
		CLUSTER_NAME="${1#--cluster-name=}"
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
	"--with-tunefs="*)
		TUNEFS_BIN="${1#--with-tunefs=}"
		;;
	"--block-size="*)
		blocksize="${1#--block-size=}"
		;;
	"--cluster-size="*)
		clustersize="${1#--cluster-size=}"
		;;
	*)
		DEVICE="$1"
		;;
	esac
	shift
done

if [ ! -b "${DEVICE}" ]; then
	echo "invalid block device - $DEVICE"
	usage
	exit 1
fi

sect_total=`${BLOCKDEV} --getsize $DEVICE`
byte_total=`expr $sect_total \* 512`

# We must have at least 1 cluster above the FIRST_BACKUP_OFF for our test.
min_vol_size=`expr $FIRST_BACKUP_OFF + 1048576`
if [ `expr $byte_total` -lt `expr $min_vol_size` ]; then
	echo "$DEVICE is too small for our test. We need $min_vol_size at least"
	exit 1
fi

set_log_file

#from now on all the command and log will be recorded to the logfile.

normal_test

volume_small_test

exit 0
