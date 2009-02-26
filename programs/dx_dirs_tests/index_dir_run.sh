#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# index_dir_run.sh
#
# description:  This script will perform a thorough test on indexed-dirs for ocfs2.
#		Following testcases will be involved.
#
#		1. Basic func test
#		
#		2. Random test
#		
#		3. Concurrent test
#		
#		4. Multi-processes test
#		
#		5. Growing test
#		
#		6. Stress test
#		
#		7. Boundary test.
#		
#		8. Destructive test.
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      10 Feb 2009
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
. ./o2tf.sh

BLOCKSIZE=
CLUSTERSIZE=
SLOTS=4
JOURNALSIZE=0
BLOCKS=0
LABELNAME=ocfs2-indexed-dirs-tests
DEVICE=
WORK_PLACE_DIRENT=indexed-dirs-tests
WORK_PLACE=

INDEXED_DIRS_TEST_BIN="${BINDIR}/index_dir"

KERNEL_TARBALL=
TAR_ARGS=xzvf

DISK_FREE=
DISK_FREE_M=
TAR_SIZE=
TAR_SIZE_M=

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=

TEST_NO=0
TEST_PASS=0

set -o pipefail

################################################################################
# Utility Functions
################################################################################
function f_usage()
{
        echo "usage: `basename ${0}` [-o logdir] <-d device> <-t kernel tarball> <mountpoint path>"
        echo "       -o output directory for the logs"
        echo "       -t kernel tarball for growing test"
        echo "       -d block device name used for ocfs2 volume"
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
                t ) KERNEL_TARBALL="$OPTARG";;
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
	
	TAR_ARGS=xzvf

        file ${KERNEL_TARBALL}|grep -q gzip || {
                TAR_ARGS=xjvf
        }

        LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-indexed-dirs-tests-run.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-indexed-dirs-tests.log"
}

function f_get_disk_usage()
{
        f_LogMsg ${LOG_FILE} "Calculate the disk free size"

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
        f_LogMsg ${LOG_FILE} "Get untared package size"

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


function f_growtest()
{
	TAR_NUM=1

	while :;do
		TAR_DIR=${WORK_PLACE}/tar-released-${TAR_NUM}
		${MKDIR_BIN} -p ${TAR_DIR}
		${TAR_BIN} ${TAR_ARGS} ${KERNEL_TARBALL} -C ${TAR_DIR} >/dev/null 2>&1|| {
                        f_LogMsg ${LOG_FILE} "Untar failed, probably due to no space for file data or inodes."
			return 0
                }

		if [ "${TAR_NUM}" = "1" ];then
                        sync
                        f_get_tar_size ${TAR_DIR}
                fi

                sync
                f_get_disk_usage

                CMP_RC=`echo "${DISK_FREE_M}<$((${TAR_SIZE_M}))"|bc`
                if [ "${CMP_RC}" = "1" ];then
                        break
                fi

                ((TAR_NUM++))
	done

	return 0
}

function f_runtest()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} ${DEVICE} "indexed-dirs,noinline-data" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mount device ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Basic Fucntional Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Basic Fucntional Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 10 -n 20000 -v ${DEVICE} -d 2 -w ${WORK_PLACE} -f"
	${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 10 -n 20000 -v ${DEVICE} -d 2 -w ${WORK_PLACE} -f >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Random Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Random Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -n 5000 -v ${DEVICE} -d 2 -w ${WORK_PLACE} -r 10"
	${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -n 5000 -v ${DEVICE} -d 2 -w ${WORK_PLACE} -r 10 >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Concurrent Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Concurrent Test:, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -n 4000 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -c 200"
        ${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -n 4000 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -c 200 >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Multiple Processes Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Multiple Processes Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 10 -n 1000 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -m 1000"
        ${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -n 300 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -m 10 >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}


	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Boundary & Limitation Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Boundary & Limitation Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1 -v ${DEVICE} -w ${WORK_PLACE} -b"
        ${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 1  -v ${DEVICE} -w ${WORK_PLACE} -b>>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Growing Fillup Test:"
	f_growtest
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Stress Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Stress Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 10 -n 500000 -v ${DEVICE} -w ${WORK_PLACE} -s"
        ${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 10 -n 500000  -v ${DEVICE} -w ${WORK_PLACE} -s>>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}
	
	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Destructive Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Destructive Test, CMD:${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 2 -n 500 -d 10  -v ${DEVICE} -w ${WORK_PLACE} -p "
        ${SUDO} ${INDEXED_DIRS_TEST_BIN} -i 2 -n 500 -d 10  -v ${DEVICE} -w ${WORK_PLACE} -p>>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
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
f_LogRunMsg ${RUN_LOG_FILE} "=====================Indexed dirs tests start:  `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Indexed dirs tests start:  `date`====================="

for BLOCKSIZE in 512 1024 4096;do
	for CLUSTERSIZE in 4096 32768 1048576;do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} bs and ${CLUSTERSIZE} cs ->\n"
		f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} bs and ${CLUSTERSIZE} cs ->"
		f_runtest
	done
done

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Indexed dirs tests end: `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Indexed dirs tests end: `date`====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
