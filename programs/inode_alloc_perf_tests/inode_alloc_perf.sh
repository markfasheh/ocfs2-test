#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# inode_alloc_perf.sh
#
# Description:  The script helps to run a performance comparison tests between 
# 		a original and a enhanced(with inode alloc patches) kernel on 
#		inode allocation, deletion and traversing etc.
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      20 Jan 2009
#
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


################################################################################
# Global Variables
################################################################################

PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.

. `dirname ${0}`/config.sh

SUDO="`which sudo` -u root"
RM_BIN="`which rm`"
TEE_BIN=`which tee`
MKDIR_BIN="`which mkdir`"
TOUCH_BIN="`which touch`"
TAR_BIN="`which tar`"
LS_BIN="`which ls --skip-alias`"
LI_BIN="$LS_BIN"
TIME_BIN="`which time`"

CHOWN_BIN=`which chown`
CHMOD_BIN=`which chmod`

MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"

MOUNT_POINT=
WORK_PLACE=
DEVICE=
KERNEL_PATH=
KERNEL_TARBALL=
PATCH_PATH=
ISCSI_SERVER=
ITERATION=1

TAR_NUM=1
TAR_ARGS=

DISK_SIZE=
DISK_SIZE_M=
DISK_FREE_M=

TAR_SIZE=
TAR_SIZE_M=

BLOCKSIZE=4K
CLUSTERSIZE=4K
SLOTS=
JOURNALSIZE=
BLOCKS=
LABELNAME=ocfs2-inode-alloc-perf-tests

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
MKFSLOG=
MOUNTLOG=

TEST_PASS=0
TEST_NO=0

#record the original commitid of kernel tree before applying the patches
ORIG_COMMITID=

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
        echo "usage: `basename ${0}` [-o logdir] [-i iterations] <-d device> <-t kernel src tarball> <-k kernel path> <-p patches path> [-s iscsi server] <mountpoint path>"
        echo "       -o output directory for the logs"
        echo "       -d block device name used for ocfs2 volume"
        echo "       -t tarball of kernel src for test"
        echo "       -k path of currently used/complied kernel"
        echo "       -p path of newly released patches for kernel"
        echo "       -s specify iscsi server for iscsi-target cache dropping,use local disk by default"
        echo "       -i specify the iteration of test"
        echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
        exit 1;
}

function f_getoptions()
{
        if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "o:hd:t:k:i:p:s:" options; do
                case $options in
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
                t ) KERNEL_TARBALL="$OPTARG";;
                k ) KERNEL_PATH="$OPTARG";;
                p ) PATCH_PATH="$OPTARG";;
                s ) ISCSI_SERVER="$OPTARG";;
                i ) ITERATION="$OPTARG";;
                h ) f_usage;;
                * ) f_usage;;
                esac
        done
        shift $(($OPTIND -1))
        MOUNT_POINT=${1}
}

function f_check()
{
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

	if [ -z "${DEVICE}" ];then
		echo "Should specify the device"
		exit 1
	fi

	if [ -z "${KERNEL_TARBALL}" ];then
		echo "Should specify tarball of kernel src"
		exit 1
	fi

	TAR_ARGS=xzvf

	file ${KERNEL_TARBALL}|grep -q gzip || {
		TAR_ARGS=xjvf
	}


	if [ -z "${KERNEL_PATH}" ];then
		echo "Should specify the path of currently used kernel"
		exit 1
	fi

	if [ -z "${PATCH_PATH}" ];then
		echo "Should specify the path of desired patches"
		exit 1
	fi

        LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}

        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-inode-alloc-perf-tests-run.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-inode-alloc-perf-tests.log"
        MKFSLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mkfs.log"
        MOUNTLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mount.log"

	TIME_STAT_LOG_PREFIX="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-inode-alloc-perf-tests-time-stat"
}

function f_LogRunMsg()
{
        echo -ne "$@"| ${TEE_BIN} -a ${RUN_LOG_FILE}
}

function f_LogMsg()
{
        echo "$(date +%Y/%m/%d,%H:%M:%S)  $@" >>${LOG_FILE}
}

function f_RecordOrigCommitID()
{
	f_LogMsg "Try to record the original commitid of target kernel tree"
	OLD_DIR=${PWD}

	cd ${KERNEL_PATH}

	ORIG_COMMITID=`git log|sed -n '1p'|awk '{print $2}'` || {
		f_LogMsg "Failed to get last commitid of kernel tree, you may wrongly specify a kernel tree"
		return 1
	}

	f_LogMsg "Get original commitid = ${ORIG_COMMITID}"

	cd ${OLD_DIR}
}

function f_ApplyPatches()
{
	f_LogMsg "Try to apply the patches on target kernel tree"
	OLD_DIR=${PWD}

	cd ${KERNEL_PATH}

	for PATCH in `ls ${PATCH_PATH}`;do
		PATCH_ABS=${PATCH_PATH}/${PATCH}
		cat ${PATCH_ABS}| git am >> ${LOG_FILE} || {
			f_LogMsg "Failed to apply patch ${PATCH_ABS}"
			return 1
		}
	done

	cd ${OLD_DIR}
}

function f_ResetCommitID()
{
	f_LogMsg "Try to reset kernel with original commit ID"

	OLD_DIR=${PWD}

	cd ${KERNEL_PATH}

	git reset ${ORIG_COMMITID} --hard >> ${LOG_FILE}|| {
		f_LogMsg "Failed to reset commitid(${ORIG_COMMITID}) for kernel"
		return 1

	}
	
	cd ${OLD_DIR}
}

function f_Rebuild_Kernel()
{
	f_LogMsg "Try to rebuild kernel,then restart oc2b service"

	OLD_DIR=${PWD}
	
	cd ${KERNEL_PATH}

	make SUBDIRS=fs/ocfs2 >> ${LOG_FILE} 2>&1 || {
		f_LogMsg "Failed to make ocfs2 subdirs"
		return 1
	}

	RC=0
	cp fs/ocfs2/ocfs2.ko /lib/modules/`uname -r`/kernel/fs/ocfs2/ocfs2.ko
	RC=$((${RC}+$?))
	cp fs/ocfs2/dlm/ocfs2_dlm.ko /lib/modules/`uname -r`/kernel/fs/ocfs2/dlm/ocfs2_dlm.ko
	RC=$((${RC}+$?))
	cp fs/ocfs2/dlm/ocfs2_dlmfs.ko /lib/modules/`uname -r`/kernel/fs/ocfs2/dlm/ocfs2_dlmfs.ko
	RC=$((${RC}+$?))
	cp fs/ocfs2/cluster/ocfs2_nodemanager.ko /lib/modules/`uname -r`/kernel/fs/ocfs2/cluster/ocfs2_nodemanager.ko
	RC=$((${RC}+$?))

	if [ "${RC}" != "0" ];then
                f_LogMsg "Failed to install ocfs2 modules"
                return 1
        fi

	RC=0

	${SUDO} /etc/init.d/o2cb offline >> ${LOG_FILE} 2>&1
	RC=$((${RC}+$?))
	${SUDO} /etc/init.d/o2cb unload >> ${LOG_FILE} 2>&1
	RC=$((${RC}+$?))

	${SUDO} /etc/init.d/o2cb load >> ${LOG_FILE} 2>&1
	RC=$((${RC}+$?))
	${SUDO} /etc/init.d/o2cb online >> ${LOG_FILE} 2>&1
	RC=$((${RC}+$?))

	if [ "${RC}" != "0" ];then
		f_LogMsg "Failed to restart o2cb service"
		return 1
	fi

	cd ${OLD_DIR}
}

function f_mkfs()
{
	f_LogMsg "Mkfs volume ${DEVICE} by ${BLOCKSIZE} bas and ${CLUSTERSIZE} cs"
	echo "y"|${MKFS_BIN} -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -L ${LABELNAME} -M local ${DEVICE}>>${MKFSLOG} 2>&1
	RET=$?

	if [ "${RET}" != "0" ];then
		f_LogMsg "Mkfs failed"
		exit 1
	fi
}
function f_mount()
{
	f_LogMsg "Mount Volume ${DEVICE} to ${MOUNT_POINT}"
	${MOUNT_BIN} -t ocfs2 ${DEVICE} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
	RET=$?

	if [ "${RET}" != "0" ];then
                f_LogMsg "Mount volume failed"
                exit 1
        fi

	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1

	WORK_PLACE=${MOUNT_POINT}/inode_alloc_test_workplace

	${SUOD} ${MKDIR_BIN} -p ${WORK_PLACE}
}

function f_umount()
{
        f_LogMsg "Umount volume ${DEVICE} from ${MOUNT_POINT}"
        ${UMOUNT_BIN} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
        RET=$?

	if [ "${RET}" != "0" ];then
                f_LogMsg "Umount failed"
                exit 1
        fi
}

function f_get_disk_usage()
{
	f_LogMsg "Calculate the disk total and free size"

	DISK_FREE=`df -h|grep ${MOUNT_POINT}|awk '{print $4}'`

        if [ -z "${DISK_FREE}" ]; then
                DISK_FREE=`df -h|grep ${DEVICE}|awk '{print $4}'`
        fi

        TRAILER_POS=$((${#DISK_FREE}-1))
        TRAILER_CHAR=${DISK_FREE:${TRAILER_POS}}

	if [ "${TRAILER_CHAR}" = "M" ];then
                DISK_FREE_M=${DISK_FREE:0:${TRAILER_POS}}
        fi

        if [ "${TRAILER_CHAR}" = "G" ];then
                DISK_FREE_M=`echo ${DISK_FREE:0:${TRAILER_POS}}*1024|bc`
        fi

        if [ "${TRAILER_CHAR}" = "T" ];then
                DISK_FREE_M=`echo ${DISK_FREE:0:${TRAILER_POS}}*1024*1024|bc`
        fi
}

function f_get_tar_size()
{
	f_LogMsg "Get untared package size"
	
	#${1} is the path of released package

	TAR_SIZE=`du -sh ${1}|awk '{print $1}'`

	TRAILER_POS=$((${#TAR_SIZE}-1))
        TRAILER_CHAR=${TAR_SIZE:${TRAILER_POS}}

        if [ "${TRAILER_CHAR}" == "M" ]; then
                TAR_SIZE_M=${TAR_SIZE:0:${TRAILER_POS}}
        fi

        if [ "${TRAILER_CHAR}}" = "G" ];then
                TAR_SIZE_M=`echo ${TAR_SIZE:0:${TRAILER_POS}}*1024|bc`
        fi

        if [ "${TRAILER_CHAR}" = "T" ];then
                TAR_SIZE_M=`echo ${TAR_SIZE:0:${TRAILER_POS}}*1024*1024|bc`
        fi
	
}

function f_calc_tar_num()
{
	f_LogMsg "Calculate how many tars need to fill the disk up"

	f_LogMsg "Mkfsing ${DEVICE}:"
        f_mkfs

	f_LogMsg "Mount disk ${DEVICE}"
	f_mount
	
	echo WORK_PLACE=${WORK_PLACE}

	f_LogMsg "Tar ${TAR_ARGS} ${KERNEL_TARBALL} to ${WORK_PLACE}"
	${TAR_BIN} ${TAR_ARGS} ${KERNEL_TARBALL} -C ${WORK_PLACE} 1>/dev/null || {
		f_LogMsg "unTar failed when calculating the tar numbers"
		exit 1
	}

	f_LogMsg "Calculate the TAR_NUM"

	TAR_SIZE=`du -sh ${WORK_PLACE}|awk '{print $1}'`

	TRAILER_POS=$((${#TAR_SIZE}-1))
	TRAILER_CHAR=${TAR_SIZE:${TRAILER_POS}}

	if [ "${TRAILER_CHAR}" == "M" ]; then
		TAR_SIZE_M=${TAR_SIZE:0:${TRAILER_POS}}
	fi

	if [ "${TRAILER_CHAR}}" = "G" ];then
                TAR_SIZE_M=`echo ${TAR_SIZE:0:${TRAILER_POS}}*1024|bc`
        fi

	if [ "${TRAILER_CHAR}" = "T" ];then
                TAR_SIZE_M=`echo ${TAR_SIZE:0:${TRAILER_POS}}*1024*1024|bc`
        fi

	DISK_SIZE=`df -h|grep ${MOUNT_POINT}|awk '{print $4}'`

	if [ -z "${DISK_SIZE}" ]; then
		DISK_SIZE=`df -h|grep ${DEVICE}|awk '{print $4}'`
	fi

	TRAILER_POS=$((${#DISK_SIZE}-1))
        TRAILER_CHAR=${DISK_SIZE:${TRAILER_POS}}

	if [ "${TRAILER_CHAR}" = "M" ];then
		DISK_SIZE_M=${DISK_SIZE:0:${TRAILER_POS}}
	fi

	if [ "${TRAILER_CHAR}" = "G" ];then
                DISK_SIZE_M=`echo ${DISK_SIZE:0:${TRAILER_POS}}*1024|bc`
        fi

	if [ "${TRAILER_CHAR}" = "T" ];then
                DISK_SIZE_M=`echo ${DISK_SIZE:0:${TRAILER_POS}}*1024*1024|bc`
	fi

	TAR_NUM=`echo ${DISK_SIZE_M}/${TAR_SIZE_M}|bc `
	TAR_NUM=$((${TAR_NUM}-1))

	f_umount
}

#${1} specify the testing type
#${2} specify the testing number
function f_run_test_one_time()
{

	TIME_STAT_LOG=${TIME_STAT_LOG_PREFIX}-${2}.log.${1}

	f_LogMsg "Mkfsing ${DEVICE}:"
        f_mkfs

	f_LogMsg "Run #${2} Testing iteration with ${1} kernel"
	f_mount

	TAR_NUM=1

	while :;do
		echo "1st Tar #${TAR_NUM}">>${TIME_STAT_LOG}
		f_LogMsg "1st Tar #${TAR_NUM}"
		echo 3 > /proc/sys/vm/drop_caches
		TAR_DIR=${WORK_PLACE}/tar-released-${TAR_NUM}
		${SUDO} ${MKDIR_BIN} -p ${TAR_DIR}
		${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${TAR_BIN} ${TAR_ARGS} ${KERNEL_TARBALL} -C ${TAR_DIR} 1>/dev/null || {
			f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 1st #${j} Tar"
			exit 1
		}

		if [ "${TAR_NUM}" = "1" ];then
			sync
			f_get_tar_size ${TAR_DIR}
		fi

		sync
		f_get_disk_usage

		f_LogMsg "DISK_FREE=${DISK_FREE_M},TAR_SIZE=${TAR_SIZE_M}"

		CMP_RC=`echo "${DISK_FREE_M}<$((${TAR_SIZE_M}*6))"|bc`
		if [ "${CMP_RC}" = "1" ];then
			break
		fi

		((TAR_NUM++))
		
	done

	echo "1st Ls" >> ${TIME_STAT_LOG}
	f_LogMsg "1st Ls"
	#f_umount
	#f_mount
	echo 3 > /proc/sys/vm/drop_caches
	sync
	${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${LS_BIN} -lR ${MOUNT_POINT} 1>/dev/null || {
		f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 1st Ls"
                exit 1
	}


	echo "1st Rm" >> ${TIME_STAT_LOG}
	f_LogMsg "1st Rm"
	#f_umount
	#f_mount
	echo 3 > /proc/sys/vm/drop_caches
	${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${RM_BIN} ${WORK_PLACE}/* -rf 1>/dev/null || {
		f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 1st Rm"
                exit 1
	}
	sleep 1

	#f_umount
	#f_mount
	for j in `seq ${TAR_NUM}`;do
		echo "2nd Tar #${j}" >> ${TIME_STAT_LOG}
		f_LogMsg "2nd Tar #${j}"
		TAR_DIR=${WORK_PLACE}/tar-released-${j}
		${SUDO} ${MKDIR_BIN} -p ${TAR_DIR}
		echo 3 > /proc/sys/vm/drop_caches
		${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${TAR_BIN} ${TAR_ARGS} ${KERNEL_TARBALL} -C ${TAR_DIR} 1>/dev/null || {
			f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 2nd Tar"
        	        exit 1
		}

	done

	echo "2nd Ls" >> ${TIME_STAT_LOG}
	f_LogMsg "2nd Ls"
	#f_umount
	#f_mount
	echo 3 > /proc/sys/vm/drop_caches
	${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${LS_BIN} -lR ${MOUNT_POINT} 1>/dev/null  || {
		f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 2nd Ls"
                exit 1
	}

	echo "2nd Rm" >> ${TIME_STAT_LOG}
	f_LogMsg "2nd Rm"
	#f_umount
	#f_mount
	echo 3 > /proc/sys/vm/drop_caches
	${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${RM_BIN} ${WORK_PLACE}/* -rf 1>/dev/null || {
		f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 2nd Rm"
                exit 1
	}

	f_umount

	((TEST_PASS++))
	
	return 0
}

function f_run_test()
{
	f_LogRunMsg "Recording last commitid of original kernel tree:"
	f_RecordOrigCommitID
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg "Applying patches:"
	f_ApplyPatches 
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg "Rebuilding kernel:"
	f_Rebuild_Kernel
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	
	f_LogRunMsg "Running tests with ${ITERATION} times on patched kernel:"
	for i in $(seq ${ITERATION});do
        	f_run_test_one_time "patched" ${i}
	done
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg "Resetting commitid of original kernel tree:"
	f_ResetCommitID 
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg "Rebuilding kernel:"
	f_Rebuild_Kernel
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg "Running tests with ${ITERATION} times on original kernel:"
        for i in $(seq ${ITERATION});do
                f_run_test_one_time "original" ${i}
        done
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	return 0
}

f_stat_time_consumed()
{

	PATCHED_AVER_1ST_TAR_TIME=0
	PATCHED_AVER_1ST_LS_TIME=0
	PATCHED_AVER_1ST_RM_TIME=0
	PATCHED_AVER_2ND_TAR_TIME=0
        PATCHED_AVER_2ND_LS_TIME=0
        PATCHED_AVER_2ND_RM_TIME=0

	ORIG_AVER_1ST_TAR_TIME=0
        ORIG_AVER_1ST_LS_TIME=0
        ORIG_AVER_1ST_RM_TIME=0
        ORIG_AVER_2ND_TAR_TIME=0
        ORIG_AVER_2ND_LS_TIME=0
        ORIG_AVER_2ND_RM_TIME=0

	TEMP_READ_TIME=0
	TEMP_READ_TIME_ONCE=0	

	f_LogRunMsg "=====================Time Consumed Statistics(${ITERATION} iterations)=====================\n"
	f_LogMsg "=====================Time Consumed Statistics(${ITERATION} iterations)====================="


	for i in $(seq ${ITERATION});do

		PATCHED_TIME_STAT_LOG=${TIME_STAT_LOG_PREFIX}-${i}.log.patched
		ORIG_TIME_STAT_LOG=${TIME_STAT_LOG_PREFIX}-${i}.log.original
		TEMP_READ_TIME=0	
		for j in `seq ${TAR_NUM}`;do
			TEMP_READ_TIME_ONCE=`sed -n "/^1st Tar #${j}$/{n;p}" ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
			TEMP_READ_TIME=`echo ${TEMP_READ_TIME}+${TEMP_READ_TIME_ONCE}|bc`
		done
		PATCHED_AVER_1ST_TAR_TIME=`echo ${PATCHED_AVER_1ST_TAR_TIME}+${TEMP_READ_TIME}|bc`
		TEM_READ_TIME=0

		TEMP_READ_TIME=`sed -n '/1st Ls/{n;p}' ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
                PATCHED_AVER_1ST_LS_TIME=`echo ${PATCHED_AVER_1ST_LS_TIME}+${TEMP_READ_TIME}|bc`

		TEMP_READ_TIME=`sed -n '/1st Rm/{n;p}' ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
                PATCHED_AVER_1ST_RM_TIME=`echo ${PATCHED_AVER_1ST_RM_TIME}+${TEMP_READ_TIME}|bc`

		TEMP_READ_TIME=0
		for j in `seq ${TAR_NUM}`;do
			TEMP_READ_TIME_ONCE=`sed -n "/^2nd Tar #${j}$/{n;p}" ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
			TEMP_READ_TIME=`echo ${TEMP_READ_TIME}+${TEMP_READ_TIME_ONCE}|bc`
		done
                PATCHED_AVER_2ND_TAR_TIME=`echo ${PATCHED_AVER_2ND_TAR_TIME}+${TEMP_READ_TIME}|bc`
		TEMP_READ_TIME=0

                TEMP_READ_TIME=`sed -n '/2nd Ls/{n;p}' ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
                PATCHED_AVER_2ND_LS_TIME=`echo ${PATCHED_AVER_2ND_LS_TIME}+${TEMP_READ_TIME}|bc`

                TEMP_READ_TIME=`sed -n '/2nd Rm/{n;p}' ${PATCHED_TIME_STAT_LOG} | awk '{print $2}'`
                PATCHED_AVER_2ND_RM_TIME=`echo ${PATCHED_AVER_2ND_RM_TIME}+${TEMP_READ_TIME}|bc`

		TEMP_READ_TIME=0
		for j in `seq ${TAR_NUM}`;do
			TEMP_READ_TIME_ONCE=`sed -n "/^1st Tar #${j}$/{n;p}" ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
			TEMP_READ_TIME=`echo ${TEMP_READ_TIME}+${TEMP_READ_TIME_ONCE}|bc`
		done
                ORIG_AVER_1ST_TAR_TIME=`echo ${ORIG_AVER_1ST_TAR_TIME}+${TEMP_READ_TIME}|bc`
		TEMP_READ_TIME=0

                TEMP_READ_TIME=`sed -n '/1st Ls/{n;p}' ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
                ORIG_AVER_1ST_LS_TIME=`echo ${ORIG_AVER_1ST_LS_TIME}+${TEMP_READ_TIME}|bc`

                TEMP_READ_TIME=`sed -n '/1st Rm/{n;p}' ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
                ORIG_AVER_1ST_RM_TIME=`echo ${ORIG_AVER_1ST_RM_TIME}+${TEMP_READ_TIME}|bc`

		TEMP_READ_TIME=0
		for j in `seq ${TAR_NUM}`;do
	                TEMP_READ_TIME_ONCE=`sed -n "/^2nd Tar #${j}$/{n;p}" ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
			TEMP_READ_TIME=`echo ${TEMP_READ_TIME}+${TEMP_READ_TIME_ONCE}|bc`
		done
                ORIG_AVER_2ND_TAR_TIME=`echo ${ORIG_AVER_2ND_TAR_TIME}+${TEMP_READ_TIME}|bc`
		TEMP_READ_TIME=0

                TEMP_READ_TIME=`sed -n '/2nd Ls/{n;p}' ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
                ORIG_AVER_2ND_LS_TIME=`echo ${ORIG_AVER_2ND_LS_TIME}+${TEMP_READ_TIME}|bc`

                TEMP_READ_TIME=`sed -n '/2nd Rm/{n;p}' ${ORIG_TIME_STAT_LOG} | awk '{print $2}'`
                ORIG_AVER_2ND_RM_TIME=`echo ${ORIG_AVER_2ND_RM_TIME}+${TEMP_READ_TIME}|bc`

	done

	PATCHED_AVER_1ST_TAR_TIME=`echo ${PATCHED_AVER_1ST_TAR_TIME}/${ITERATION}| bc -l`
        PATCHED_AVER_1ST_LS_TIME=`echo ${PATCHED_AVER_1ST_LS_TIME}/${ITERATION}| bc -l`
        PATCHED_AVER_1ST_RM_TIME=`echo ${PATCHED_AVER_1ST_RM_TIME}/${ITERATION}| bc -l`
        PATCHED_AVER_2ND_TAR_TIME=`echo ${PATCHED_AVER_2ND_TAR_TIME}/${ITERATION}| bc -l`
        PATCHED_AVER_2ND_LS_TIME=`echo ${PATCHED_AVER_2ND_LS_TIME}/${ITERATION}| bc -l`
        PATCHED_AVER_2ND_RM_TIME=`echo ${PATCHED_AVER_2ND_RM_TIME}/${ITERATION}| bc -l`

        ORIG_AVER_1ST_TAR_TIME=`echo ${ORIG_AVER_1ST_TAR_TIME}/${ITERATION}| bc -l`
        ORIG_AVER_1ST_LS_TIME=`echo ${ORIG_AVER_1ST_LS_TIME}/${ITERATION}| bc -l`
        ORIG_AVER_1ST_RM_TIME=`echo ${ORIG_AVER_1ST_RM_TIME}/${ITERATION}| bc -l`
        ORIG_AVER_2ND_TAR_TIME=`echo ${ORIG_AVER_2ND_TAR_TIME}/${ITERATION}| bc -l`
        ORIG_AVER_2ND_LS_TIME=`echo ${ORIG_AVER_2ND_LS_TIME}/${ITERATION}| bc -l`
	ORIG_AVER_2ND_RM_TIME=`echo ${ORIG_AVER_2ND_RM_TIME}/${ITERATION}| bc -l`
	
	f_LogRunMsg "                  [Report of patched kernel]             Report of original kernel]\n"
	f_LogRunMsg "Av 1st Tar Time:	${PATCHED_AVER_1ST_TAR_TIME}s					${ORIG_AVER_1ST_TAR_TIME}s\n"
	f_LogRunMsg "Av 1st Ls Time:  	${PATCHED_AVER_1ST_LS_TIME}s 					${ORIG_AVER_1ST_LS_TIME}s\n"
	f_LogRunMsg "Av 1st Rm Time:  	${PATCHED_AVER_1ST_RM_TIME}s 					${ORIG_AVER_1ST_RM_TIME}s\n"
	f_LogRunMsg "Av 2nd Tar Time: 	${PATCHED_AVER_2ND_TAR_TIME}s					${ORIG_AVER_2ND_TAR_TIME}\n"
	f_LogRunMsg "Av 2nd Ls Time:  	${PATCHED_AVER_2ND_LS_TIME}s 					${ORIG_AVER_2ND_LS_TIME}s\n"
	f_LogRunMsg "Av 2nd Rm Time:  	${PATCHED_AVER_2ND_RM_TIME}s 					${ORIG_AVER_2ND_RM_TIME}s\n"

	return 0
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
f_LogRunMsg "=====================Inode alloc perf starts on ${FS_TYPE}:  `date`=====================\n"
f_LogMsg "=====================Inode alloc perf starts on ${FS_TYPE}:  `date`====================="

f_run_test

END_TIME=${SECONDS}
f_LogRunMsg "=====================Inode alloc perf testing ends on ${FS_TYPE}: `date`=====================\n"
f_LogMsg "=====================Inode alloc perf testing ends on ${FS_TYPE}: `date`====================="

f_LogRunMsg "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg "Tests total: $((${ITERATION}*2))\n"
f_LogRunMsg "Tests passed: ${TEST_PASS}\n"

f_stat_time_consumed
