#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# multi_inode_alloc_perf_runner.sh
#
# Description:	The script based on open-mpi utilites, behaves as a runner
# 		to launch various cmd among nodes via requests.
#
# Author:	Tristan Ye,     tristan.ye@oracle.com
#
# History:	20 Jan 2009
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

. ./config.sh

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
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"

WORKPLACE=
DEFAULT_LOG=/tmp/local-inode-alloc-tests
LOG_DIR=

MOUNT_POINT=
KERNEL_PATH=
KERNEL_TARBALL=
PATCH_PATH=
ISCSI_SERVER=
ITERATION=1

TAR_ARGS=
TAR_NUM=1

DISK_FREE=
DISK_FREE_M=

TAR_SIZE=
TAR_SIZE_M=

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
function f_exit_or_not()
{
        if [ "${1}" != "0" ];then
                exit 1;
        fi
}

function f_usage()
{
        echo "usage: `basename ${0}` [-l local logdir] <-b volume label name> <-t kernel src tarball> <-k kernel path> <-p patches path> [-s iscsi server] <mountpoint path>"
        echo "       -l directory for the local logs on each node"
        echo "       -b label name of target volume"
        echo "       -t tarball of kernel src for test"
        echo "       -k path of currently used/complied kernel"
        echo "       -p path of newly released patches for kernel"
        echo "       -s specify iscsi server for iscsi-target cache dropping,use local disk by default"
        echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
        exit 1;
}

function f_getoptions()
{
        if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "l:ht:k:b:p:s:" options; do
                case $options in
                l ) LOG_DIR="$OPTARG";;
                t ) KERNEL_TARBALL="$OPTARG";;
                k ) KERNEL_PATH="$OPTARG";;
                p ) PATCH_PATH="$OPTARG";;
                s ) ISCSI_SERVER="$OPTARG";;
                b ) LABELNAME="$OPTARG";;
                h ) f_usage;;
                * ) f_usage;;
                esac
        done
        shift $(($OPTIND -1))
        MOUNT_POINT=${1}

	WORKPLACE=${MOUNT_POINT}/$(hostname)
	${MKDIR_BIN} -p ${WORKPLACE}

	TAR_ARGS=xzvf

        file ${KERNEL_TARBALL}|grep -q gzip || {
                TAR_ARGS=xjvf
        }

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	LOG_DIR=${LOG_DIR}_`hostname`

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`hostname`-`date +%F-%H-%M-%S`-inode-alloc-perf-tests-run.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`hostname`-`date +%F-%H-%M-%S`-inode-alloc-perf-tests.log"

        TIME_STAT_LOG_PREFIX="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`hostname`-`date +%F-%H-%M-%S`-inode-alloc-perf-tests-time-stat"

	KERNEL_PATH=${KERNEL_PATH}-`hostname`

}

function f_mount()
{
        f_LogMsg "Mount Volume ${LABELNAME} to ${MOUNT_POINT}"
        ${MOUNT_BIN} -t ocfs2 -L ${LABELNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg "Mount volume failed"
                exit 1
        fi

        ${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
}
function f_umount()
{
        f_LogMsg "Umount volume ${LABELNAME} from ${MOUNT_POINT}"
        ${UMOUNT_BIN} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg "Umount failed"
                exit 1
        fi
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

	return 0
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

	return 0
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

	return 0
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

	return 0

}

function f_get_disk_usage()
{
        f_LogMsg "Calculate the disk total and free size"

        DISK_FREE=`df -h|grep ${MOUNT_POINT}|awk '{print $4}'`

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


#${1} specify the testing type
#${2} specify the testing number
function f_run_test_one_time()
{

        TIME_STAT_LOG=${TIME_STAT_LOG_PREFIX}-${2}.log.${1}

        f_LogMsg "Run #${2} Testing iteration with ${1} kernel"
        f_mount

	TAR_NUM=1
	while :;do
        echo "1st Tar #${TAR_NUM}">>${TIME_STAT_LOG}
	        f_LogMsg "1st Tar #${TAR_NUM}"
	        echo 3 > /proc/sys/vm/drop_caches
		TAR_DIR=${WORKPLACE}/tar-released-${TAR_NUM}
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

		CMP_RC=`echo "${DISK_FREE_M}<$((${TAR_SIZE_M}*8))"|bc`
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
        sleep 1
        ${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${LS_BIN} -lR ${WORKPLACE} 1>/dev/null || {
                f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 1st Ls"
                exit 1
        }
	
	echo "1st Rm" >> ${TIME_STAT_LOG}
        f_LogMsg "1st Rm"
        #f_umount
        #f_mount
        echo 3 > /proc/sys/vm/drop_caches
        ${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${RM_BIN} ${WORKPLACE}/* -rf || {
                f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 1st Rm"
                exit 1
        }

	f_umount
	f_mount
	for j in `seq ${TAR_NUM}`;do
		echo "2nd Tar #${j}" >> ${TIME_STAT_LOG}
                f_LogMsg "2nd Tar #${j}"
                TAR_DIR=${WORKPLACE}/tar-released-${j}
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
        sleep 1
        ${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${LS_BIN} -lR ${WORKPLACE} 1>/dev/null || {
                f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 2nd Ls"
                exit 1
        }

	echo "2nd Rm" >> ${TIME_STAT_LOG}
        f_LogMsg "2nd Rm"
        #f_umount
        #f_mount
        echo 3 > /proc/sys/vm/drop_caches
        ${TIME_BIN} -ao ${TIME_STAT_LOG} -p ${RM_BIN} ${WORKPLACE}/* -rf || {
                f_LogMsg "Failed at Iteration:[${2}],Type:[${1} kernel] on 2nd Rm"
                exit 1
        }

        f_umount

        ((TEST_PASS++))

        return 0
}

function f_run_test()
{
	f_LogMsg "Umount volume"
	f_umount
	RET=$?
	f_exit_or_not ${RET}

        f_LogMsg "Recording last commitid of original kernel tree:"
        f_RecordOrigCommitID
        RET=$?
        f_exit_or_not ${RET}

        f_LogMsg "Applying patches:"
        f_ApplyPatches
        RET=$?
        f_exit_or_not ${RET}

        f_LogMsg "Rebuilding kernel:"
        f_Rebuild_Kernel
        RET=$?
        f_exit_or_not ${RET}

	f_LogMsg "Running tests with ${ITERATION} times on patched kernel:"
	for i in $(seq ${ITERATION});do
	        f_run_test_one_time "patched" ${i}
	done
        RET=$?
        f_exit_or_not ${RET}

        f_LogMsg "Resetting commitid of original kernel tree:"
        f_ResetCommitID
        RET=$?
        f_exit_or_not ${RET}

        f_LogMsg "Rebuilding kernel:"
        f_Rebuild_Kernel
        RET=$?
        f_exit_or_not ${RET}

        f_LogMsg "Running tests with ${ITERATION} times on original kernel:"
	for i in $(seq ${ITERATION});do
		f_run_test_one_time "original" ${i} 
	done
        RET=$?
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

        f_LogMsg "[Report of patched kernel]"
        f_LogMsg "Average 1st Tar Time: ${PATCHED_AVER_1ST_TAR_TIME}s"
        f_LogMsg "Average 1st Ls Time: ${PATCHED_AVER_1ST_LS_TIME}s"
        f_LogMsg "Average 1st Rm Time: ${PATCHED_AVER_1ST_RM_TIME}s"
        f_LogMsg "Average 2nd Tar Time: ${PATCHED_AVER_2ND_TAR_TIME}s"
        f_LogMsg "Average 2nd Ls Time: ${PATCHED_AVER_2ND_LS_TIME}s"
        f_LogMsg "Average 2nd Rm Time: ${PATCHED_AVER_2ND_RM_TIME}s"

        f_LogMsg "[Report of original kernel]"
        f_LogMsg "Average 1st Tar Time: ${ORIG_AVER_1ST_TAR_TIME}s"
        f_LogMsg "Average 1st Ls Time: ${ORIG_AVER_1ST_LS_TIME}s"
        f_LogMsg "Average 1st Rm Time: ${ORIG_AVER_1ST_RM_TIME}s"
        f_LogMsg "Average 2nd Tar Time: ${ORIG_AVER_2ND_TAR_TIME}s"
        f_LogMsg "Average 2nd Ls Time: ${ORIG_AVER_2ND_LS_TIME}s"
        f_LogMsg "Average 2nd Rm Time: ${ORIG_AVER_2ND_RM_TIME}s"

        return 0
}

################################################################################
# Main Entry
################################################################################

#redfine the int signal hander
trap 'echo -ne "\n\n">>${LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${LOG_FILE}; f_cleanup;exit 1' SIGINT

f_getoptions $*

f_run_test

f_stat_time_consumed
