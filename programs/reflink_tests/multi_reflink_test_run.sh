#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# multi_reflink_test_run.sh
#
# description:  This script will behave as openmpi binary launcher to
#		perform following tests among multiple nodes for
#		refcount on ocfs2.
#
#		1. Basic test
#
#		2. Random test
#
#		3. Mmap test
#
#		4. DirectIO test
#
#		5. Comprehensive(concurrent) test
#
#		6. Inline-data test
#
#		7. Xattr test
#
#		8. Stress test
#
#		9. Destructive test
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      20 Mar 2009
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
if [ -f `dirname ${0}`/o2tf.sh ]; then
        . `dirname ${0}`/o2tf.sh
fi

BLOCKSIZE=
CLUSTERSIZE=
SLOTS=5
JOURNALSIZE=0
BLOCKS=0
DEVICE=
LABELNAME="ocfs2-multi-refcount-tests-`uname -m`"
WORK_PLACE_DIRENT=ocfs2-multi-refcount-tests
WORK_PLACE=
MULTI_REFLINK_TEST_BIN="${BINDIR}/multi_reflink_test"
IFCONFIG_BIN="`which sudo` -u root `which ifconfig`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=

MOUNT_OPTS=
AIO_OPT=

DEFAULT_RANKS=4
MPI_RANKS=
MPI_HOSTS=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
MPI_BTL_ARG="-mca btl tcp,self"
MPI_BTL_IF_ARG=

TEST_NO=0
TEST_PASS=0

set -o pipefail

################################################################################
# Utility Functions
################################################################################
function f_usage()
{
    echo "usage: `basename ${0}` [-r MPI_Ranks] <-f MPI_Hosts> \
[-a access method] [-o logdir] <-d <device>> [-W] [-A] <mountpoint path>"
    echo "       -r size of MPI rank"
    echo "       -a access method for mpi execution,should be ssh or rsh"
    echo "       -f MPI hosts list,separated by comma"
    echo "       -o output directory for the logs"
    echo "       -d specify the device"
    echo "       -i Network Interface name to be used for MPI messaging."
    echo "       -W enable data=writeback mode"
    echo "       -A enable asynchronous io testing mode"
    echo "       <mountpoint path> specify the mounting point."
    exit 1;

}
function f_getoptions()
{
	 if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

	 while getopts "o:d:i:r:f:WAha:" options; do
                case $options in
		r ) MPI_RANKS="$OPTARG";;
                f ) MPI_HOSTS="$OPTARG";;
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		i ) INTERFACE="$OPTARG";;
		W ) MOUNT_OPTS="data=writeback";;
		A ) AIO_OPT=" -A ";;
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
			if [ "`dirname ${MOUNT_POINT}`" = "/" ]; then
				MOUNT_POINT="`dirname ${MOUNT_POINT}``basename \
${MOUNT_POINT}`"
			else
				MOUNT_POINT="`dirname ${MOUNT_POINT}`/`basename \
${MOUNT_POINT}`"
			fi
		fi
	fi

	if [ -z "$MPI_HOSTS" ];then
		f_usage
	else
		echo $MPI_HOSTS|sed -e 's/,/\n/g' >/tmp/$$
		SLOTS=`cat /tmp/$$ |wc -l`
		rm -f /tmp/$$
	fi

	if [ ! -z "${INTERFACE}" ]; then
		${IFCONFIG_BIN} ${INTERFACE} >/dev/null 2>&1 || {
			echo "Invalid NIC";
			f_usage;
		} 
		MPI_BTL_IF_ARG="-mca btl_tcp_if_include ${INTERFACE}"   
	fi;
	MPI_RANKS=${MPI_RANKS:-$DEFAULT_RANKS}

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/multi-refcount-tests-run-\
`uname -m`-`date +%F-%H-%M-%S`.log"
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/multi-refcount-tests-\
`uname -m`-`date +%F-%H-%M-%S`.log"

}

function f_runtest()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "refcount,xattr" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Basic Functional Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Basic Functional Test, CMD:\
${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 \
-n 100 -w ${WORK_PLACE} -f ${AIO_OPT}"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 \
-n 100 -w ${WORK_PLACE} -f ${AIO_OPT} >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Random Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Random Test, CMD:${MPIRUN} \
${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host \
${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 -n 100 -w \
${WORK_PLACE} -r ${AIO_OPT}"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 -n \
100 -w ${WORK_PLACE} -r ${AIO_OPT} >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Mmap Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Mmap Test, CMD:${MPIRUN} \
${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host \
${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 -n 100 -w \
${WORK_PLACE} -m "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 -n \
100 -w ${WORK_PLACE} -m >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] O_DIRECT Test:"
        f_LogMsg ${LOG_FILE} "[${TEST_NO}] O_DIRECT Test, CMD:${MPIRUN} \
${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host \
${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 41943040 -n 100 -w \
${WORK_PLACE} -O ${AIO_OPT}"
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 41943040 -n \
100 -w ${WORK_PLACE} -O ${AIO_OPT} >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
        f_LogMsg ${LOG_FILE} "Cleanup working place"
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Concurrent Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Concurrent Test, CMD:${MPIRUN} \
${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host \
${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 10485760 -n 1000 -w \
${WORK_PLACE} -c "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 10485760 -n \
1000 -w ${WORK_PLACE} -c >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Inline-data Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Inline-data Refcount Test, CMD:\
${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l $((${BLOCKSIZE}-200)) \
-n 100 -w ${WORK_PLACE} -f "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l $((${BLOCKSIZE}-200)) \
-n 100 -w ${WORK_PLACE} -f >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Xattr Combination Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Xattr Combination Test, CMD:\
${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 \
-n 10 -w ${WORK_PLACE} -x 1000 "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -l 104857600 \
-n 10 -w ${WORK_PLACE} -x 1000 >>${LOG_FILE} 2>&1
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
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Stress Test, CMD:${MPIRUN} \
${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np ${MPI_RANKS} --host \
${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -p 1000 -l 2147483648 -n 2000 \
-w ${WORK_PLACE} -s "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} -np \
${MPI_RANKS} --host ${MPI_HOSTS} ${MULTI_REFLINK_TEST_BIN} -i 1 -p 1000 -l \
2147483648 -n 2000 -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	RET=$?
        f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
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

trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping\
... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT

f_setup $*

START_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Multi-nodes refcount tests \
start:  `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Multi-nodes refcount tests \
start:  `date`====================="

for BLOCKSIZE in 512 1024 2048 4096;do
	for CLUSTERSIZE in  4096 32768 1048576;do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} \
bs and ${CLUSTERSIZE} cs ->\n"
                f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} \
bs and ${CLUSTERSIZE} cs ->"
		f_runtest
        done
done

f_cleanup

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Multi-nodes refcount \
tests end: `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Multi-nodes refcount tests \
end: `date`====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
