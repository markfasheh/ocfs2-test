#!/bin/bash
#
# Copyright (C) 2008 Oracle.  All rights reserved.
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
# Description:  This script will perform a sanity check on quota feature of
#		a quota-supported fs with the help of quota toolsa,mainly aim 
#		at ocfs2(currently support ocfs2 and ext3),
#		following tools will be used:
#
#		/sbin/quotacheck,/sbin/quotaoff,/sbin/quotaon,/usr/bin/quota,
#		/usr/sbin/setquota,/usr/sbin/repquota,/usr/sbin/warnquota
#
# Author:       Tristan Ye (tristan.ye@oracle.com)
#

################################################################################
# Global Variables
################################################################################
. `dirname ${0}`/config.sh

MKFS_BIN=
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
TEE_BIN=`which tee`
RM_BIN=`which rm`
MKDIR_BIN=`which mkdir`
TOUCH_BIN=`which touch`
DD_BIN=`which dd`
FS_TYPE=ocfs2
INTENSITY=medium
DEVICE=
MOUNT_POINT=

USERNAME=`id -un`
GROUPNAME=`id -gn`

SUDO="`which sudo` -u root"

GROUPADD_BIN="`which sudo` -u root /usr/sbin/groupadd"
GROUPDEL_BIN="`which sudo` -u root /usr/sbin/groupdel"
USERADD_BIN="`which sudo` -u root /usr/sbin/useradd"
USERDEL_BIN="`which sudo` -u root /usr/sbin/userdel"
USERMOD_BIN="`which sudo` -u root /usr/sbin/usermod"
CHOWN_BIN=`which chown`
CHMOD_BIN=`which chmod`
SUDO_BIN=`which sudo`

QUOTA_BIN="`which sudo` -u root `which quota`"
QUOTACHECK_BIN="`which sudo` -u root `which quotacheck`"
EDQUOTA_BIN="`which sudo` -u root `which edquota`"
SETQUOTA_BIN="`which sudo` -u root `which setquota`"
QUOTAON_BIN="`which sudo` -u root `which quotaon`"
QUOTAOFF_BIN="`which sudo` -u root `which quotaoff`"

BLOCKSIZE=
CLUSTERSIZE=
SLOTS=
JOURNALSIZE=
BLOCKS=
LABELNAME=ocfs2-quota-tests

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
MKFSLOG=
MOUNTLOG=

SOFT_SPACE_LIMIT=
HARD_SPACE_LIMIT=
SOFT_INODES_LIMIT=
HARD_INODES_LIMIT=

SPACE_GRACE_TIME=
INODE_GRACE_TIME=

USERNUM=

TEST_NO=0
TEST_PASS=0

set -o pipefail

BOOTUP=color
RES_COL=80
MOVE_TO_COL="echo -en \\033[${RES_COL}G"
SETCOLOR_SUCCESS="echo -en \\033[1;32m"
SETCOLOR_FAILURE="echo -en \\033[1;31m"
SETCOLOR_WARNING="echo -en \\033[1;33m"
SETCOLOR_NORMAL="echo -en \\033[0;39m"
################################################################################
# Utility Functions
################################################################################
function f_echo_success()
{
	[ "$BOOTUP" = "color" ] && $MOVE_TO_COL
		echo -n "["
	[ "$BOOTUP" = "color" ] && $SETCOLOR_SUCCESS
		echo -n $" PASS "
	[ "$BOOTUP" = "color" ] && $SETCOLOR_NORMAL
		echo -n "]"

	return 0
}

function f_echo_failure()
{
	[ "$BOOTUP" = "color" ] && $MOVE_TO_COL
		echo -n "["
	[ "$BOOTUP" = "color" ] && $SETCOLOR_FAILURE
		echo -n $"FAILED"
	[ "$BOOTUP" = "color" ] && $SETCOLOR_NORMAL
		echo -n "]"

	return 1
}

function f_echo_status()
{
        if [ "${1}" == "0" ];then
                f_echo_success
                echo
        else
                f_echo_failure
                echo
                exit 1
        fi
}

function f_exit_or_not()
{
        if [ "${1}" != "0" ];then
                exit 1;
        fi
}

function f_usage()
{
	echo "usage: `basename ${0}` <-o logdir> <-d device> [-t fs_type] [ -i intensity] <mountpoint path>"
	echo "       -o output directory for the logs"
	echo "       -d block device name used for ocfs2 volume"
	echo "       -t fs_type,currently support ocfs2 and ext3"
	echo "       -i intensity should be small,medium(default) and large"
	echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
	exit 1;

}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "o:hd:t:i:" options; do
                case $options in
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
                t ) FS_TYPE="$OPTARG";;
		i ) INTENSITY="$OPTARG";;
                h ) f_usage;;
                * ) f_usage;;
                esac
        done
        shift $(($OPTIND -1))
        MOUNT_POINT=${1}

	local -a test_profile_small=(
                1024            # block size
                4096            # cluster size
                4               # number of node solts
                "4M"            # journal
                262144          # blocks count,volume should be 256M
		1024		# space soft limit,1M
		2048		# space hard limit,2M
		300		# inodes soft limit
		600		# inodes hard limit
		20		# space grace time,20s
		60		# inode grace time,60s
		10		# users in one group
		
        );
        # Medium
        local -a test_profile_medium=(
                4096            # block size
                32768           # cluster size
                4               # number of node solts
                "16M"           # journal
                1048576         # blocks count,4G
		1048576		# space soft limit,1G
		2097152		# space hard limit,2G
		40000		# inodes soft limit
		80000		# inodes hard limit
		40		# space grace time,40s
		120		# inodes grace time,120s
		100		# users in one group
        );
        # Large
        local -a test_profile_large=(
                4096            # block size
                131072          # cluster size
                4               # number of node solts
                "64M"           # journal
                4194304         # blocks count,16G
		4194304		# space soft limit,4G
		8388608         # space hard limit,16G
                1000000         # inodes soft limit
                2000000         # inodes hard limit
                80              # space grace time,40s
                240             # inodes grace time,120s
		1000		# users in one group
        );

	local v="test_profile_${INTENSITY}[@]"
        local -a test_profile=("${!v}")

        if [ 0 -eq "${#test_profile[@]}" ]
        then
		f_usage
        fi

	BLOCKSIZE=${test_profile[0]}
	CLUSTERSIZE=${test_profile[1]}
	SLOTS=${test_profile[2]}
	JOURNALSIZE=${test_profile[3]}
	BLOCKS=${test_profile[4]}

	SOFT_SPACE_LIMIT=${test_profile[5]}
	HARD_SPACE_LIMIT=${test_profile[6]}
	SOFT_INODES_LIMIT=${test_profile[7]}
	HARD_INODES_LIMIT=${test_profile[8]}

	SPACE_GRACE_TIME=${test_profile[9]}
	INODE_GRACE_TIME=${test_profile[10]}

	USERNUM=${test_profile[11]}

}

function f_check()
{
	#if [ "${EUID}" != "0" ];then
	#	echo "You have to be root to run quota tests!"
	#	exit 1
	#fi
	
	f_getoptions $*
	
	if [ -z "${MOUNT_POINT}" ];then
                f_usage
        else
                if [ ! -d ${MOUNT_POINT} ]; then
                        echo "Mount point ${MOUNT_POINT} does not exist."
                        exit 1
                else
                        #To assure that mount point will not end with a trailing '/'
                        if [ "`dirname ${MOUNT_POINT}`" = "/" ]; then
                                MOUNT_POINT="`dirname ${MOUNT_POINT}``basename ${MOUNT_POINT}`"
                        else
                                MOUNT_POINT="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`"
                        fi
                fi
        fi
	
	if [ "${FS_TYPE}" != "ocfs2" -a "${FS_TYPE}" != "ext3" ];then
		echo "Currently,tests only support ocfs2 and ext3!"
		exit 1
	fi

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}

        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-quota-tests-run.log"
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-quota-tests.log"
	MKFSLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mkfs.log"
	MOUNTLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mount.log"

	if [ "${FS_TYPE}" = "ocfs2" ];then
		MKFS_BIN="`which sudo` -u root /sbin/mkfs.ocfs2"
        else
		MKFS_BIN="`which sudo` -u root /sbin/mkfs.ext3"
	fi
}

function f_LogRunMsg()
{
	echo -ne "$@"| ${TEE_BIN} -a ${RUN_LOG_FILE}
}

function f_LogMsg()
{
	echo "$(date +%Y/%m/%d,%H:%M:%S)  $@" >>${LOG_FILE}
}

function f_mkfs_and_mount()
{
	f_LogMsg "Mkfs and mount volume before test"
	if [ "${FS_TYPE}" = "ocfs2" ];then
		f_LogRunMsg "Mkfsing target volume as ${FS_TYPE} with -b ${BLOCKSIZE} -C ${CLUSTERSIZE}:"
		echo "y"| ${MKFS_BIN} --fs-features=usrquota,grpquota -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -L ${LABELNAME} -N ${SLOTS} -J "size=${JOURNALSIZE}" ${DEVICE} ${BLOCKS}>>${MKFSLOG} 2>&1
		RET=$?
		f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	        f_exit_or_not ${RET}

		f_LogRunMsg "Mounting ${DEVICE} to ${MOUNT_POINT}:"
		${MOUNT_BIN} -t ${FS_TYPE} -o rw,usrquota,grpquota ${DEVICE} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
		RET=$?
		f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
		f_exit_or_not ${RET}
	else
		f_LogRunMsg "Mkfsing target volume as ${FS_TYPE} with -b ${BLOCKSIZE}:"
		${MKFS_BIN} -b ${BLOCKSIZE} -L ${LABELNAME} ${DEVICE} -F >>${MKFSLOG} 2>&1
		RET=$?
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
		f_exit_or_not ${RET}

		f_LogRunMsg "Mounting ${DEVICE} to ${MOUNT_POINT}:"
		${MOUNT_BIN} -t ${FS_TYPE} -o rw,usrquota,grpquota ${DEVICE} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
		RET=$?
		f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
		f_exit_or_not ${RET}
	fi
	
	f_LogMsg "Chmod ${MOUNT_POINT} as 777"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	f_exit_or_not $?
}

function f_quotacheck()
{
	f_LogMsg "Quotacheck for testing:"
	${QUOTACHECK_BIN} -agumf  >>${LOG_FILE} 2>&1
	f_exit_or_not $?
}

function f_one_user()
{
	#one user
	local USERNAME=quotauser
	local WORKPLACE=${MOUNT_POINT}/${USERNAME}
	local TEST_INODE_PREFIX=${WORKPLACE}/quota-${USERNAME}-inode-
	local TEST_DD_FILE_PREFIX=${WORKPLACE}/quota-${USERNAME}-dd-testfile
	local -i i
	local consumed_inodes

	((TEST_NO++))
	f_LogRunMsg "<${TEST_NO}> One user test:\n"
        f_LogMsg "Test ${TEST_NO}:One User Test."
	f_LogMsg "Add one user ${USERNAME}"
	${USERADD_BIN} -m ${USERNAME}
	${MKDIR_BIN} -p ${WORKPLACE}
	${CHMOD_BIN} -R 777 ${WORKPLACE}
	f_exit_or_not $?

	if [ "${FS_TYPE}" = "ext3" ];then
		f_quotacheck
	fi

	f_LogMsg "set inode quota(SPACE_SOFT:${SOFT_SPACE_LIMIT},SPACE_HARD:${HARD_SPACE_LIMIT},INODE_SOFT:${SOFT_INODES_LIMIT},INODE_HARD:${HARD_INODES_LIMIT}) for user ${USERNAME}:"
	${SETQUOTA_BIN} -u ${USERNAME} ${SOFT_SPACE_LIMIT} ${HARD_SPACE_LIMIT} ${SOFT_INODES_LIMIT} ${HARD_INODES_LIMIT} -a ${DEVICE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "attempt to touch soft limit number(${SOFT_INODES_LIMIT}) of inodes"
	for i in $(seq ${SOFT_INODES_LIMIT});do

		f_LogMsg "touch file quota-${USERNAME}-inode-${i}:"
		${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${TEST_INODE_PREFIX}${i} >>${LOG_FILE} 2>&1
		consumed_inodes=`${QUOTA_BIN} -u ${USERNAME}|grep -v File|grep -v Disk|awk '{print $5}'`
		if [ "$consumed_inodes" != "$i" ];then
			f_LogMsg "wrong quota inodes calculated!"
		fi
	done
	
	f_LogMsg "attempt to exceed the soft limit of inodes"

	for i in $(seq $((${SOFT_INODES_LIMIT}+1)) ${HARD_INODES_LIMIT}); do
		f_LogMsg "touch file quota-${USERNAME}-inode-${i}:"
		#${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${MOUNT_POINT}/quota-${USERNAME}-inode-${i} 2>&1 >>${LOG_FILE}
		${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${TEST_INODE_PREFIX}${i} >>${LOG_FILE} 2>&1
		consumed_inodes=`${QUOTA_BIN} -u ${USERNAME}|grep -v File|grep -v Disk|awk '{print $5}'`
		if [ "$consumed_inodes" != "${i}*" ];then
                        f_LogMsg "wrong quota inodes calculated!"
                fi

	done

	f_LogMsg "attempt to exceed the hard limit of inodes"
	((i++))
	${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${TEST_INODE_PREFIX}${i} >> ${LOG_FILE} 2>&1

	if [ "$?" = "0" ];then
		f_LogMsg "should not exceed the hard limit of inodes"
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
		return -1
	fi

	f_LogMsg "attmpt to test grace time of inodes limit"
	
	for i in $(seq $((${SOFT_INODES_LIMIT}+2)) ${HARD_INODES_LIMIT}); do
		f_LogMsg "remove file quota-${USERNAME}-inode-${i}:"
		${RM_BIN}  ${TEST_INODE_PREFIX}${i} >>${LOG_FILE} 2>&1
	done

	local start_sec=$SECONDS
	local end_sec
	i=$((${SOFT_INODES_LIMIT}+2))
	f_LogMsg "set grace time of inodes as 60000 ${INODE_GRACE_TIME}"
	${SETQUOTA_BIN} -u ${USERNAME} -T 60000 ${INODE_GRACE_TIME} -a ${DEVICE}

	while [ $SECONDS -lt  $((${start_sec}+${INODE_GRACE_TIME})) ];do
		f_LogMsg "touch file quota-${USERNAME}-inode-${i}:"
		${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${TEST_INODE_PREFIX}${i} >>${LOG_FILE} 2>&1
		((i++))
		sleep 1
	done

	f_LogMsg "${INODE_GRACE_TIME} seconds of grace time been runned out"
	${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${TEST_INODE_PREFIX}${i} >>${LOG_FILE} 2>&1
	if [ "$?" = "0" ];then
		f_LogMsg "should not touch a file after grace time running out"
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return
	fi

	f_LogMsg "remove all testing inodes"
	${RM_BIN} -rf ${TEST_INODE_PREFIX}* >>${LOG_FILE} 2>&1

	f_LogMsg "dd file to consume the soft limit space by user ${USERNAME}"
	${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-1 bs=1024 count=${SOFT_SPACE_LIMIT}>>${LOG_FILE} 2>&1
	f_LogMsg "dd file to consume the hard limit space by user ${USERNAME}"
	${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-2 bs=1024 count=$((${HARD_SPACE_LIMIT}-${SOFT_SPACE_LIMIT}))>>${LOG_FILE} 2>&1

	f_LogMsg "try to exceed 1 byte than space hard limit"
	${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-3 bs=1 count=1>>${LOG_FILE} 2>&1
	if [ "$?" = "0" ];then
		f_LogMsg "should not exceed the hard limit of space"
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
		return -1
	fi

	f_LogMsg "attempt to test grace time for space limit"
	${RM_BIN} -rf ${TEST_DD_FILE_PREFIX}-2 ${TEST_DD_FILE_PREFIX}-3

	f_LogMsg "attempt to exceed the soft limit by adding one 1-byte file."
	${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-2 bs=1 count=1>>${LOG_FILE} 2>&1

	f_LogMsg "attempt to set grace time for space limit as ${SPACE_GRACE_TIME} 60000"

	start_sec=$SECONDS
	i=3
	${SETQUOTA_BIN} -u ${USERNAME} -T ${SPACE_GRACE_TIME} 60000 -a ${DEVICE}

	while [ $SECONDS -lt  $((${start_sec}+${SPACE_GRACE_TIME})) ];do
		f_LogMsg "dd file ${TEST_DD_FILE_PREFIX}-${i}:"
		${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-${i} bs=1024 count=1>>${LOG_FILE} 2>&1
                ((i++))
                sleep 1
	done

	f_LogMsg "${SPACE_GRACE_TIME} seconds of grace time been runned out"
	${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${TEST_DD_FILE_PREFIX}-${i} bs=1024 count=1>>${LOG_FILE} 2>&1
        if [ "$?" = "0" ];then
                f_LogMsg "should not dd a file after grace time running out"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return
        fi

	${RM_BIN} -rf ${TEST_DD_FILE_PREFIX}-*
	${RM_BIN} -rf ${WORKPLACE}
	
	f_LogMsg "Remove one user ${USERNAME}"
	${USERDEL_BIN} -r ${USERNAME}>>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Remove all testing files for user ${USERNAME}"
        ${RM_BIN} -rf ${WORKPLACE} >>${LOG_FILE} 2>&1
        f_exit_or_not $?


	RET=$?
	f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))

	return 0
}

function f_group_test()
{
	#one group
        local GROUPNAME=quotagroup-${1}
        local USERNAMEPREFIX=${GROUPNAME}-quotauser-
	local USERNAME=
	local TEST_INODE=
	local TEST_DD_FILE=
	local -i i
	local -i user_index
        local WORKPLACE=${MOUNT_POINT}/${GROUPNAME}
	local CLUSTERSIZE_K=$((${CLUSTERSIZE}/1024))

        f_LogMsg "Add one group ${GROUPNAME}"
        
        ${GROUPADD_BIN} ${GROUPNAME}>>${LOG_FILE} 2>&1
	f_exit_or_not $?
        ${MKDIR_BIN} -p ${WORKPLACE}
	
	f_LogMsg "Add ${USERNUM} users for group ${group}"
	
	for i in $(seq ${USERNUM});do
		USERNAME=${USERNAMEPREFIX}${i}
		${USERADD_BIN} -m -g ${GROUPNAME} ${USERNAME} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
		${MKDIR_BIN} -p ${WORKPLACE}/${USERNAME}
	done

	${CHMOD_BIN} -R 777 ${WORKPLACE}

	#start to perfrom test
	if [ "${FS_TYPE}" = "ext3" ];then
                f_quotacheck
        fi

	f_LogMsg "set inode quota(SPACE_SOFT:${SOFT_SPACE_LIMIT},SPACE_HARD:${HARD_SPACE_LIMIT},INODE_SOFT:${SOFT_INODES_LIMIT},INODE_HARD:${HARD_INODES_LIMIT}) for group ${GROUPNAME}:"
        ${SETQUOTA_BIN} -g ${GROUPNAME} ${SOFT_SPACE_LIMIT} ${HARD_SPACE_LIMIT} ${SOFT_INODES_LIMIT} ${HARD_INODES_LIMIT} -a ${DEVICE} >>${LOG_FILE} 2>&1
        f_exit_or_not $?
	
	f_LogMsg "attempt to touch soft limit number(${SOFT_INODES_LIMIT}) of inodes"
	for i in $(seq ${SOFT_INODES_LIMIT});do
		#all users have chance to touch file in turn
		user_index=$((${i}%${USERNUM}+1))
		USERNAME=${USERNAMEPREFIX}${user_index}
		TEST_INODE=quota-${USERNAME}-inode-${i}
                f_LogMsg "touch file ${TEST_INODE} under ${WORKPLACE}/${USERNAME}"
                ${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >>${LOG_FILE} 2>&1
                consumed_inodes=`${QUOTA_BIN} -g ${GROUPNAME}|grep -v File|grep -v Disk|awk '{print $5}'`
                if [ "$consumed_inodes" != "$i" ];then
                        f_LogMsg "wrong quota inodes calculated!"
                fi
        done
	
	f_LogMsg "attempt to exceed the soft limit of inodes to hard limit(${HARD_INODES_LIMIT})"
	for i in $(seq $((${SOFT_INODES_LIMIT}+1)) ${HARD_INODES_LIMIT});do
                #all users have chance to touch file in turn
                user_index=$((${i}%${USERNUM}+1))
                USERNAME=${USERNAMEPREFIX}${user_index}
                TEST_INODE=quota-${USERNAME}-inode-${i}
                f_LogMsg "touch file ${TEST_INODE} under ${WORKPLACE}/${USERNAME}"
                ${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >>${LOG_FILE} 2>&1
                consumed_inodes=`${QUOTA_BIN} -g ${GROUPNAME}|grep -v File|grep -v Disk|awk '{print $5}'`
		if [ "$consumed_inodes" != "${i}*" ];then
                        f_LogMsg "wrong quota inodes calculated!"
                fi
        done

	f_LogMsg "attempt to exceed the hard limit of inodes"
        ((i++))
	TEST_INODE=quota-${USERNAME}-inode-${i}
	${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >> ${LOG_FILE} 2>&1

        if [ "$?" = "0" ];then
                f_LogMsg "should not exceed the hard limit of inodes"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return -1
        fi

	f_LogMsg "attmpt to test grace time of inodes limit"

	for i in $(seq $((${SOFT_INODES_LIMIT}+2)) ${HARD_INODES_LIMIT}); do
		user_index=$((${i}%${USERNUM}+1))
		USERNAME=${USERNAMEPREFIX}${user_index}
		TEST_INODE=quota-${USERNAME}-inode-${i}
                f_LogMsg "remove file ${TEST_INODE} from ${WORKPLACE}/${USERNAME}"
                ${RM_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >>${LOG_FILE} 2>&1
        done

	local start_sec=$SECONDS
        local end_sec

	i=$((${SOFT_INODES_LIMIT}+2))
        f_LogMsg "set grace time of inodes as 60000 ${INODE_GRACE_TIME}"
        ${SETQUOTA_BIN} -g ${GROUPNAME} -T 60000 ${INODE_GRACE_TIME} -a ${DEVICE}

	while [ $SECONDS -lt  $((${start_sec}+${INODE_GRACE_TIME})) ];do
		user_index=$((${i}%${USERNUM}+1))
		USERNAME=${USERNAMEPREFIX}${user_index}
		TEST_INODE=quota-${USERNAME}-inode-${i}
                f_LogMsg "touch file ${TEST_INODE}:"
                ${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >>${LOG_FILE} 2>&1
                ((i++))
                sleep 1
        done

	f_LogMsg "${INODE_GRACE_TIME} seconds of grace time been runned out"
	user_index=$((${i}%${USERNUM}+1))
	USERNAME=${USERNAMEPREFIX}${user_index}
	TEST_INODE=quota-${USERNAME}-inode-${i}
	${SUDO_BIN} -u ${USERNAME} ${TOUCH_BIN} ${WORKPLACE}/${USERNAME}/${TEST_INODE} >>${LOG_FILE} 2>&1
        if [ "$?" = "0" ];then
                f_LogMsg "should not touch a file after grace time running out"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return ${RET}
        fi

	f_LogMsg "set grace time of inodes back 60000 60000"
        ${SETQUOTA_BIN} -g ${GROUPNAME} -T 60000 60000 -a ${DEVICE}

	f_LogMsg "remove all testing inodes"
        ${RM_BIN} -rf ${WORKPLACE}/* >>${LOG_FILE} 2>&1

	f_LogMsg "dd file to consume the soft limit space by group ${GROUPNAME}"
	for i in $(seq $((${SOFT_SPACE_LIMIT}/${CLUSTERSIZE_K})));do
		user_index=$((${i}%${USERNUM}+1))
		USERNAME=${USERNAMEPREFIX}${user_index}
		TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
		${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${WORKPLACE}/${TEST_DD_FILE} bs=${CLUSTERSIZE} count=1>>${LOG_FILE} 2>&1
	done

	f_LogMsg "dd file to consume the hard limit space by user ${USERNAME}"
	for i in $(seq $((${SOFT_SPACE_LIMIT}/${CLUSTERSIZE_K}+1)) $((${HARD_SPACE_LIMIT}/${CLUSTERSIZE_K})));do
                user_index=$((${i}%${USERNUM}+1))
                USERNAME=${USERNAMEPREFIX}${user_index}
                TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
                ${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${WORKPLACE}/${TEST_DD_FILE} bs=${CLUSTERSIZE} count=1>>${LOG_FILE} 2>&1
        done

	f_LogMsg "try to exceed 1 byte than space hard limit"
	((i++))
	user_index=$((${i}%${USERNUM}+1))
	USERNAME=${USERNAMEPREFIX}${user_index}
	TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
        ${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${WORKPLACE}/${TEST_DD_FILE} bs=1 count=1>>${LOG_FILE} 2>&1
        if [ "$?" = "0" ];then
                f_LogMsg "should not exceed the hard limit of space"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return
        fi

	f_LogMsg "attempt to test grace time for space limit"
	for i in $(seq $((${SOFT_SPACE_LIMIT}/${CLUSTERSIZE_K}+2)) $((${HARD_SPACE_LIMIT}/${CLUSTERSIZE_K})));do
		user_index=$((${i}%${USERNUM}+1))
                USERNAME=${USERNAMEPREFIX}${user_index}
                TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
		f_LogMsg "remove dd file ${TEST_DD_FILE} from ${WORKPLACE}"
                ${RM_BIN} ${WORKPLACE}/${TEST_DD_FILE} >>${LOG_FILE} 2>&1
	done

        f_LogMsg "attempt to set grace time for space limit as ${SPACE_GRACE_TIME} 60000"
	start_sec=$SECONDS
        i=$((${SOFT_SPACE_LIMIT}/${CLUSTERSIZE_K}+2))
	${SETQUOTA_BIN} -g ${GROUPNAME} -T ${SPACE_GRACE_TIME} 60000 -a ${DEVICE}

	while [ $SECONDS -lt  $((${start_sec}+${SPACE_GRACE_TIME})) ];do
                user_index=$((${i}%${USERNUM}+1))
                USERNAME=${USERNAMEPREFIX}${user_index}
                TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
                f_LogMsg "dd file ${TEST_INODE}:"
                ${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${WORKPLACE}/${TEST_DD_FILE} bs=${CLUSTERSIZE} count=1>>${LOG_FILE} 2>&1
                ((i++))
                sleep 1
        done

	f_LogMsg "${SPACE_GRACE_TIME} seconds of grace time been runned out"
	user_index=$((${i}%${USERNUM}+1))
	USERNAME=${USERNAMEPREFIX}${user_index}
	TEST_DD_FILE=quota-${USERNAME}-dd-file-${i}
        ${SUDO_BIN} -u ${USERNAME} ${DD_BIN} if=/dev/zero of=${WORKPLACE}/${TEST_DD_FILE} bs=${CLUSTERSIZE} count=1>>${LOG_FILE} 2>&1
        if [ "$?" = "0" ];then
                f_LogMsg "should not dd a file after grace time running out"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return
        fi

	f_LogMsg "Remove group ${GROUPNAME} and all related users"
	for i in $(seq ${USERNUM});do
		${USERDEL_BIN} -r ${USERNAMEPREFIX}${i} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	${GROUPDEL_BIN} ${GROUPNAME} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Remove all testing files for group ${GROUPNAME}"
        ${RM_BIN} -rf ${WORKPLACE} >>${LOG_FILE} 2>&1
        f_exit_or_not $?

	return 0
}

function f_one_group()
{
	((TEST_NO++))
        f_LogRunMsg "<${TEST_NO}> One group test:\n"
        f_LogMsg "Test ${TEST_NO}:One Group Test."

        f_group_test 1

        RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
        ((TEST_PASS++))


}

function f_multi_groups()
{
	((TEST_NO++))
        f_LogRunMsg "<${TEST_NO}> Multiple groups test:\n"
        f_LogMsg "Test ${TEST_NO}:One Group Test."

        local -i i

        for i in $(seq 2 10);do
                f_group_test ${i} || break
        done

        RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
        ((TEST_PASS++))
}


function f_sanity_checker()
{
	f_LogRunMsg "Sanity check for quota on ocfs2:\n"
	f_one_user
	f_one_group
	f_multi_groups
}

function f_runtest()
{
	f_LogMsg "Turn on quota:"
        ${QUOTAON_BIN} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
        f_exit_or_not $?

	f_sanity_checker

	f_LogMsg "Turn off quota:"
        ${QUOTAOFF_BIN} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
        f_exit_or_not $?

}

function f_umount()
{
	f_LogMsg "Umount volume after test done"
	f_LogRunMsg "Umounting volume ${DEVICE} from ${MOUNT_POINT}:"
	${UMOUNT_BIN} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
	RET=$?
	f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
}
function f_cleanup()
{
	:
}


################################################################################
# Main Entry
################################################################################

#redfine the int signal hander
trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT

f_check $*

START_TIME=${SECONDS}
f_LogRunMsg "=====================Quota testing starts on ${FS_TYPE}:  `date`=====================\n"
f_LogMsg "=====================Quota testing starts on ${FS_TYPE}:  `date`====================="

f_mkfs_and_mount
f_runtest
f_umount

END_TIME=${SECONDS}
f_LogRunMsg "=====================Quota testing ends on ${FS_TYPE}: `date`=====================\n"
f_LogMsg "=====================Quota testing ends on ${FS_TYPE}: `date`====================="

f_LogRunMsg "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg "Tests total: ${TEST_NO}\n"
f_LogRunMsg "Tests passed: ${TEST_PASS}\n"
