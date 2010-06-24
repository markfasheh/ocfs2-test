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
# Description:  This script will perform a sanity check on acls with a series
#		of POSIX acls tools used for a acl-supported fs.
#
#		following tools needed:
# #               /usr/bin/setfacl,/usr/bin/getfacl
#               /usr/bin/chacl
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
DIFF_BIN=`which diff`
MOVE_BIN=`which mv`
CP_BIN=`which cp`
SED_BIN=`which sed`
CUT_BIN=`which cut`

USERNAME=`id -un`
GROUPNAME=`id -gn`

SUDO="`which sudo` -u root"

FS_TYPE=ocfs2
DEVICE=
MOUNT_POINT=

GROUPADD_BIN="`which sudo` -u root /usr/sbin/groupadd"
GROUPDEL_BIN="`which sudo` -u root /usr/sbin/groupdel"
USERADD_BIN="`which sudo` -u root /usr/sbin/useradd"
USERDEL_BIN="`which sudo` -u root /usr/sbin/userdel"
USERMOD_BIN="`which sudo` -u root /usr/sbin/usermod"
CHOWN_BIN=`which chown`
CHMOD_BIN=`which chmod`
SUDO_BIN=`which sudo`

SETACL_BIN="`which sudo` -u root `which setfacl`"
GETACL_BIN="`which getfacl` --absolute-names"
CHACL_BIN="`which sudo` -u root `which chacl`"

BLOCKSIZE=
CLUSTERSIZE=
SLOTS=4
LABELNAME="ocfs2-acl-tests-`uname -m`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
MKFSLOG=
MOUNTLOG=


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
        echo "usage: `basename ${0}` <-o logdir> <-d device> [-t fs_type] <mountpoint path>"
        echo "       -o output directory for the logs"
        echo "       -d block device name used for ocfs2 volume"
        echo "       -t fs_type,currently support ocfs2 and ext3"
        echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
        exit 1;
}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
                f_usage;
                exit 1
	fi

        while getopts "o:hd:t:" options; do
		case $options in
		o ) LOG_DIR="$OPTARG";;
		d ) DEVICE="$OPTARG";;
		t ) FS_TYPE="$OPTARG";;
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

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}

        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/acl-tests-run-`uname -m`-\
`date +%F-%H-%M-%S`.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/acl-tests-`uname -m`-\
`date +%F-%H-%M-%S`.log"
        MKFSLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mkfs.log"
        MOUNTLOG="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/$$_mount.log"

        if [ "${FS_TYPE}" = "ocfs2" ];then
                MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
        else
                MKFS_BIN="`which sudo` -u root `which mkfs.ext3`"
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
                echo "y"| ${MKFS_BIN} --fs-features=xattr -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -L ${LABELNAME} -N ${SLOTS} ${DEVICE} >>${MKFSLOG} 2>&1
                RET=$?
                f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
                f_exit_or_not ${RET}

                f_LogRunMsg "Mounting ${DEVICE} to ${MOUNT_POINT}:"
                ${MOUNT_BIN} -t ${FS_TYPE} -o acl ${DEVICE} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
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
                ${MOUNT_BIN} -t ${FS_TYPE} -o acl  ${DEVICE} ${MOUNT_POINT} >>${MOUNTLOG} 2>&1
                RET=$?
                f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
                f_exit_or_not ${RET}
        fi

        f_LogMsg "Chmod ${MOUNT_POINT} as 777"
        ${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
	${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
        f_exit_or_not $?
}

function f_runtest()
{
	local WORKPLACE=${MOUNT_POINT}/acls-tests
	local USERNAMEPREFIX=acls-test-user
        local USERNAME=
	local GROUPNAMEPREFIX=acls-tet-group
	local GROUPNAME=
	local TESTFILE=
	local TESTDIR=
	USERNUM=
	GROUPNUM=

	${MKDIR_BIN} -p ${WORKPLACE}
	f_exit_or_not $?

	((TEST_NO++))
	TESTFILE=${WORKPLACE}/acls-generic-test-file
	TESTDIR=${WORKPLACE}/acls-generic-test-dir
	f_LogRunMsg "[${TEST_NO}] Generic ACLs Test:"
        f_LogMsg "Test ${TEST_NO}: Generic ACLs Test."

	STARTID=500

	f_LogMsg "Touch testing file."
	${TOUCH_BIN} ${TESTFILE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?
	${MKDIR_BIN} ${TESTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Add ACLs for file and dir."

	${SETACL_BIN} -m u:${STARTID}:r-x ${TESTFILE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?
	${SETACL_BIN} -m u:${STARTID}:rwx ${TESTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	${GETACL_BIN}  --numeric --omit ${TESTFILE} |grep -q ${STARTID} >>${LOG_FILE} 2>&1
        f_exit_or_not $?
        ${GETACL_BIN}  --numeric --omit ${TESTDIR} |grep -q ${STARTID} >>${LOG_FILE} 2>&1
        f_exit_or_not $?

	f_LogMsg "Remove testing files."
	${RM_BIN} -rf ${TESTFILE} ${TESTDIR}
	f_exit_or_not $?

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	((TEST_PASS++))

	((TEST_NO++))
        f_LogRunMsg "[${TEST_NO}] Default ACLs Test:"
        f_LogMsg "Test ${TEST_NO}: Default ACLs Test."
	#deep depth dirent operations

	f_LogMsg "Test Default ACLs with considerable depth."
	DEPTH=100
	ROOTDIR=${WORKPLACE}/acls-default-test-dir
	f_LogMsg "Mkdir rootdir ${ROOTDIR}."
	${MKDIR_BIN} ${ROOTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?
	f_LogMsg "Add default ACLs to root dir."
	DEFAULT_ACL_NUM=1000
	STARTID=500
	for i in $(seq ${DEFAULT_ACL_NUM});do
		${SETACL_BIN} -d -m u:$((${STARTID}+${i})):rwx ${ROOTDIR} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	ROOTDIR_DEFAULT_ACL=/tmp/rootdir_default_acl.$$
	SONDIR_DEFAULT_ACL=/tmp/sondir_default_acl.$$

	INHERIT_ACL_TEMPLATE=/tmp/inherit_acl_template.$$
	INHERIT_ACL=/tmp/inherit_acl.$$

	${GETACL_BIN} -d --omit ${ROOTDIR} > ${ROOTDIR_DEFAULT_ACL} 2>>${LOG_FILE}
	f_exit_or_not $?

	TESTDIR=${ROOTDIR}

	f_LogMsg "Create dir tree to test default acls."
	for i in $(seq ${DEPTH});do
		TESTFILE=${TESTDIR}/default-file-level-${i}
		TESTDIR=${TESTDIR}/default-dir-level-${i}
		${MKDIR_BIN} ${TESTDIR} >>${LOG_FILE} 2>&1
		${TOUCH_BIN} ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?

		${GETACL_BIN} -d --omit ${TESTDIR} > ${SONDIR_DEFAULT_ACL} 2>>${LOG_FILE}
		f_exit_or_not $?

		${DIFF_BIN} ${ROOTDIR_DEFAULT_ACL} ${SONDIR_DEFAULT_ACL} || {
			f_LogMsg "Level ${i} dir ${TESTDIR}'s default ACLs did not match root's."
                	RET=1
	                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
			return -1
		}

		if [ ! -f ${INHERIT_ACL_TEMPLATE} ];then
			${GETACL_BIN} --omit ${TESTFILE} >${INHERIT_ACL_TEMPLATE} 2>>${LOG_FILE}
			f_exit_or_not $?
		else
			${GETACL_BIN} --omit ${TESTFILE} >${INHERIT_ACL} 2>>${LOG_FILE}
			f_exit_or_not $?
			${DIFF_BIN} ${INHERIT_ACL_TEMPLATE} ${INHERIT_ACL} || {
				f_LogMsg "Level ${i} file ${TESTFILE}'s inherited ACLs did not match other's."
				RET=1
				f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
				return -1
			}
		fi
		
	done

	f_LogMsg "Remove temporary comparing files."
	${RM_BIN} -rf ${ROOTDIR_DEFAULT_ACL}
        ${RM_BIN} -rf ${SONDIR_DEFAULT_ACL}

        ${RM_BIN} -rf ${INHERIT_ACL_TEMPLATE}
        ${RM_BIN} -rf ${INHERIT_ACL}
	f_exit_or_not $?

	f_LogMsg "Remove testing files."
	${RM_BIN} -rf ${ROOTDIR}
	
	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))


	((TEST_NO++))
	#cp,mv,ls
        f_LogRunMsg "[${TEST_NO}] ACLs Test With File Utilities:"
        f_LogMsg "Test ${TEST_NO}: ACLs Test With File Utilities."

	TESTFILE=${WORKPLACE}/acls-utils-test-file
	TESTFILE_MOVED=${WORKPLACE}/acls-utils-test-file-moved
	TESTFILE_COPIED=${WORKPLACE}/acls-utils-test-file-copied

	ORIG_ACL=/tmp/utils-test-orig-acl.$$
	MOVED_ACL=/tmp/utils-test-moved-acl.$$
	COPIED_ACL=/tmp/utils-test-copied-acl.$$

	USERNUM=1000
	STARTID=500

	f_LogMsg "Touch original files with ACLs attached."

	${TOUCH_BIN} ${TESTFILE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	for i in $(seq ${USERNUM});do
		${SETACL_BIN} -m u:$((${STARTID}+${i})):r-x ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	f_LogMsg "Test ls cmd."
	ls -l ${TESTFILE} | awk '{print $1}'| grep -q '+' || {
		f_LogMsg "ls utility has not been updated for handling acl."
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
		return -1
	}

	${GETACL_BIN} --omit ${TESTFILE} >${ORIG_ACL} 2>>${LOG_FILE}
	f_exit_or_not $?

	f_LogMsg "Test cp cmd."
	${CP_BIN} -p ${TESTFILE} ${TESTFILE_COPIED} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	${GETACL_BIN} --omit ${TESTFILE_COPIED} >${COPIED_ACL} 2>>${LOG_FILE}
	f_exit_or_not $?

	${DIFF_BIN} ${ORIG_ACL} ${COPIED_ACL} || {
		f_LogMsg "cp utility has not been updated for handling acl."
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
		return -1
	}
	
	f_LogMsg "Test mv cmd."
	${MOVE_BIN} ${TESTFILE} ${TESTFILE_MOVED} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	${GETACL_BIN} --omit ${TESTFILE_MOVED} >${MOVED_ACL} 2>>${LOG_FILE}
	f_exit_or_not $?

	${DIFF_BIN} ${ORIG_ACL} ${MOVED_ACL} || {
		f_LogMsg "mv utility has not been updated for handling acl."
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return -1
	}

	f_LogMsg "Remove temporary comparing files."
	${RM_BIN} -rf ${ORIG_ACL} ${MOVED_ACL} ${COPIED_ACL}
	f_exit_or_not $?

	f_LogMsg "Remove testing files."
	${RM_BIN} -rf ${TESTFILE_COPIED} ${TESTFILE_MOVED}
	f_exit_or_not $?
	
	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))

	((TEST_NO++))
        f_LogRunMsg "[${TEST_NO}] Copying ACLs Test:"
        f_LogMsg "Test ${TEST_NO}: Copying ACLs Test."
	
	TESTFILE=${WORKPLACE}/copying-src-acl-test-file
	USERNUM=100
	STARTID=500
	FILENUM=1000
	DESTFILE_PREFIX=${WORKPLACE}/copying-dest-acl-test-file

	SRC_ACL=/tmp/copying-test-src-acl.$$
	DEST_ACL=/tmp/copying-test-desct-acl.$$

	f_LogMsg "Touch source copying file."
	${TOUCH_BIN} ${TESTFILE} 
	f_exit_or_not $?

	for i in $(seq ${USERNUM});do
		${SETACL_BIN} -m u:$((${STARTID}+${i})):r-x ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	${GETACL_BIN} --omit ${TESTFILE}>${SRC_ACL} 2>>${LOG_FILE}
	f_exit_or_not $?

	for i in $(seq ${FILENUM});do
		${TOUCH_BIN} ${DESTFILE_PREFIX}-${i}
		f_exit_or_not $?
	done
	
	f_LogMsg "Copying ACLs to destination files."
	${GETACL_BIN} --omit ${TESTFILE} 2>>${LOG_FILE}| ${SETACL_BIN} --set-file=- ${DESTFILE_PREFIX}* >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Copying ACLs to dest file failed."
		RET=1
		f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return -1
	}

	for i in $(seq ${FILENUM});do
		${GETACL_BIN} --omit ${DESTFILE_PREFIX}-${i} >${DEST_ACL} 2>>${LOG_FILE}
		f_exit_or_not $?

		${DIFF_BIN} ${SRC_ACL} ${DEST_ACL} || {
			f_LogMsg "Dest ACL from copied file did not match original one."
			RET=1
			f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
			return -1
		}
	done

	f_LogMsg "Remove temporary and testing files"
	${RM_BIN} -rf ${TESTFILE}
	f_exit_or_not $?

	for i in $(seq ${FILENUM});do
		${RM_BIN} -rf ${DESTFILE_PREFIX}-${i}
		f_exit_or_not $?	
	done

	${RM_BIN} -rf ${SRC_ACL} ${DEST_ACL}
	f_exit_or_not $?

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))

	((TEST_NO++))
        f_LogRunMsg "[${TEST_NO}] Archive and Restore ACLs Test:"
        f_LogMsg "Test ${TEST_NO}: Archive and Restore ACLs Test."

	f_LogMsg "Construct the files Tree with ACLs."

	ROOTDIR=${WORKPLACE}/ar_test_root
	DEPTH=100
	STARTID=500

	ACL_AR_FILE=/tmp/acl_test_ar_file

	f_LogMsg "Mkdir root"
	${MKDIR_BIN} ${ROOTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Add ACLs for root."
	${SETACL_BIN} -m u:${STARTID}:rwx ${ROOTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	TESTDIR=${ROOTDIR}
	for i in $(seq ${DEPTH});do
		TESTFILE=${TESTDIR}/ar_file_level_${i}
		TESTDIR=${TESTDIR}/ar_dir_level_${i}

		${TOUCH_BIN} ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
		${MKDIR_BIN} ${TESTDIR} >>${LOG_FILE} 2>&1
		f_exit_or_not $?

		${SETACL_BIN} -m u:$((${STARTID}+${i})):rwx ${TESTDIR} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
		${SETACL_BIN} -m u:$((${STARTID}+${i})):rw- ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	f_LogMsg "Archive ACLs recursively into a file"
	${GETACL_BIN} -R ${ROOTDIR} > ${ACL_AR_FILE} 2>>${LOG_FILE}
	f_exit_or_not $?

	f_LogMsg "Clear ACLs for files Tree recursively."
	${SETACL_BIN} -R -b ${ROOTDIR} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Restore ACLs for files Tree from ar file."
	${SETACL_BIN} --restore ${ACL_AR_FILE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	f_LogMsg "Verify the restored ACLs."

	TESTDIR=${ROOTDIR}
	for i in $(seq ${DEPTH});do
		TESTFILE=${TESTDIR}/ar_file_level_${i}
		TESTDIR=${TESTDIR}/ar_dir_level_${i}
		
		FILE_USER_ID=$(${GETACL_BIN} --omit --numeric ${TESTFILE} 2>>${LOG_FILE}|${SED_BIN} -n '2p'|${CUT_BIN} -d':' -f2)
		FILE_ACL_PEM=$(${GETACL_BIN} --omit --numeric ${TESTFILE} 2>>${LOG_FILE}|${SED_BIN} -n '2p'|${CUT_BIN} -d':' -f3)

		DIR_USER_ID=$(${GETACL_BIN} --omit --numeric ${TESTDIR} 2>>${LOG_FILE}|${SED_BIN} -n '2p'|${CUT_BIN} -d':' -f2)
                DIR_ACL_PEM=$(${GETACL_BIN} --omit --numeric ${TESTDIR} 2>>${LOG_FILE}|${SED_BIN} -n '2p'|${CUT_BIN} -d':' -f3)

		if [ ! "${FILE_USER_ID}" = "$((${STARTID}+${i}))" ] || [ ! "${FILE_ACL_PEM}" = "rw-" ];then
			f_LogMsg "Level ${i}'s testfile's ACLs was not restored correctly"
	                RET=1
			f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
			return -1
		fi

		if [ ! "${DIR_USER_ID}" = "$((${STARTID}+${i}))" ] || [ ! "${DIR_ACL_PEM}" = "rwx" ];then
			f_LogMsg "Level ${i}'s testdir's ACLs was not restored correctly"
			RET=1
			f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
			return -1
                fi

	done

	f_LogMsg "Remove temporary and testing files."
	${RM_BIN} -rf ${WORKPLACE}
	f_exit_or_not $?
	${RM_BIN} -rf ${ACL_AR_FILE}
	f_exit_or_not $?

	${MKDIR_BIN} -p ${WORKPLACE}
	f_exit_or_not $?

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))

	((TEST_NO++))
	#8191
        f_LogRunMsg "[${TEST_NO}] ACLs Limitation Test:"
        f_LogMsg "Test ${TEST_NO}: ACLs Limitation Test."

	f_LogMsg "Touch testing file ${TESTFILE}"
        TESTFILE=${WORKPLACE}/acls-test-file
        ${TOUCH_BIN} ${TESTFILE}
        f_exit_or_not $?

	ACL_ENTRY_LIMIT=$((8191-4)) #should exclude mask and 3 normal permission entries
	STARTID=500
	
	f_LogMsg "Add ${ACL_ENTRY_LIMIT} ACLs entries to reach the limitation."
	for i in $(seq ${ACL_ENTRY_LIMIT});do
		${SETACL_BIN} -m u:$((${STARTID}+${i})):rwx ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	f_LogMsg "Try to exceed ACLs limitation"
	${SETACL_BIN} -m u:$((${STARTID}+${i}+1)):rwx ${TESTFILE} >>${LOG_FILE} 2>&1
	RET=$?
	if [ ${RET} == "0" ];then
		f_LogMsg "should not exceed the limit of ACLs"
                RET=1
                f_echo_status ${RET}|tee -a ${RUN_LOG_FILE}
                return -1
	fi

	f_LogMsg "Remove testing file ${TESTFILE}."
        ${RM_BIN} -rf ${TESTFILE}
        f_exit_or_not $?

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))

	((TEST_NO++))
	TESTFILE=${WORKPLACE}/acls-huge-entries-test-file
	f_LogRunMsg "[${TEST_NO}] ACLs Huge Entries Test:"
        f_LogMsg "Test ${TEST_NO}: ACLs Huge Entries Test."
	
	f_LogMsg "Touch testing file ${TESTFILE}"
	TESTFILE=${WORKPLACE}/acls-test-file
	${TOUCH_BIN} ${TESTFILE}
	f_exit_or_not $?

	USERNUM=4000
	GROUPNUM=4000
	STARTID=500
	
	f_LogMsg "Add ${USERNUM} ACLs user entries."
	for i in $(seq ${USERNUM});do
		${SETACL_BIN} -m u:$((${STARTID}+${i})):rwx ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	f_LogMsg "Add ${USERNUM} ACLs group entries."
	for i in $(seq ${GROUPNUM});do
		${SETACL_BIN} -m g:$((${STARTID}+${i})):r-x ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	f_LogMsg "Remove all ACLs entries."
	${SETACL_BIN} -b ${TESTFILE} >>${LOG_FILE} 2>&1
	f_exit_or_not $?

	
	f_LogMsg "Add ${USERNUM} ACLs user entries."
	for i in $(seq ${USERNUM});do
                ${SETACL_BIN} -m u:$((${STARTID}+${i})):rwx ${TESTFILE} >>${LOG_FILE} 2>&1
                f_exit_or_not $?
        done
	
	f_LogMsg "Add ${USERNUM} ACLs group entries."
	for i in $(seq ${GROUPNUM});do
                ${SETACL_BIN} -m g:$((${STARTID}+${i})):r-x ${TESTFILE} >>${LOG_FILE} 2>&1
                f_exit_or_not $?
        done

	f_LogMsg "Remove ACL entry one by one."
	for i in $(seq ${USERNUM});do
                ${SETACL_BIN} -x u:$((${STARTID}+${i})) ${TESTFILE} >>${LOG_FILE} 2>&1
                f_exit_or_not $?
        done

	for i in $(seq ${GROUPNUM});do
                ${SETACL_BIN} -x g:$((${STARTID}+${i})) ${TESTFILE} >>${LOG_FILE} 2>&1
                f_exit_or_not $?
        done

	f_LogMsg "Remove testing file ${TESTFILE}."
	${RM_BIN} -rf ${TESTFILE}
	f_exit_or_not $?

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	((TEST_PASS++))

	((TEST_NO++))
        f_LogRunMsg "[${TEST_NO}] Stress ACLs Test:"
        f_LogMsg "Test ${TEST_NO}: Stress ACLs Test."
	#huge entries,huge files and dirents

	FILENUM=1000
	DIRNUM=1000
	USERNUM=100
	GROUPNUM=100
	STARTID=500
	f_LogMsg "Touch ${FILENUM} files and ${DIRNUM} dirs"	

	TESTFILE=${WORKPLACE}/acls-test-file
        ${TOUCH_BIN} ${TESTFILE}


	for i in $(seq ${FILENUM});do
		TESTFILE=${WORKPLACE}/acls-test-file-${i}
		${TOUCH_BIN} ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
		for j in $(seq ${USERNUM});do
			${SETACL_BIN} -m u:$((${STARTID}+${j})):rwx ${TESTFILE} >>${LOG_FILE} 2>&1
		done
		for k in $(seq ${GROUPNUM});do
			${SETACL_BIN} -m g:$((${STARTID}+${k})):r-x ${TESTFILE} >>${LOG_FILE} 2>&1
		done
	
	done

	for i in $(seq ${DIRNUM});do
		TESTDIR=${WORKPLACE}/acls-test-dir-${i}
		${MKDIR_BIN} ${TESTDIR} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
		for j in $(seq ${USERNUM});do
			${SETACL_BIN} -m u:$((${STARTID}+${j})):rwx ${TESTDIR} >>${LOG_FILE} 2>&1
                done
                for k in $(seq ${GROUPNUM});do
			${SETACL_BIN} -m g:$((${STARTID}+${k})):r-x ${TESTDIR} >>${LOG_FILE} 2>&1
                done
		for l in $(seq ${USERNUM});do
			${SETACL_BIN} -d -m u:$((${STARTID}+${l})):r-- ${TESTDIR} >>${LOG_FILE} 2>&1
                done
	done

	f_LogMsg "Remove all ACLs and testing files."
	for i in $(seq ${FILENUM});do
		TESTFILE=${WORKPLACE}/acls-test-file-${i}
		${SETACL_BIN} -b ${TESTFILE} >>${LOG_FILE} 2>&1
		${RM_BIN} -rf ${TESTFILE} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done
	
	for i in $(seq ${DIRNUM});do
		TESTDIR=${WORKPLACE}/acls-test-dir-${i}
		${SETACL_BIN} -b ${TESTDIR} >>${LOG_FILE} 2>&1
		${RM_BIN} -rf ${TESTDIR} >>${LOG_FILE} 2>&1
		f_exit_or_not $?
	done

	RET=$?
        f_echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        ((TEST_PASS++))


	${RM_BIN} -rf ${WORKPLACE}
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
f_LogRunMsg "=====================ACLs testing starts on ${FS_TYPE}: `date`=====================\n"
f_LogMsg "=====================ACLs testing starts on ${FS_TYPE}: `date`====================="

for BLOCKSIZE in 512 1024 2048 4096;do
	for CLUSTERSIZE in 4096 32768 1048576;do
		f_mkfs_and_mount
		f_runtest
		f_umount
	done
done

END_TIME=${SECONDS}
f_LogRunMsg "=====================ACLs testing ends on ${FS_TYPE}: `date`=====================\n"
f_LogMsg "=====================ACLs testing ends on ${FS_TYPE}: `date`====================="

f_LogRunMsg "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg "Tests total: ${TEST_NO}\n"
f_LogRunMsg "Tests passed: ${TEST_PASS}\n"
