# *
# * o2tf.sh
# *
# * Collection of functions used by ocfs2-test shell programs.
# *
# * Copyright (C) 2008 Oracle.  All rights reserved.
# *
# * This program is free software; you can redistribute it and/or
# * modify it under the terms of the GNU General Public
# * License version 2 as published by the Free Software Foundation.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# * General Public License for more details.
# *
# * Author : Tristan Ye
#
# o2tf - OCFS2 Tests Functions.
#

################################################################################
# Global Variables
################################################################################
PATH=$PATH:/sbin:/usr/sbin      # Add /sbin,/usr/sbin to the path for ocfs2 tools
export PATH=$PATH:.

. `dirname ${0}`/config.sh

MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
FSCK_BIN="`which sudo` -u root `which fsck.ocfs2`"
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
TUNEFS_BIN="`which sudo` -u root `which tunefs.ocfs2`"
REMOTE_MOUNT_BIN="${BINDIR}/remote_mount.py"
REMOTE_UMOUNT_BIN="${BINDIR}/remote_umount.py"

TEE_BIN=`which tee`
RM_BIN=`which rm`
TAR_BIN=`which tar`
MKDIR_BIN=`which mkdir`
TOUCH_BIN=`which touch`
DIFF_BIN=`which diff`
MOVE_BIN=`which mv`
CP_BIN=`which cp`
SED_BIN=`which sed`
CUT_BIN=`which cut`
AWK_BIN=`which awk`
CHOWN_BIN=`which chown`
CHMOD_BIN=`which chmod`

SUDO="`which sudo` -u root"

USERNAME=`id -un`
GROUPNAME=`id -gn`

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

function f_LogRunMsg()
{
	#${1} specify the RUN_LOG_FILE
	#Rest of params are log content

	local run_logfile=${1}

	shift 1
	
        echo -ne "$@"| ${TEE_BIN} -a ${run_logfile}
}

function f_LogMsg()
{
	#${1} specify the LOG_FILE
	#Rest of params are log content

	local logfile=${1}
	
	shift 1

        echo "$(date +%Y/%m/%d,%H:%M:%S)  $@" >>${logfile}
}

function f_mkfs()
{
	#${1} specify log file

	#${2} specify block size
	#${3} specify cluster size
	#${4} specify label name
	#${5} specify max slot number
	#${6} specify device name
	#${7} specify fs features
	#${8} specify journal size
	#${9} specify volume size
	#${10} specify cluster stack
	#${11} specify cluster name

	local slot_opts=""
        local cluster_stack=""
        local cluster_name=""
	if [ "${5}" == "0" ];then
		slot_opts="-M local"
	else
		slot_opts="-N ${5}"
                cluster_stack="--cluster-stack=${10}" #cluster stack
                cluster_name="--cluster-name=${11}" #cluster name
	fi
	
	local journal_opts=""
	if [ "${8}" != "0" ];then
		journal_opts="-J size=${8}M"
	fi
	
	local blocks=""
	if [ "${9}" != "0" ];then
		blocks="${9}"
	fi
	
	local logfile=${1}

	local B=${2} #block size
	local C=${3} #cluster size
	local L=${4} #volume name
	local D=${6} #device name
	local O=${7} #fs featuers
	local S=${10} #cluster stack
	local N=${11} #cluster name
	
	shift 11
	R=${1} #Reserved options
	
        f_LogMsg ${logfile} "${MKFS_BIN} --fs-features=${O} ${cluster_stack} ${cluster_name} \
-b ${B} -C ${C} -L ${L} ${slot_opts} ${journal_opts} ${R} ${D} ${blocks}"
        echo "y"|${MKFS_BIN} --fs-features=${O} ${cluster_stack} ${cluster_name} \
-b ${B} -C ${C} -L ${L} ${slot_opts} ${journal_opts} ${R} ${D} ${blocks}>>${logfile} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg ${logfile} "Mkfs failed"
                return 1
        fi

        return 0
}

function f_mount()
{
	#${1} specify the logfile
	
	#${2} specify the device
	#${3} specify the mount point

	#${4} specify fs type
	#${5} specify mount options

	local logfile=${1}

	local D=${2}
	local M=${3}

	local T=${4}
	local fs_type="-t ${T}"

	shift 4

	local O=${@}
	if [ ! -z "${O}" ];then
		local mt_opts="-o ${O}"
	fi

        f_LogMsg ${logfile} "${MOUNT_BIN} ${fs_type} ${mt_opts} ${D} ${M}"
        ${MOUNT_BIN} ${fs_type} ${mt_opts} ${D} ${M} >>${logfile} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg ${logfile} "Mount volume failed"
                return 1
        fi

        ${SUDO} ${CHMOD_BIN} -R 777 ${M}  >>${logfile} 2>&1
        ${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${M} >>${logfile} 2>&1

        return 0
}

function f_umount()
{
	#${1} specify logfile
	#${2} specify mount point

	local logfile=${1}
	local M=${2}

        f_LogMsg ${logfile} "Umount Volume from ${M}"
        ${UMOUNT_BIN} ${M} >>${logfile} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg ${logfile} "Mount volume failed"
                return 1
        fi

        return 0
}

function f_remote_mount()
{
	#${1} specify logfile

	#${2} specify label name
	#${3} specify mount point
	#${4} specify MPI hosts, it's comma-spearated-value
	#${5} specify mount options

	local logfile=${1}

	local L=${2}
	local M=${3}
	local MPIHOSTS=${4}
	local MT_OPTIONS=

	shift 4

	local O=${@}
	if [ ! -z "${O}" ];then
		MT_OPTIONS="-o ${O}"
	fi

        f_LogMsg ${logfile} "${REMOTE_MOUNT_BIN} -l ${L} -m ${M} -n ${MPIHOSTS} ${MT_OPTIONS}"
        ${REMOTE_MOUNT_BIN} -l ${L} -m ${M} -n ${MPIHOSTS} ${MT_OPTIONS}>>${logfile} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg ${logfile} "Remote mount failed"
                return ${RET}
        fi

        ${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${M}
        ${SUDO} chmod -R 777 ${M}

        return 0
}

function f_remote_umount()
{
	#${1} specify logfile

	#${2} specify mount point
	#${3} specify mpi hosts

	local logfile=${1}

	local M=${2}
	local MPIHOSTS=${3}

        f_LogMsg ${logfile} "${REMOTE_UMOUNT_BIN} -m ${M} -n ${MPIHOSTS}"
        ${REMOTE_UMOUNT_BIN} -m ${M} -n ${MPIHOSTS}>>${logfile} 2>&1
        RET=$?

        if [ "${RET}" != "0" ];then
                f_LogMsg ${logfile} "Remote umount failed"
                return ${RET}
        fi

        return 0
}
