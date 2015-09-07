#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# multi_index_dir_run.sh
#
# description:  This script will behave as openmpi binary launcher to 
#		perform following tests among multiple nodes for
#		indexed-dirs on ocfs2.
#
#		1. Grow test
#		
#		2. Rename test
#		
#		3. Read test
#		
#		4. Unlink test
#
#		5. Fillup test
#		
#		6. Stress test 
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
DEVICE=
LABELNAME=ocfs2-multi-indexed-dirs-tests
WORK_PLACE_DIRENT=multi-indexed-dirs-tests
WORK_PLACE=
MULTI_INDEXED_DIRS_TEST_BIN="${BINDIR}/multi_index_dir"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=

DEFAULT_RANKS=4
MPI_RANKS=
MPI_HOSTS=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"
MPI_BTL_ARG="-mca btl tcp,self"
MPI_BTL_IF_ARG=

TEST_NO=0
TEST_PASS=0

MULTI_INDEXED_DIRS_TEST_BIN="${BINDIR}/multi_index_dir"

set -o pipefail

################################################################################
# Utility Functions
################################################################################
function f_usage()
{
    echo "usage: `basename ${0}` [-r MPI_Ranks] <-f MPI_Hosts> [-a access method] [-o logdir] <-d <device>> <mountpoint path>"
    echo "       -r size of MPI rank"
    echo "       -a access method for process propagation,should be ssh or rsh,set ssh as a default method when omited."
    echo "       -f MPI hosts list,separated by comma,e.g -f node1.us.oracle.com,node2.us.oracle.com."
    echo "       -o output directory for the logs"
    echo "       -d specify the device which has been formated as an ocfs2 volume."
    echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
    exit 1;

}
function f_getoptions()
{
	 if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

	 while getopts "o:d:r:f:a:h:" options; do
                case $options in
		r ) MPI_RANKS="$OPTARG";;
                f ) MPI_HOSTS="$OPTARG";;
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
                h ) f_usage
                    exit 1;;
                * ) f_usage
                   exit 1;;
                esac
        done
	shift $(($OPTIND -1))
	MOUNT_POINT=${1}
}

function f_setup()
{
	if [ "${UID}" = "0" ];then
		echo "Should not run tests as root"
		exit 1
	fi

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

	if [ -z "$MPI_HOSTS" ];then
		f_usage
	fi

	MPI_RANKS=${MPI_RANKS:-$DEFAULT_RANKS}

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-multi-indexed-dirs-tests-run.log"
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-multi-indexed-dirs-tests.log"
}

function f_runtest()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} ${DEVICE} "indexed-dirs,noinline-data" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Remote Mount amongs nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS}
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Basic Grow Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Basic Grow Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 10 -n 4000 -w ${WORK_PLACE} -g"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 10 -n 4000 -w ${WORK_PLACE} -g >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Rename Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Rename Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 2000 -w ${WORK_PLACE} -m"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 2000 -w ${WORK_PLACE} -m >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Read Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Read Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 3000 -w ${WORK_PLACE} -r"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 3000 -w ${WORK_PLACE} -r >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}
 
	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Unlink Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Unlink Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 2000 -w ${WORK_PLACE} -u"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 2000 -w ${WORK_PLACE} -u >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Fillup Test:"
        f_LogMsg ${LOG_FILE} "[${TEST_NO}] Fillup Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 10000 -w ${WORK_PLACE} -f"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 1 -n 10000 -w ${WORK_PLACE} -f >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
        f_LogMsg ${LOG_FILE} "Cleanup working place"
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}
	
	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Stress Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Stress Test, CMD:${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 10 -n 60000 -w ${WORK_PLACE} -s"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_INDEXED_DIRS_TEST_BIN} -i 10 -n 60000 -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount volume ${LABELNAME} amongs nodes ${MPI_HOSTS}:"
        f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
}

f_cleanup()
{
	:
}

################################################################################
# Main Entry
################################################################################

trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT

f_setup $*

START_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Multi-nodes indexed dirs tests start:  `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Multi-nodes indexed dirs tests start:  `date`====================="

for BLOCKSIZE in 512 1024 2048 4096
do
        for CLUSTERSIZE in  4096 32768 1048576
        do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} bs and ${CLUSTERSIZE} cs ->\n"
                f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} bs and ${CLUSTERSIZE} cs ->"
		f_runtest
        done
done
f_cleanup

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Multi-nodes indexed dirs tests end: `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Multi-nodes indexed dirs tests end: `date`====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
