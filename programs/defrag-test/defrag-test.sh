#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# defrag-test.sh
#
# description:  This script will perform a thorough single-node/multi-nodes
#		test on ocfs2's online-defragmentation.
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      10 Mar 2011
#
#
# Copyright (C) 2011 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.
#
# his program is distributed in the hope that it will be useful,
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

BINDIR=`dirname ${0}`

BLOCKSIZE=
CLUSTERSIZE=
SLOTS=0
JOURNALSIZE=0
BLOCKS=0
LABELNAME=ocfs2-defrag-tests
DEVICE=
WORK_PLACE_DIRENT=ocfs2-defrag-tests
WORK_PLACE=

MOUNT_OPTS=
AIO_OPT=

DSCV_TEST=
LISTENER_ADDR=
LISTENER_PORT=

VERI_TEST=
VERI_LOG=

FRAG_BIN="${BINDIR}/frager"
DEFRAG_BIN="${BINDIR}/defrager"
MULTI_DEFRAG_BIN="${BINDIR}/multi_defrager"
VERIFY_FILE_BIN="${BINDIR}/verify_file"
REFLINK_BIN="`which reflink`"
FILL_HOLES_BIN="${BINDIR}/fill_holes"
VERIFY_HOLES_BIN="${BINDIR}/verify_holes"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
VERIFY_LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
VERIFY_HOLES_LOG_FILE=

MULTI_TEST=
MPI_HOSTS=
MPI_RANKS=
MPI_ACCESS_METHOD="rsh"
MPI_PLS_AGENT_ARG="-mca pls_rsh_agent rsh:ssh"
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
        echo "usage: `basename ${0}` [-W] [-D] [-o logdir] <-d device> [-m multi_hosts] \
[-a access_method] <mountpoint path>"
        echo "       -o output directory for the logs"
        echo "       -d block device name used for ocfs2 volume"
        echo "       -W enable data=writeback mode"
        echo "       -D enable destructive test"
        echo "       <mountpoint path> specify the testing mounting point."
        exit 1;

}

function f_getoptions()
{
        if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "o:DWhd:m:a:" options; do
                case $options in
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
		W ) MOUNT_OPTS="data=writeback";;
		D ) DSCV_TEST="1";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		m ) MULTI_TEST=1
		    MPI_HOSTS="$OPTARG";;
                h ) f_usage;;
                * ) f_usage;;
                esac
        done
        shift $(($OPTIND -1))
        MOUNT_POINT=${1}
}

function f_verify_hosts()
{
	local -a hosts=${1}
	local host=
	local -i slots=0

	hosts=`echo ${hosts}|tr "[,]" "[ ]"`

	for host in `echo $hosts`;do
		ping -q -w 2 $host >/dev/null 2>&1 || {
			echo "$host is unreachable."
			return 1
		}
		((slots++))
	done

	SLOTS=${slots}

	return 0
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
                        if [ "`dirname ${MOUNT_POINT}`" = "/" ]; then
                                MOUNT_POINT="`dirname ${MOUNT_POINT}``basename \
					     ${MOUNT_POINT}`"
                        else
                                MOUNT_POINT="`dirname ${MOUNT_POINT}`/`basename \
					     ${MOUNT_POINT}`"
                        fi
                fi
        fi

	if [ -n "${MULTI_TEST}" ];then
		if [ -z "${MPI_HOSTS}" ];then
			echo "please specify the required mpi hosts in terms of CSV."
			f_usage
		else
			f_verify_hosts ${MPI_HOSTS} || {
				f_usage
			}

			if [ "$MPI_ACCESS_METHOD" = "rsh" ];then
				MPI_PLS_AGENT_ARG="-mca pls_rsh_agent rsh:ssh"
			else
				MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"
			fi
		fi

		WORK_PLACE_DIRENT=${WORK_PLACE_DIRENT}-multi-nodes
		LABELNAME=${LABELNAME}-multi-nodes
	fi

        LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	VERIFY_LOG_DIR=${LOG_DIR}/verify_logs
	${MKDIR_BIN} -p ${VERIFY_LOG_DIR} || exit 1

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-defrag-tests-run.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-defrag-tests.log"
	VERIFY_HOLES_LOG_FILE="`dirname ${LOG_DIR}`/`basename \
${LOG_DIR}`/`date +%F-%H-%M-%S`-verify-holes.log"
}

function f_should_return()
{
	if [ "${1}" != "0" ];then
		return 1
	fi
}

function f_basic_test_one_run()
{
	local filename=${1}
	local logfile=${2}
	local start=${3}
	local len=${4}
	local thresh=${5}
	local filesize=${6}
	local chunksize=${7}
	local reflink=${8}
	local ignore_fileverify=${9}

	local md5sum_orig=
	local md5sum_defrag=
	local ret=

	md5sum_org=`md5sum ${filename} |awk '{print $1}'`
	f_LogMsg ${LOG_FILE} "Defrag file ${filename} from ${start} to \
$((${star}+${len})) with ${thresh} threshold"
	f_LogMsg ${LOG_FILE} "CMD: ${DEFRAG_BIN} -s ${start} -l ${len} -t \
${thresh} ${filename}"
	${DEFRAG_BIN} -s ${start} -l ${len} -t ${thresh} ${filename} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}
	md5sum_defrag=`md5sum ${file} |awk '{print $1}'`
	f_LogMsg ${LOG_FILE} "Verify md5sum of ${filename} after defragmentation"
	if [ "${md5sum_org}" != "${md5sum_defrag}" ];then
		f_LogMsg ${LOG_FILE} "md5sum mismatch, original md5sum: \
${md5sum_org}, defraged md5sum: ${md5sum_defrag}"
		return -1
	fi

	if [ "${ignore_fileverify}" == "1" ];then
		return 0
	fi	

	f_LogMsg ${LOG_FILE} "Verify chunks of ${filename} after defragmentation"
	f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_FILE_BIN} -f ${filename} -o \
${logfile} -l ${filesize} -k ${chunksize}"
	${VERIFY_FILE_BIN} -f ${filename} -o ${logfile} -l ${filesize} -k \
${chunksize} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	if [ -n "${reflink}" -a "${reflink}" != "0" ];then
		f_LogMsg ${LOG_FILE} "Verify chunks of reflink:(${reflink}) after \
defragmentation"
		f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_FILE_BIN} -f ${reflink} -o \
${logfile} -l ${filesize} -k ${chunksize}"
		${VERIFY_FILE_BIN} -f ${reflink} -o ${logfile} -l ${filesize} -k \
${chunksize} >>${LOG_FILE} 2>&1
		ret=$?
		f_should_return ${ret}
	fi

	return ${ret}
}

function f_get_rand()
{
	local min=${1}
	local max=${2}
	local range=

	if [ "${min}" -gt "${max}" ];then
		echo "Upper limit of f_get_rand needs to be large than lower limit"
		return 1
	fi

	if [ "${min}" -eq "${max}" ];then
		echo ${min}
		return 0
	fi

	local range=$((${max}-${min}))

	rand=$((${min}+${RANDOM}%${range}))

	echo ${rand}
}

# num_children needs to be more than 2, num_files as well.
function f_basic_test()
{
	local num_children=${1}
	local num_files=${2}
	local file_size=${3}
	local chunk_size=${4}
	local refcount_flag=${5}
	local reflink=
	local dir_index=
	local dir=
	local file_index=
	local file=
	local log_file=
	local start=
	local len=
	local thresh=
	local ret=

	f_LogMsg ${LOG_FILE} "Generating fragmented files"
	f_LogMsg ${LOG_FILE} "CMD: ${FRAG_BIN} -n ${num_files} -m ${num_children} \
-l ${file_size} -k ${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} ${refcount_flag}"
	${FRAG_BIN} -n ${num_files} -m ${num_children} -l ${file_size} -k \
${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} ${refcount_flag}>>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	dir_index=1
	file_index=1
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	if [ -n "${refcount_flag}" ];then
		reflink="${WORK_PLACE}/${dir}/refile-$((${file_index}-1))"
	fi
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} 1048576 \
${file_size} ${chunk_size} ${reflink}
	ret=$?
	f_should_return ${ret}

	dir_index=1
	file_index=2
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	if [ -n "${refcount_flag}" ];then
		reflink="${WORK_PLACE}/${dir}/refile-$((${file_index}-1))"
	fi
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} ${file_size} \
${file_size} ${chunk_size} ${reflink}
	ret=$?
	f_should_return ${ret}

	dir_index=1
	file_index=3
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	if [ -n "${refcount_flag}" ];then
		reflink="${WORK_PLACE}/${dir}/refile-$((${file_index}-1))"
	fi
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} $((${file_size}/2)) \
${file_size} ${chunk_size} ${reflink}
	ret=$?
	f_should_return ${ret}

	f_LogMsg ${LOG_FILE} "Random tests over random start, len and thresholds."
	for dir_index in `seq 2 ${num_children}`;do
		dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
		for file_index in `seq ${num_files}`;do
			file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
			log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
			if [ -n "${refcount_flag}" ];then
				reflink="${WORK_PLACE}/${dir}/refile-$((${file_index}-1))"
			fi
			start=`f_get_rand 0 ${file_size}`
			len=`f_get_rand 0 $((${file_size}-${start}))`
			thresh=`f_get_rand ${CLUSTERSIZE} ${file_size}`
			if [ "$((file_index%2))" == "0" ];then
				f_basic_test_one_run ${file} ${log_file} ${start} ${len} \
${thresh} ${file_size} ${chunk_size} ${reflink}
			else
				f_basic_test_one_run ${reflink} ${log_file} ${start} ${len} \
${thresh} ${file_size} ${chunk_size} ${file}
			fi
			ret=$?
			f_should_return ${ret}
		done
	done

	return ${ret}
}

function f_hole_test()
{
	local num_children=${1}
	local num_files=${2}
	local file_size=${3}
	local chunk_size=${4}
	local num_holes=${5}
	local dir_index=
	local dir=
	local file_index=
	local file=
	local log_file=
	local start=
	local len=
	local thresh=
	local ret=

	f_LogMsg ${LOG_FILE} "Generating fragmented files"
	f_LogMsg ${LOG_FILE} "CMD: ${FRAG_BIN} -n ${num_files} -m ${num_children} \
-l ${file_size} -k ${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} "
	${FRAG_BIN} -n ${num_files} -m ${num_children} -l ${file_size} -k \
${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	dir_index=1
	file_index=1
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	f_LogMsg ${LOG_FILE} "Fill holes, CMD: :${FILL_HOLES_BIN} -f -o \
${VERIFY_HOLES_LOG_FILE} -i ${num_holes} ${file} ${file_size}"
	${FILL_HOLES_BIN} -f -o ${VERIFY_HOLES_LOG_FILE} -i ${num_holes} \
${file} ${file_size} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}
	sync
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} 1048576 \
${file_size} ${chunk_size} 0 1
	ret=$?
	f_should_return ${ret}
	f_LogMsg ${LOG_FILE} "Verify holes for defragmented ${file}"
	f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file}"
	${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file} || return -1

	dir_index=1
	file_index=2
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	f_LogMsg ${LOG_FILE} "Fill holes, CMD: :${FILL_HOLES_BIN} -f -o \
${VERIFY_HOLES_LOG_FILE} -i ${num_holes} ${file} ${file_size}"
	${FILL_HOLES_BIN} -f -o ${VERIFY_HOLES_LOG_FILE} -i ${num_holes} \
${file} ${file_size} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}
	sync
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} ${file_size} \
${file_size} ${chunk_size} ${reflink} 0 1
	ret=$?
	f_should_return ${ret}
	f_LogMsg ${LOG_FILE} "Verify holes for defragmented ${file}"
	f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file}"
	${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file} || return -1

	dir_index=1
	file_index=3
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
	f_LogMsg ${LOG_FILE} "Fill holes, CMD: :${FILL_HOLES_BIN} -f -o \
${VERIFY_HOLES_LOG_FILE} -i ${num_holes} ${file} ${file_size}"
	${FILL_HOLES_BIN} -f -o ${VERIFY_HOLES_LOG_FILE} -i ${num_holes} \
${file} ${file_size} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}
	sync
	f_basic_test_one_run ${file} ${log_file} 0 ${file_size} $((${file_size}/2)) \
${file_size} ${chunk_size} ${reflink} 0 1
	ret=$?
	f_should_return ${ret}	
	f_LogMsg ${LOG_FILE} "Verify holes for defragmented ${file}"
	f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file}"
	${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${file} || return -1
}

function f_destructive_test()
{
	local num_children=${1}
	local num_files=${2}
	local file_size=${3}
	local chunk_size=${4}
	local reflink=
	local dir_index=
	local dir=
	local file_index=
	local file=
	local log_file=
	local ret=
	local yesno=

	f_LogMsg ${LOG_FILE} "Generating fragmented files"
	f_LogMsg ${LOG_FILE} "CMD: ${FRAG_BIN} -n ${num_files} -m ${num_children} \
-l ${file_size} -k ${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE}"
	${FRAG_BIN} -n ${num_files} -m ${num_children} -l ${file_size} -k \
${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	sleep 20

	dir_index=1
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	f_LogMsg ${LOG_FILE} "Massive defragmentation on dir ${WORK_PLACE}/${dir} \
before nodes down"
	for file_index in `seq ${num_files}`;do
		file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
		f_basic_test_one_run ${file} ${log_file} 0 ${file_size} ${file_size} \
${file_size} ${chunk_size} &
	done
	while [ "${yesno}" != "Y" -a "${yesno}" != "y" ];do
		if [ "${yesno}" == "n" -o "${yesno}" == "N" ];then
			f_LogMsg ${LOG_FILE} "Abort crashing."
			return $ret
		fi
		echo -n "Do you really want to crash the box?[Y/N]"
		read yesno
	done

	f_LogMsg ${LOG_FILE} "Crashing the box"
	echo b>/proc/sysrq-trigger
}

function f_multi_nodes_test()
{
	local num_children=${1}
	local num_files=${2}
	local file_size=${3}
	local chunk_size=${4}
	local reflink=
	local dir_index=
	local dir=
	local file_index=
	local file=
	local log_file=
	local start=0
	local len=
	local thresh=
	local ret=

	f_LogMsg ${LOG_FILE} "Generating fragmented files"
	f_LogMsg ${LOG_FILE} "CMD: ${FRAG_BIN} -n ${num_files} -m ${num_children} \
-l ${file_size} -k ${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE}"
	${FRAG_BIN} -n ${num_files} -m ${num_children} -l ${file_size} -k \
${chunk_size} -o ${VERIFY_LOG_DIR} -w ${WORK_PLACE} >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	sync

	dir_index=1
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	f_LogMsg ${LOG_FILE} "Multi-nodes defragmentation on dir ${WORK_PLACE}"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} \
${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_DEFRAG_BIN} -s 0 -l ${file_size} \
-t ${CLUSTERSIZE} -w ${WORK_PLACE}/${dir} -r -v"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host \
${MPI_HOSTS} ${MULTI_DEFRAG_BIN} -s 0 -l ${file_size} -t ${CLUSTERSIZE} -w \
${WORK_PLACE}/${dir} -r -v >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	for file_index in `seq ${num_files}`;do
		file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
		log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
		f_LogMsg ${LOG_FILE} "Verify chunks of ${file} after \
multi-defragmentation"
		f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_FILE_BIN} -f ${file} -o \
${log_file} -l ${file_size} -k ${chunk_size}"
		${VERIFY_FILE_BIN} -f ${file} -o ${log_file} -l ${file_size} \
-k ${chunk_size} >>${LOG_FILE} 2>&1
		ret=$?
		f_should_return ${ret}
	done

	dir_index=2
	dir=`ls -1 ${WORK_PLACE}|sed -n "${dir_index}p"`
	f_LogMsg ${LOG_FILE} "Multi-nodes defragmentation on dir ${WORK_PLACE}"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} \
${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_DEFRAG_BIN} -s 0 -l ${file_size} \
-t ${file_size} -w ${WORK_PLACE}/${dir} -r -v"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host \
${MPI_HOSTS} ${MULTI_DEFRAG_BIN} -s 0 -l ${file_size} -t ${file_size} -w \
${WORK_PLACE}/${dir} -r -v >>${LOG_FILE} 2>&1
	ret=$?
	f_should_return ${ret}

	for file_index in `seq ${num_files}`;do
		file="${WORK_PLACE}/${dir}/file-$((${file_index}-1))"
		log_file="${VERIFY_LOG_DIR}/${dir}/logfile-$((${file_index}-1))"
		f_LogMsg ${LOG_FILE} "Verify chunks of ${file} after \
multi-defragmentation"
		f_LogMsg ${LOG_FILE} "CMD: ${VERIFY_FILE_BIN} -f ${file} -o \
${log_file} -l ${file_size} -k ${chunk_size}"
		${VERIFY_FILE_BIN} -f ${file} -o ${log_file} -l ${file_size} \
-k ${chunk_size} >>${LOG_FILE} 2>&1
		ret=$?
		f_should_return ${ret}
	done

	return ${ret}
}

function f_multi_runner()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "refcount,xattr" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

        ((TEST_PASS++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Multi-nodes Defragmentation Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Multi-nodes Defragmentaiton Test, CMD: \
f_multi_nodes_test 10 10 10485760 ${CLUSTERSIZE} -R"
	f_multi_nodes_test 10 10 10485760 ${CLUSTERSIZE} >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
        ((TEST_PASS++))
        f_LogMsg ${LOG_FILE} "Cleanup working place"
        ${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
	RET=$?
	f_exit_or_not ${RET}
}

function f_single_runner()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "refcount,xattr" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	if [ -n "${DSCV_TEST}" ];then
	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Destructive Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Destructive Test, CMD: \
f_destructive_test 10 10 104857600 ${CLUSTERSIZE}"
	f_destructive_test 10 10 104857600 ${CLUSTERSIZE}
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}
	fi

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Basic Fucntional Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Basic Fucntional Test, CMD: \
f_basic_test 10 40 104857600 ${CLUSTERSIZE}"
	f_basic_test 10 40 104857600 ${CLUSTERSIZE} >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Reflink Combination Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Reflink Combination Test, CMD: \
f_basic_test 10 10 10485760 ${CLUSTERSIZE} -R"
	f_basic_test 10 10 10485760 ${CLUSTERSIZE} -R >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Punching Hole Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Punching Hole Test, CMD: \
f_hole_test 10 10 10485760 ${CLUSTERSIZE} 1000"
	f_hole_test 10 10 10485760 ${CLUSTERSIZE} 1000 >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}

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
trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping\
... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT

f_check $*

START_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Defragmentation tests start:  `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Defragmentation tests start:  `date`\
====================="

for BLOCKSIZE in 512 1024 2048 4096;do
	for CLUSTERSIZE in 4096 32768 1048576;do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} \
bs and ${CLUSTERSIZE} cs ->\n"
		f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} bs \
and ${CLUSTERSIZE} cs ->"
		if [ -z "${MULTI_TEST}" ];then
			f_single_runner
		else
			f_multi_runner
		fi
	done
done

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Defragmentation tests end: `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Defragmentation tests end: `date`\
====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
