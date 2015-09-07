#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# multi_inode_alloc_perf.sh
#
# Description:	The script based on open-mpi utilites,helps to run 
#		a performance comparison tests between original and 
#		enhanced kernel on inode allocation, deletion and 
#		traversing tests among multiple nodes.
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
#BINDIR=./

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

REMOTE_MOUNT_BIN="${BINDIR}/remote_mount.py"
REMOTE_UMOUNT_BIN="${BINDIR}/remote_umount.py"

LOCAL_TEST_RUNNER="${BINDIR}/multi_inode_alloc_perf_runner.sh"

MOUNT_POINT=
DEVICE=
KERNEL_PATH=
KERNEL_TARBALL=
PATCH_PATH=
ISCSI_SERVER=dummy-server
ITERATION=1

DISK_FREE=
DISK_FREE_M=

TAR_SIZE=
TAR_FREE_M=

BLOCKSIZE=4K
CLUSTERSIZE=4K
SLOTS=4
LABELNAME=ocfs2-multi-inode-alloc-perf-tests

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
LOCAL_LOG_DIR=/tmp/ocfs2-multi-inode-alloc-perf-tests-logs

#record the original commitid of kernel tree before applying the patches
ORIG_COMMITID=

declare -i MPI_RANKS
MPI_HOSTS=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"

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
        echo "usage: `basename ${0}` [-o overall logdir] <-l local log> [-i iterations] [-a access method] <-n node list> <-d device> <-t kernel src tarball> <-k kernel path> <-p patches path> [-s iscsi server] <mountpoint path>"
        echo "       -o directory for overall logs"
        echo "       -l directory for local logs"
	echo "       -a access method for process propagation,should be ssh or rsh,ssh by default."
        echo "       -n specify node list,which should be comma separated value"
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

         while getopts "o:hd:t:k:i:p:s:a:n:l:" options; do
                case $options in
                o ) LOG_DIR="$OPTARG";;
                l ) LOCAL_LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		n ) MPI_HOSTS="$OPTARG";;
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

	if [ "$MPI_ACCESS_METHOD" = "rsh" ];then
                MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"
        fi

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


function f_mkfs()
{
        f_LogMsg "Mkfs volume ${DEVICE} by ${BLOCKSIZE} bas and ${CLUSTERSIZE} cs"
        echo "y"|${MKFS_BIN} -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N ${SLOTS} -L ${LABELNAME} ${DEVICE}>>${MKFSLOG} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg "Mkfs failed"
                exit 1
        fi
}

function f_remote_mount()
{
	f_LogMsg "Mount Volume ${DEVICE} to ${MOUNT_POINT} on nodes ${MPI_HOSTS}"

        ${REMOTE_MOUNT_BIN} -l ${LABELNAME} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
	if [ "${RET}" != "0" ];then
                f_LogMsg "Remote mount failed"
                return 1
        fi

	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1

	return 0
}

function f_remote_umount()
{
	f_LogMsg "Umount Volume ${DEVICE} from ${MOUNT_POINT} among nodes ${MPI_HOSTS}"

	${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
	if [ "${RET}" != "0" ];then
                f_LogMsg "Remote umount failed"
                return 1
        fi
	
	return 0
}

f_run_test_one_time()
{
	f_LogRunMsg "<Iteration ${1}>Mkfsing ${DEVICE}:"
       	f_mkfs
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	f_LogRunMsg "<Iteration ${1}>Mount Volume ${DEVICE} to ${MOUNT_POINT} on nodes ${MPI_HOSTS}:"
	f_remote_mount
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	f_LogRunMsg "<Iteration ${1}>Run inode alloc perf tests among nodes ${MPI_HOSTS}:"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0  --host ${MPI_HOSTS} ${LOCAL_TEST_RUNNER} -l ${LOCAL_LOG_DIR} -b ${LABELNAME} -t ${KERNEL_TARBALL} -k ${KERNEL_PATH} -p ${PATCH_PATH} -s ${ISCSI_SERVER} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

        f_LogRunMsg "<Iteration ${1}>Umount Volume ${DEVICE} from ${MOUNT_POINT} among nodes ${MPI_HOSTS}:"
	f_remote_umount
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
f_LogRunMsg "=====================Multi-nodes inode alloc perf starts on ${FS_TYPE}:  `date`=====================\n"
f_LogMsg "=====================Multi-nodes inode alloc perf starts on ${FS_TYPE}:  `date`====================="

for i in $(seq ${ITERATION});do
	f_run_test_one_time ${i}
done

END_TIME=${SECONDS}
f_LogRunMsg "=====================Multi-nodes inode alloc perf testing ends on ${FS_TYPE}: `date`=====================\n"
f_LogMsg "=====================Multi-nodes inode alloc perf testing ends on ${FS_TYPE}: `date`====================="

f_LogRunMsg "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
