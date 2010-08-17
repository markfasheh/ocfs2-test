#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# reflink_test_run.sh
#
# description:  This script will perform a thorough single-node test on
#		refcount for ocfs2. Following testcases will be involved.
#
#		1. Basic func test
#
#		2. Random test
#
#		3. Mmap test
#
#		4. DirectIO test
#		
#		5. CoW test on punching holes
#		
#		6. CoW test on truncating
#
#		7. Concurrent test
#
#		8. Boundary test
#
#		9. Stress test
#
#		10. Inline-data test
#
#		11. Xattr combination test
#
#		12. OracleVM simulation test
#
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      18 Mar 2009
#
#
# Copyright (C) 2008 Oracle.  All rights reserved.
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
SLOTS=4
JOURNALSIZE=0
BLOCKS=0
LABELNAME=ocfs2-refcount-tests
DEVICE=
WORK_PLACE_DIRENT=ocfs2-refcount-tests
WORK_PLACE=

MOUNT_OPTS=

DSCV_TEST=
LISTENER_ADDR=
LISTENER_PORT=

VERI_TEST=
VERI_LOG=

REFLINK_TEST_BIN="${BINDIR}/reflink_test"
FILL_HOLES_BIN="${BINDIR}/fill_holes"
VERIFY_HOLES_BIN="${BINDIR}/verify_holes"

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
        echo "usage: `basename ${0}` [-D <-a remote_listener_addr> <-p port>] \
[-v verify_log] [-W] [-o logdir] <-d device> <mountpoint path>"
        echo "       -o output directory for the logs"
        echo "       -d block device name used for ocfs2 volume"
        echo "       -W enable data=writeback mode"
	echo "       -D enable destructive test,it will crash the testing node,\
be cautious, you need to specify listener addr and port then"
        echo "       <mountpoint path> specify the testing mounting point."
        exit 1;

}

function f_getoptions()
{
        if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "o:WDhd:a:p:v:" options; do
                case $options in
                o ) LOG_DIR="$OPTARG";;
                d ) DEVICE="$OPTARG";;
		W ) MOUNT_OPTS="data=writeback";;
		D ) DSCV_TEST="1";;
		a ) LISTENER_ADDR="$OPTARG";;
		p ) LISTENER_PORT="$OPTARG";;
		v ) VERI_LOG="$OPTARG";;
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
                        if [ "`dirname ${MOUNT_POINT}`" = "/" ]; then
                                MOUNT_POINT="`dirname ${MOUNT_POINT}``basename \
					     ${MOUNT_POINT}`"
                        else
                                MOUNT_POINT="`dirname ${MOUNT_POINT}`/`basename \
					     ${MOUNT_POINT}`"
                        fi
                fi
        fi

	if [ -n "${DSCV_TEST}" ];then
		if [ -z "${LISTENER_ADDR}" -o -z "${LISTENER_PORT}" ];then
			echo "You need to specify listener address and port in destructive test."
			exit 1
		fi
	fi

	if [ -n "${VERI_LOG}" ];then
		if [ ! -f "${VERI_LOG}" ];then
			echo "Please specify a legal verify log file."
			exit 1
		else
			VERI_TEST="1"
		fi
	fi

        LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1

        RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-reflink-tests-run.log"
        LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-reflink-tests.log"
        VERIFY_HOLES_LOG_FILE="`dirname ${LOG_DIR}`/`basename \
${LOG_DIR}`/verify-holes.log"

}

function f_reflink_bash_utils_test()
{
	#${1} specify the reflink_nums
	#${2} specfiy the reflink_size
	local ref_counts=${1}
	local ref_size=${2}
	local bs=
	local count=
	local skip=
	local target_pfx=${WORK_PLACE}/copy
	local target=
	local i=

	local orig=${WORK_PLACE}/orig
	local HUNKSIZE=1048576

	for bs in ${BLOCKSIZE} ${CLUSTERSIZE} ${HUNKSIZE};do
		count=$((${ref_size}/${bs}))
		dd if=/dev/zero of=${orig} bs=${bs} count=${count}
		if [ "$?" -ne "0" ]; then
			return -1
		fi

		for i in $(seq ${ref_counts});do
			target=${target_pfx}-${i}
			reflink ${orig} ${target}
		done

		for i in $(seq ${ref_counts});do
			target=${target_pfx}-${i}
			skip=$((${RANDOM}%${count}))
			dd if=/dev/random of=${target} bs=${bs} count=1 \
			   seek=${skip}

			if [ "$?" -ne "0" ]; then
				return -1
			fi
		done

		${RM_BIN} -rf ${target_pfx}*
		${RM_BIN} -rf ${orig}
	done

	return 0
}

function f_fill_and_verify_holes()
{

	#${1} specify the reflink_nums
	#${2} specfiy the reflink_size
	#${3} specify the hole_nums
	local ref_counts=${1}
	local ref_size=${2}
	local hole_nums=${3}
	local orig=${WORK_PLACE}/orig
	local target_pfx=${WORK_PLACE}/copy
	local target=
	local i=

	f_LogMsg ${LOG_FILE} "Fill Holes, CMD:${FILL_HOLES_BIN} -f -o \
${VERIFY_HOLES_LOG_FILE} -i ${hole_nums} ${orig} ${ref_size}"

	${FILL_HOLES_BIN} -f -o ${VERIFY_HOLES_LOG_FILE} -i ${hole_nums} \
${orig} ${ref_size}

	if [ "$?" -ne "0" ];then
		return -1
	fi

	sync

	f_LogMsg ${LOG_FILE} "Reflink inode with hole to ${ref_counts} reflinks"

	for i in $(seq ${ref_counts});do

		target=${target_pfx}-${i}
		reflink ${orig} ${target}

	done

	f_LogMsg ${LOG_FILE} "Verify holes for ${ref_counts} reflinks"
	for i in $(seq 1 ${ref_counts});do

		target=${target_pfx}-${i}

		f_LogMsg ${LOG_FILE} "Verify holes for reflink[${i}]:${target}"
		${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${target}
		if [ "$?" -ne "0" ];then
			return -1
		fi
        done

	f_LogMsg ${LOG_FILE} "Verify holes for original file"
	${VERIFY_HOLES_BIN} ${VERIFY_HOLES_LOG_FILE} ${orig} || return -1

	return 0
}

function f_verify_reflinks_holes()
{
	#${1} specify the reflink_nums

	local ref_counts=${1}
	local orig=${WORK_PLACE}/original_holes_refile
	local target=
	local i=
	local logfile=

	f_LogMsg ${LOG_FILE} "Verify holes for ${ref_counts} reflinked files"
	for i in $(seq 0 ${ref_counts});do
		if [ "${i}" -eq "${ref_counts}" ];then
			target=${orig}
			logfile=${VERIFY_HOLES_LOG_FILE}

		else
			target=${orig}r${i}
			logfile=${VERIFY_HOLES_LOG_FILE}r${i}
		fi

		f_LogMsg ${LOG_FILE} "Verify holes for reflink[${i}]:${target} \
from logfile ${logfile}"

		f_LogMsg ${LOG_FILE} "${VERIFY_HOLES_BIN} -v ${logfile} \
${target}"

		${VERIFY_HOLES_BIN} -v ${logfile} ${target}

		if [ "$?" -ne "0" ];then
			return -1
		fi
	done
}

function f_ovmtest()
{
	#{1} specify the size of original file, in terms of M.
	#{2} specify the iterations.

	local size=${1}
	local iterations=${2}

	local bs=1024k

	local orig_file=${WORK_PLACE}/ovm_disk.img
	local snap_postfix=snap
	local cp_postfix=cp

	local -i i=0
	local -i j=0
	local -i offset=0
	local -i interval=0
	local -i volume_sz=

	interval=$((${size}/${iterations}))

	f_LogMsg ${LOG_FILE} "Prepare original ovm image with ${size}M"

	dd if=/dev/zero of=${orig_file} bs=${bs} count=${size} >/dev/null 2>&1

	if [ "$?" -ne "0" ]; then
		return -1
	fi

	f_LogMsg ${LOG_FILE} "Reflink image to ${orig_file}.${snap_postfix}${i}"

	reflink ${orig_file} ${orig_file}.${snap_postfix}${i}
	cp ${orig_file} ${orig_file}.${cp_postfix}${i}

	removed=0
	check_head=0

	for i in $(seq 1 ${iterations});do

		check_end=$((${i}-1))
		disk_free_k=`df |grep ${DEVICE}|awk '{print $4}'`
		rc=`echo "${disk_free_k}<$[2*${size}*1024]"|bc`
		if [ "${rc}" = "1" ];then
			f_LogMsg ${LOG_FILE} "Remove #${removed} snapshot and \
copy to release space"
			rm -rf ${orig_file}.${snap_postfix}${removed}
			rm -rf ${orig_file}.${cp_postfix}${removed}
			check_head=$((${removed}+1))
			removed=$((${removed}+1))
		fi
		f_LogMsg ${LOG_FILE} "Random Writes(#${i}) in ${offset} Pos"
		dd if=/dev/random of=${orig_file} seek=${offset} bs=1024 \
count=$((${RANDOM}/1024)) conv=notrunc>/dev/null 2>&1
		sync

		f_LogMsg ${LOG_FILE} "Do #${i} reflink and copy"
		reflink ${orig_file} ${orig_file}.${snap_postfix}${i}
		cp ${orig_file} ${orig_file}.${cp_postfix}${i}

		f_LogMsg ${LOG_FILE} "Do #${i} data integrity verification"
		for j in $(seq ${check_head} ${check_end});do

			f_LogMsg ${LOG_FILE} "Do data integrity verification \
for #${j} reflink and copy"
			sum1=`md5sum ${orig_file}.${snap_postfix}${j}|awk '{print $1}'`
			sum2=`md5sum ${orig_file}.${cp_postfix}${j}|awk '{print $1}'`
			if [ "${sum1}" != "${sum2}" ];then
				f_LogMsg ${LOG_FILE} "Verification for #${j} reflink and copy failed."
				f_LogMsg ${LOG_FILE} "Md5sum for ${orig_file}.${snap_postfix}${j}:${sum1}"
				f_LogMsg ${LOG_FILE} "Md5sum for ${orig_file}.${cp_postfix}${j}:${sum2}"
				return 1
			fi
		done

		offset=$((${offset}+${interval}))
	done

	return 0
}

function f_runtest()
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

	if [ -n "${VERI_TEST}" ];then
	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Verify Test After Desctruction :"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Verify Test After Desctruction, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 10 -p 10 -l 1638400 -d ${DEVICE} -w ${WORK_PLACE} -v ${VERI_LOG} "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 10 -p 10 -l 1638400 -d ${DEVICE} -w \
${WORK_PLACE} -v ${VERI_LOG} >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	exit ${RET}
	fi

	if [ -n "${DSCV_TEST}" ];then
	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Destructive Test For DirectIO:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Destructive Test For DirectIO, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 10 -p 10 -l 1638400 -d ${DEVICE} -w ${WORK_PLACE} \
-D 10 -a ${LISTENER_ADDR} -P ${LISTENER_PORT} "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 10 -p 10 -l 1638400 -d ${DEVICE} -w \
${WORK_PLACE} -D 10 -a ${LISTENER_ADDR} -P ${LISTENER_ADDR} >>${LOG_FILE} 2>&1
        RET=$?
        f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	exit ${RET}
	fi

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Basic Fucntional Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Basic Fucntional Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w ${WORK_PLACE} -f "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w \
${WORK_PLACE} -f >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Random Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Random Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w ${WORK_PLACE} -f -r "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w \
${WORK_PLACE} -r >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Mmap Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Mmap Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w ${WORK_PLACE} -m "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w \
${WORK_PLACE} -m >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Boundary Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Boundary Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -d ${DEVICE} -w ${WORK_PLACE} -b "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -d ${DEVICE} -w \
${WORK_PLACE} -b >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Concurrent Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Concurrent Refcount Test, \
CMD:${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} \
-w ${WORK_PLACE} -c 100 "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w \
${WORK_PLACE} -c 100 >>${LOG_FILE} 2>&1
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
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] O_DIRECT Refcount Test:"
        f_LogMsg ${LOG_FILE} "[${TEST_NO}] O_DIRECT Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w ${WORK_PLACE} -O "
        ${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 104857600 -d ${DEVICE} -w \
${WORK_PLACE} -O >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Verificationl CoW Test On \
Punching Holes:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Verification CoW Test On Punching \
Holes, CMD:${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 3276800 -d ${DEVICE} \
-w ${WORK_PLACE} -H "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 3276800 -d ${DEVICE} -w \
${WORK_PLACE} -H >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Verificationl CoW Test On \
Truncating:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Verification CoW Test On Truncating\
, CMD:${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 3276800 -d ${DEVICE} \
-w ${WORK_PLACE} -T "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l 3276800 -d ${DEVICE} -w \
${WORK_PLACE} -T >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Bash & Tools Utility Test:"
	f_reflink_bash_utils_test 100 104857600 >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Filling&Verify holes Refcount Test:"
	f_LogMsg ${LOG_FILE} "Fill&Verify holes on original file:"
	f_fill_and_verify_holes  10 10485760 1000 >>${LOG_FILE} 2>&1
        RET=$?
        f_exit_or_not ${RET}
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	${RM_BIN} -rf ${VERIFY_HOLES_LOG_FILE}*
	f_LogMsg ${LOG_FILE} "Fill&Verify holes on reflinks:"
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 20 -l 10485760 -d ${DEVICE} -w \
${WORK_PLACE} -h 500 -o ${VERIFY_HOLES_LOG_FILE} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}
	f_verify_reflinks_holes 20 >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	${RM_BIN} -rf ${VERIFY_HOLES_LOG_FILE}*
	f_LogMsg ${LOG_FILE} "Fill&Verify holes(mmap) on reflinks:"
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 20 -l 10485760 -d ${DEVICE} -w \
${WORK_PLACE} -h 500 -m -o ${VERIFY_HOLES_LOG_FILE} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}
	f_verify_reflinks_holes 20 >>${LOG_FILE} 2>&1
	RET=$?
	f_echo_status ${RET} | tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
	((TEST_PASS++))
	f_LogMsg ${LOG_FILE} "Cleanup working place"
	${SUDO} ${CHMOD_BIN} -R 777 ${MOUNT_POINT}  >>${LOG_FILE} 2>&1
        ${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1
	${RM_BIN} -rf ${VERIFY_HOLES_LOG_FILE}*
        RET=$?
        f_exit_or_not ${RET}

	((TEST_NO++))
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] OracleVM Data Integrity Test:"
	f_ovmtest 8048 10 >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Inline-data Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Inline-data Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 100 -l $((${BLOCKSIZE}-200)) -d ${DEVICE} -w ${WORK_PLACE} \
-I "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 100 -l $((${BLOCKSIZE}-200)) -d ${DEVICE} -w \
${WORK_PLACE} -I >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Xattr Refcount Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] Xattr Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 50 -l 10485760 -d ${DEVICE} -w ${WORK_PLACE} \
-x 5000 "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 50 -l 10485760 -d ${DEVICE} -w \
${WORK_PLACE} -x 5000 >>${LOG_FILE} 2>&1
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
        f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] Stress Refcount Test:"
        f_LogMsg ${LOG_FILE} "[${TEST_NO}] Stress Refcount Test, CMD:${SUDO} \
${REFLINK_TEST_BIN} -i 1 -n 10000 -l 2048576000 -p 20000 -d ${DEVICE} -w \
${WORK_PLACE} -s "
        ${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 10000 -l 2048576000 -p 20000 -d \
${DEVICE} -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1
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
	f_LogRunMsg ${RUN_LOG_FILE} "[${TEST_NO}] OracleVM Simulation Test:"
	f_LogMsg ${LOG_FILE} "[${TEST_NO}] OracleVM Simulation Test, CMD:\
${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 10 -l 20485760000 -p 1000 -d ${DEVICE} -w \
${WORK_PLACE} -s "
	${SUDO} ${REFLINK_TEST_BIN} -i 1 -n 10 -l 20485760000 -p 1000 -d \
${DEVICE} -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1
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
f_LogRunMsg ${RUN_LOG_FILE} "=====================Reflink tests start:  `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Reflink tests start:  `date`\
====================="

for BLOCKSIZE in 512 1024 4096;do
	for CLUSTERSIZE in 4096 32768 1048576;do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} \
bs and ${CLUSTERSIZE} cs ->\n"
		f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} bs \
and ${CLUSTERSIZE} cs ->"
		f_runtest
	done
done

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Reflink tests end: `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Reflink dirs tests end: `date`\
====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
