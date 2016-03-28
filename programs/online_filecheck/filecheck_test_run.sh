#!/bin/bash
#
# filecheck_run.sh
#
# descritpion: 	This script will perform a thorough single node test on online
#               filecheck for OCFS2. Following testcases will be involed.
#
# 1. inode block: inode number, inode generation, block ECC
#
# Author:	Eric Ren,	zren@suse.com
# History:	22 Mar, 2016
#
# Copyright (C) 2016 SUSE.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation, version 2.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

################################################################################
# Global Variables
################################################################################
if [ -f `dirname ${0}`/o2tf.sh ]; then
	. `dirname ${0}`/o2tf.sh
fi

USERNAME="`id -un`"
GROUPNAME="`id -gn`"

SUDO="`which sudo` -u root"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
MOUNT_BIN="`which sudo` -u root `which mount.ocfs2`"
UMOUNT_BIN="`which sudo` -u root `which umount`"

LABELNAME=ocfs2-filecheck-tests
OCFS2_DEVICE=
MOUNT_POINT=
MOUNT_OPTS="errors=continue"
FEATURES="local,metaecc"
BLOCKSIZE=4096
CLUSTERSIZE=32768
WORKSPACE=

DEFAULT_LOG="filecheck-test-logs"
LOG_OUT_DIR=
DETAIL_LOG_FILE=

TEST_NO=0
TEST_PASS=0

set -o pipefail

################################################################################
# Utility Functions
################################################################################

f_usage()
{
        echo "usage: `basename ${0}` [-o output log dir] <-d <device>> <mountpoint>"
        echo "  -o output directory for logs."
        echo "  -d specify the device to be formated as an ocfs2 volume."
        echo "  <mountpoint> path of mount point where the ocfs2 volume will be mounted on."
}

f_getoptions()
{
        if [ $# -eq 0 ];then
                f_usage
                exit 1
        fi

        while getopts "o:d:" options; do
                case $options in
                o ) LOG_OUT_DIR="$OPTARG";;
                d ) OCFS2_DEVICE="$OPTARG";;
                * ) f_usage
                    exit 1;;
                esac
        done
        shift $(($OPTIND - 1))
        MOUNT_POINT=${1}
}

f_setup()
{
        f_getoptions $*

        if [ -z "${MOUNT_POINT}" -o ! -d ${MOUNT_POINT} ];then
                echo "Mount point ${MOUNT_POINT} does not exist."
                f_usage
        fi

        LOG_OUT_DIR="${LOG_OUT_DIR:-$DEFAULT_LOG}/filecheck"
        mkdir -p ${LOG_OUT_DIR} || exit 1

        LOG_SURFIX=$(date +%Y%m%d-%H%M%S)
        DETAIL_LOG_FILE="${LOG_OUT_DIR}/filecheck_test-${LOG_SURFIX}.log"
        RUN_LOG_FILE="$LOG_OUT_DIR/run-filecheck-${LOG_SURFIX}.log"

        WORKSPACE="${MOUNT_POINT}/filecheck_test_place"
}

f_do_mkfs_and_mount()
{
       echo -n "Mkfsing device:" | tee -a ${DETAIL_LOG_FILE}

       echo y | ${MKFS_BIN} --fs-features=${FEATURES} --label ${LABELNAME} -b ${BLOCKSIZE} -C ${CLUSTERSIZE} ${OCFS2_DEVICE} >> ${DETAIL_LOG_FILE} 2>&1
       RET=$?
       f_echo_status ${RET} | tee -a ${DETAIL_LOG_FILE}
       f_exit_or_not ${RET}

       echo -n "Mounting device to ${MOUNT_POINT}:" | tee -a ${DETAIL_LOG_FILE}
       ${MOUNT_BIN} -t ocfs2 -o ${MOUNT_OPTS} ${OCFS2_DEVICE} ${MOUNT_POINT} >> ${DETAIL_LOG_FILE} 2>&1
       RET=$?
       f_echo_status ${RET} | tee -a ${DETAIL_LOG_FILE}
       f_exit_or_not ${RET}

       ${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
       ${SUDO} chmod -R 777 ${MOUNT_POINT}

       mkdir -p ${WORKSPACE} || exit 1
}

f_do_umount()
{
    echo -n "Umounting device to ${MOUNT_POINT}:" | tee -a ${DETAIL_LOG_FILE}

    rm -rf ${WORKSPACE} || exit 1

    ${UMOUNT_BIN} ${MOUNT_POINT} >> ${DETAIL_LOG_FILE} 2>&1
    RET=$?
    f_echo_status ${RET} | tee -a ${DETAIL_LOG_FILE}
    f_exit_or_not ${RET}
}

f_runtest()
{
       # put testing units here
       f_do_mkfs_and_mount

       ((TEST_NO++))
       echo -ne "[${TEST_NO}] Inode block corrupt & check & fix:" | tee -a ${RUN_LOG_FILE}
       inode_block_test.sh -d ${OCFS2_DEVICE} -m ${MOUNT_POINT} -l ${LOG_OUT_DIR}
       RET=$?
       f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
       f_exit_or_not ${RET}
       ((TEST_PASS++))

       f_do_umount
}

function f_cleanup()
{
        :
}

################################################################################
# Main Entry
################################################################################

#redefine the int signal hander
trap 'echo -ne "\n\n">>${RUN_LOG_FILE}; echo "Interrupted by Ctrl+C,Cleanuping \
... "|tee -a ${RUN_LOG_FILE}; f_cleanup; exit 1' SIGINT

f_setup $*

START_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Filecheck tests start:  `date`\
=====================\n"
f_LogMsg ${DETAIL_LOG_FILE} "=====================Filecheck tests start:  `date`\
====================="

f_runtest

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Filecheck tests end: `date`\
=====================\n"
f_LogMsg ${DETAIL_LOG_FILE} "=====================Filecheck tests end: `date`\
====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
