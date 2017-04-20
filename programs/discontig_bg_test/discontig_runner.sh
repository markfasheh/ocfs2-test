#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# discontig_runner.sh
#
# Copyright (C) 2009 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.

################################################################################
# Global Variables
################################################################################
if [ -f `dirname ${0}`/o2tf.sh ]; then
	. `dirname ${0}`/o2tf.sh
fi

DEVICE=
MOUNT_POINT=
CLUSTER_STACK=
CLUSTER_NAME=
WORK_PLACE=
WORK_PLACE_DIRENT=ocfs2-discontig-bg-test
DISCONTIG_ACTIVATE_BIN="${BINDIR}/activate_discontig_bg.sh"
GEN_EXTENTS_BIN="${BINDIR}/gen_extents"
PUNCH_HOLE_BIN="${BINDIR}/punch_hole"
SPAWN_INODES_BIN="${BINDIR}/spawn_inodes"
XATTR_TEST_BIN="${BINDIR}/xattr-test"
MULTI_XATTR_TEST_BIN="${BINDIR}/xattr-multi-test"
DXDIR_TEST_BIN="${BINDIR}/index_dir"
REFCOUNT_TEST_BIN="`which sudo` -u root ${BINDIR}/reflink_test"
MULTI_REFCOUNT_TEST_BIN="${BINDIR}/multi_reflink_test"
INLINE_DATA_TEST_BIN="`which sudo` -u root ${BINDIR}/inline-data"
INLINE_DIRS_TEST_BIN="`which sudo` -u root ${BINDIR}/inline-dirs"
REFLINK_BIN="`which reflink`"
SETXATTR="`which sudo` -u root `which setfattr`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
PUNCH_LOG_FILE=

BLOCKSIZE=
CLUSTERSIZE=
JOURNALSIZE=0
BLOCKS=0
LABELNAME="ocfs2-discontig-bg-tests"
MOUNT_OPTS="localalloc=0"
OLD_MOUNT_OPTS=
DEVICE=
WORK_PLACE=

OCFS2_LINK_MAX=65000

MULTI_TEST=
MPI_HOSTS=
MPI_RANKS=
MPI_ACCESS_METHOD="rsh"
MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
MPI_BTL_ARG="-mca btl tcp,self"
MPI_BTL_IF_ARG=
################################################################################
# Utility Functions
################################################################################
function f_usage()
{
    echo "usage: `basename ${0}` <-d device> [-o logdir] [-m multi_hosts] [-a access_method] \
[-b block_size] [-c cluster_size] <-s cluster stack> <-n cluster name> <mount point>"
    exit 1;

}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi
	
	while getopts "hd:o:m:a:b:c:s:n:" options; do
		case $options in
		d ) DEVICE="$OPTARG";;
		o ) LOG_DIR="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		m ) MULTI_TEST=1
		    MPI_HOSTS="$OPTARG";;
                b ) BLOCKSIZE="$OPTARG";;
                c ) CLUSTERSIZE="$OPTARG";;
                s ) CLUSTER_STACK="$OPTARG";;
                n ) CLUSTER_NAME="$OPTARG";;
		h ) f_usage
			exit 1;;
		* ) f_usage
			exit 1;;
		esac
	done

	shift $(($OPTIND -1))
	MOUNT_POINT=${1}
}

function f_verify_hosts()
{
	local -a hosts=${1}
	local host=

	hosts=`echo ${hosts}|tr "[,]" "[ ]"`

	for host in `echo $hosts`;do
		ping -q -w 2 $host >/dev/null 2>&1 || {
			echo "$host is unreachable."
			return 1
		}
	done

	return 0
}

function f_setup()
{
	f_getoptions $*
	
	if [ -z "${DEVICE}" ];then
		f_usage
	fi	
	
	if [ -z "${MOUNT_POINT}" ];then
		f_usage
	fi
	
	if [ ! -d "${MOUNT_POINT}" ];then
		echo "${MOUNT_POINT} you specified was not a dir."
		f_usage
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
				MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
			else
				MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
			fi
		fi

		WORK_PLACE_DIRENT=${WORK_PLACE_DIRENT}-multi-nodes
		LABELNAME=${LABELNAME}-multi-nodes
	fi
	
	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1
	
	if [ -n "${MULTI_TEST}" ];then
		RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-discontig-bg-multiple-run.log"
	else
		RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-discontig-bg-single-run.log"
	fi
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-discontig-bg.log"
        PUNCH_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-punch-hole.log"

}

function f_get_disk_usage()
{
	local DISK_FREE=
	local DISK_FREE_M=
	local DISK_NAME=

	# if a symbollink is given, work out the typical device name, like /dev/sda
	if [ -L ${DEVICE} ];then
		DISK_NAME=`readlink -f ${DEVICE}`
	fi

        f_LogMsg ${LOG_FILE} "Calculate the disk total and free size"

        DISK_FREE=`df |grep ${MOUNT_POINT}|awk '{print $4}'`

        if [ -z "${DISK_FREE}" ]; then
                DISK_FREE=`df |grep ${DISK_NAME}|awk '{print $4}'`
        fi

        DISK_FREE_M=`echo ${DISK_FREE}/1024|bc`

	echo "$DISK_FREE_M"
}

function f_get_extents_num_of_contig_bg()
{
	local -i num_extents

	num_extents=$(($((4*1024*1024))/${BLOCKSIZE}))

	echo $num_extents
}

function f_get_inodes_num_of_contig_bg()
{
	local -i num_inodes

	if [ "${BLOCKSIZE}" == "512" ]; then
		num_inodes=$(($((1024*1024))/${BLOCKSIZE}))
	elif [ "${BLOCKSIZE}" == "1024" ];then
		num_inodes=$(($((2*1024*1024))/${BLOCKSIZE}))
	elif [ "${BLOCKSIZE}" == "2048" -o "${BLOCKSIZE}" == "4096" ];then
		num_inodes=$(($((4*1024*1024))/${BLOCKSIZE}))
	fi

	echo $num_inodes
}

function f_inodes_test()
{
	local  i=1
	local  j=1
	local  k=
	local filename_prefix=
	local filename=

	f_LogMsg ${LOG_FILE} "Activate inode discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t inode -r 200 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}
	
	f_LogMsg ${LOG_FILE} "Fill up volumes by spreading inodes from discontig-bg."

	filename_prefix=testfile-zero
	while :;do
		filename=${filename_prefix}${i}
		if [ "${i}" -gt "$OCFS2_LINK_MAX" ];then
			${MKDIR_BIN} -p ${WORK_PLACE}/${filename}
			sync
			i=1
			filename_prefix=${filename}/
			continue
		else
			${TOUCH_BIN} ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Volume gets full being filled with zero files"
				break
			}
		fi

		((i++))
		((j++))
	done

	((j--))

	f_LogMsg ${LOG_FILE} "Traverse and updates inode blocks in discontig-bg"
	filename_prefix=testfile-zero
	i=1
	for k in `seq ${j}`;do
		filename=${filename_prefix}${i}
		if [ "${i}" -gt "$OCFS2_LINK_MAX" ];then
			i=1
			filename_prefix=${filename}/
			continue
		else
			${TOUCH_BIN} ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Unable to touch ${WORK_PLACE}/${filename}"
				return 1
			}
		fi
		((i++))
	done

	f_LogMsg ${LOG_FILE} "Randomly remove inodes in discontig-bg"
	filename_prefix=testfile-zero
	i=1
	for k in `seq ${j}`;do
		filename=${filename_prefix}${i}
		if [ "${i}" -gt "$OCFS2_LINK_MAX" ];then
			i=1
			filename_prefix=${filename}/
			continue
		else
			if [ "$(($RANDOM%2))" -eq "0" ];then
				${RM_BIN} -rf ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
					f_LogMsg ${LOG_FILE} "Unable to remove ${WORK_PLACE}/${filename}"
					return 1
				}
			fi
		fi
		((i++))
	done

	f_LogMsg ${LOG_FILE} "Remove all inodes in discontig-bg"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Activate inode discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t inode -r 4096 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Stress spawn-inodes test with multiple processes"
	${SPAWN_INODES_BIN} -n 1000 -m 1000 -w ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "stress test fill up the volume."
	}

	f_LogMsg ${LOG_FILE} "Remove all inodes in discontig-bg"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1

	f_LogMsg ${LOG_FILE} "Stress random spawn-inodes test with multiple processes"
	${SPAWN_INODES_BIN} -n 1000 -m 1000 -w ${WORK_PLACE} -r >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "stress test fill up the volume."
	}

	f_LogMsg ${LOG_FILE} "Remove all inodes in discontig-bg"
	${RM_BIN} -rf ${WORK_PLACE}/* >>${LOG_FILE} 2>&1

	f_LogMsg ${LOG_FILE} "Remove all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_is_xattr_in_block()
{
	#${1} is test file
	#${2} is target volume

	${DEBUGFS_BIN} -R "xattr ${1}" ${2}|grep -qi "block" && {
		return 0
	}

	return 1
}

function f_propagate_xattr_blocks()
{

	local filename_prefix=${1}
	local filename=
	local -i i=1
	local RET=0

	while :;do
		filename=${filename_prefix}-${i}
		${TOUCH_BIN} ${WORK_PLACE}/$filename >/dev/null 2>&1|| {
			f_LogMsg ${LOG_FILE} "touch file failed when propagating xattr blocks."
			RET=1
			break
		}

		sync

		for j in $(seq 100);do
			${SETXATTR} -n user.name${j} -v value${j} ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "setxattr failed."
				RET=1
				break
			}

			sync

			f_is_xattr_in_block ${WORK_PLACE_DIRENT}/${filename} ${DEVICE} && {
				break
			}
		done

		if [ "${RET}" -ne "0" ];then
			break
		fi

		((i++))
	done

	return $RET
}

function f_update_xattr_blocks()
{

	local filename_prefix=${1}
	local filename=
	local -i i=1
	local RET=0

	while :;do
		filename=${filename_prefix}-${i}

		${SETXATTR} -n user.name1 -v updt1 ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
			f_LogMsg ${LOG_FILE} "update failed."
			RET=1
			break
		}

		((i++))
	done

	return $RET
}

function f_extents_test()
{
	local disk_free_m=
	local filesize=
	local filename=

	local offset=
	local num=
	local recs_in_blk=
	local iter=1000
	local count=
	local inc=

	f_LogMsg ${LOG_FILE} "[*] Activate extent discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r 2048 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

        f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
        f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
        RET=$?
        f_exit_or_not ${RET}

	${RM_BIN} -rf ${MOUNT_POINT}/ocfs2-fillup-contig-bg-dir-*
	sync

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	filename=${WORK_PLACE}/extents_testfile

	f_LogMsg ${LOG_FILE} "Fill up volumes by spreading extents from discontig-bg."
	disk_free_m=`f_get_disk_usage`

	filesize=$(((${disk_free_m}-40)*1024*1024/2))

	f_LogMsg ${LOG_FILE} "Fill up volumes by extents(filesize = $filesize), free space is ${disk_free_m}."
	f_LogMsg ${LOG_FILE} "CMD: ${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1"
	${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1 >>${LOG_FILE} 2>&1 || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Propagate xattr blocks to fill up extent block group."
	f_propagate_xattr_blocks "extents_testfile_xattr_addup" || {
		f_LogMsg ${LOG_FILE} "Volume gets full by adding additional xattr blocks."
	}

	f_update_xattr_blocks "extents_testfile_xattr_addup" || {
		f_LogMsg ${LOG_FILE} "finish updating xattr."
	}

	sync
	
	f_LogMsg ${LOG_FILE} "Traverse extent blocks"
	offset=0
	num=0
	count=$((${filesize}/${CLUSTERSIZE}))
	if [ "${count}" -gt "${iter}" ];then
		inc=$((${count}/${iter}))
	else
		inc=1
	fi
	while :;do
		dd if=${filename} of=/dev/null bs=${CLUSTERSIZE} count=1 skip=${num} >/dev/null 2>&1 ||{
			echo "dd if=${filename} of=/dev/null bs=${CLUSTERSIZE} count=1 skip=${num}"
			f_LogMsg ${LOG_FILE} "Traverse failed at #${num} cluster."
			return 1
		}
		num=$((${num}+${inc}))
		offset=$((${num}*${CLUSTERSIZE}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "Traverse completed."
			break
		fi
	done

	#use punch_hole to change extent_list then to update
	f_LogMsg ${LOG_FILE} "Update extent blocks by punching holes"
	rm_start=1
	offset=0
	num=0
	count=$((${filesize}/${CLUSTERSIZE}))
	if [ "${count}" -gt "${iter}" ];then
		inc=$((${count}/${iter}))
	else
		inc=1
	fi
	while :;do
		if [ "$((${RANDOM}%2))" -eq "0" ];then
			${PUNCH_HOLE_BIN} -f ${filename} -s ${offset} -l ${CLUSTERSIZE} >>/dev/null 2>&1 || {
				if [ $rm_start -ne 10 ]; then
					f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed, rm addup-${rm_start} and try again."
					rm -rf ${WORK_PLACE}/extents_testfile_xattr_addup-${rm_start}*
					sync
					rm_start=$(($rm_start+1))
					continue
				else
					f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed."
					return 1
				fi
			}
		fi
		num=$((${num}+${inc}))
		offset=$((${num}*${CLUSTERSIZE}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "extents updating completed."
			break
		fi
	done

	f_LogMsg ${LOG_FILE} "Randomly remove extent blocks"
	recs_in_blk=$(((${BLOCKSIZE}-64)/16))
	while :;do
		if [ "$((${RANDOM}%2))" -eq "0" ];then
			${PUNCH_HOLE_BIN} -f ${filename} -s ${offset} -l $((${CLUSTERSIZE}*${recs_in_blk})) >>${PUNCH_LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed."
				return 1
			}
		fi

		offset=$((${offset}+${CLUSTERSIZE}*${recs_in_blk}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "extents updating completed."
			break
		fi
	done

	f_LogMsg ${LOG_FILE} "Remove all extent blocks and xattr blocks"
	${RM_BIN} -rf ${filename}
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Remove all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_inline_test()
{
	f_LogMsg ${LOG_FILE} "[*] Activate inode discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t inode -r 1024 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	${RM_BIN} -rf ${MOUNT_POINT}/ocfs2-fillup-contig-bg-dir-*
	sync

	f_LogMsg ${LOG_FILE} "Regular inline-file test."
	${INLINE_DATA_TEST_BIN} -i 1 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Multiple inline-file test."
	${INLINE_DATA_TEST_BIN} -i 1 -m 100 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Concurrent inline-file test."
	${INLINE_DATA_TEST_BIN} -i 1 -c 100 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Stress inline-file test."
	${INLINE_DATA_TEST_BIN} -i 10 -c 50 -m 100 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}
	
	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Regular inline-dir test."
	${INLINE_DIRS_TEST_BIN} -i 1 -s 20 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Multiple inline-dir test."
	${INLINE_DIRS_TEST_BIN} -i 1 -s 5 -m 100 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "Concurrent inline-dir test."
	${INLINE_DIRS_TEST_BIN} -i 1 -s 5 -c 100 -d ${DEVICE} ${MOUNT_POINT}>>${LOG_FILE} 2>&1 || {
		return 1
	}

	${SUDO} ${RM_BIN} -rf ${MOUNT_POINT}/inline-data-test

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_xattr_test()
{
	f_LogMsg ${LOG_FILE} "[*] Activate extent discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r 10240 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

        f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
        f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
        RET=$?
        f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Stress xattr filling-up test with multiple processes."
	f_LogMsg ${LOG_FILE} "CMD: ${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 150 -f 500 -r -k ${WORK_PLACE}"
	${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 150 -f 500 -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Volume get filled up with xattr blocks."
	}
	
	f_LogMsg ${LOG_FILE} "Remove all xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "CMD: ${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 30 -s 80 -f 800 -r -k ${WORK_PLACE}"
	${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 30 -s 80 -f 800 -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Volume get filled up with xattr blocks."
	}
	
	f_LogMsg ${LOG_FILE} "Remove all xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Stress&Random xattr filling-up test with multiple processes."
	f_LogMsg ${LOG_FILE} "CMD: ${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 50 -s 150 -f 500 -r -k ${WORK_PLACE}"
	${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 50 -s 150 -f 500 -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Volume get filled up with xattr blocks."
	}

	f_LogMsg ${LOG_FILE} "Remove all xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Stress&Random xattr filling-up test with multiple processes in bucket."
	f_LogMsg ${LOG_FILE} "CMD: ${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 1000 -f 1000 -r -k ${WORK_PLACE}"
	${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 1000 -f 1000 -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Volume get filled up with xattr blocks."
	}

	f_LogMsg ${LOG_FILE} "Remove all xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Concurrent xattr block group test"
	f_LogMsg ${LOG_FILE} "CMD: ${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 50 -s 150 -m 500 -r -k ${WORK_PLACE}"
	${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 50 -s 150 -m 500 -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg "Volume get filled up with xattr blocks."
	}

	f_LogMsg ${LOG_FILE} "Remove all xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Remove all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}

}

function f_refcount_test()
{
	local oirg_filename=
	local ref_filename=
	local filesize=
	local -i remain_space=2048

	local num=
	local offset=
	local recs_in_blk=
	local disk_free_m=
	local pattern=
	local iter=1000
	local count=
	local inc=

	f_LogMsg ${LOG_FILE} "[*] Activate extent discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

        f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
        f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
        RET=$?
        f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}
	
	f_LogMsg ${LOG_FILE} "Tremendous refcount block testing."
	f_LogMsg ${LOG_FILE} "CMD: ${REFCOUNT_TEST_BIN} -i 1 -n 10 -p 2000 -l 1048576 -d ${DEVICE} -w ${WORK_PLACE} -s"
	${REFCOUNT_TEST_BIN} -i 1 -n 10 -p 2000 -l 1048576 -d ${DEVICE} -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Tremendous refcount block testing failed."
		return 1
	}

	f_LogMsg ${LOG_FILE} "Remove all refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	sync
	disk_free_m=`f_get_disk_usage`

	f_LogMsg ${LOG_FILE} "Prepare original file with extents for reflink"
	filesize=$(((${disk_free_m}-200)*1024*1024/2))
	orig_filename=${WORK_PLACE}/extents_for_reflink
	ref_filename=${WORK_PLACE}/extents_reflink
	f_LogMsg ${LOG_FILE} "CMD: ${GEN_EXTENTS_BIN} -f ${orig_filename} -l ${filesize} -c ${CLUSTERSIZE} -k 0"
	${GEN_EXTENTS_BIN} -f ${orig_filename} -l ${filesize} -c ${CLUSTERSIZE} -k 0 >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Failed to generate file $orig_filename with extents"
                return 1
        }
	
	f_LogMsg ${LOG_FILE} "Reflink original file to target."
	${REFLINK_BIN} ${orig_filename} ${ref_filename} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Failed to reflink original file ${orig_filename}."
		return 1
	}

	f_LogMsg ${LOG_FILE} "Update original and reflinks randomly"
	offset=0
	num=0
	pattern=/tmp/pattern-$$
	dd if=/dev/random of=${pattern} bs=${CLUSTERSIZE} count=1 >>/dev/null 2>&1
	count=$((${filesize}/${CLUSTERSIZE}))
	if [ "${count}" -gt "${iter}" ];then
		inc=$((${count}/${iter}))
	else
		inc=1
	fi
	while :;do
		if [ "$((${RANDOM}%2))" -eq "0" ];then
			dd if=${pattern} of=${orig_filename} bs=${CLUSTERSIZE} count=1 seek=${num} >>/dev/null 2>&1 || {
				f_LogMsg ${LOG_FILE} "Update at offset:${offset} failed on ${orig_filename}."
				return 1
			}
		fi
		if [ "$((${RANDOM}%2))" -eq "1" ];then
			dd if=${pattern} of=${ref_filename} bs=${CLUSTERSIZE} count=1 seek=${num} >>/dev/null 2>&1 || {
				f_LogMsg ${LOG_FILE} "Update at offset:${offset} failed on ${ref_filename}."
				return 1
			}
		fi
		num=$((${num}+${inc}))
		offset=$((${num}*${CLUSTERSIZE}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "data updating completed."
			break
		fi
	done
	rm -rf ${pattern}

	f_LogMsg ${LOG_FILE} "Update reflink extent blocks by punching holes"
	offset=0
	num=0
	count=$((${filesize}/${CLUSTERSIZE}))
	if [ "${count}" -gt "${iter}" ];then
		inc=$((${count}/${iter}))
	else
		inc=1
	fi
	while :;do
		if [ "$((${RANDOM}%2))" -eq "0" ];then
			${PUNCH_HOLE_BIN} -f ${orig_filename} -s ${offset} -l ${CLUSTERSIZE} >>${PUNCH_LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed on ${orig_filename}."
				return 1
			}
		fi
		if [ "$((${RANDOM}%2))" -eq "1" ];then
			${PUNCH_HOLE_BIN} -f ${ref_filename} -s ${offset} -l ${CLUSTERSIZE} >>${PUNCH_LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed on ${ref_filename}."
				return 1
			}
		fi
		num=$((${num}+${inc}))
		offset=$((${num}*${CLUSTERSIZE}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "extents updating completed."
			break
		fi
	done

	f_LogMsg ${LOG_FILE} "Randomly remove extent blocks for reflink"
	recs_in_blk=$(((${BLOCKSIZE}-64)/16))
	while :;do
		if [ "$((${RANDOM}%2))" -eq "0" ];then
			${PUNCH_HOLE_BIN} -f ${orig_filename} -s ${offset} -l $((${CLUSTERSIZE}*${recs_in_blk})) >>${PUNCH_LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed on ${orig_filename}."
				return 1
			}
		fi

		if [ "$((${RANDOM}%2))" -eq "1" ];then
			${PUNCH_HOLE_BIN} -f ${ref_filename} -s ${offset} -l $((${CLUSTERSIZE}*${recs_in_blk})) >>${PUNCH_LOG_FILE} 2>&1 || {
				f_LogMsg ${LOG_FILE} "Punch hole at offset:${offset} failed on ${ref_filename}."
				return 1
			}
		fi

		offset=$((${offset}+${CLUSTERSIZE}*${recs_in_blk}))

		if [ "${offset}" -gt "${filesize}" ];then
			f_LogMsg ${LOG_FILE} "extents updating completed."
			break
		fi
	done

	f_LogMsg ${LOG_FILE} "Remove all refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	sync
#	disk_free_m=`f_get_disk_usage`
#	f_LogMsg ${LOG_FILE} "Refcount Fill-up Testing."
#	f_LogMsg ${LOG_FILE} "CMD: ${REFCOUNT_TEST_BIN} -i 1 -n 2 -l $((${disk_free_m}*1024*1024))  -d ${DEVICE} -w ${WORK_PLACE} -s"
#	${REFCOUNT_TEST_BIN} -i 1 -n 2 -l $((${disk_free_m}*1024*1024))  -d ${DEVICE} -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1 || {
#		f_LogMsg ${LOG_FILE} "Refcount fill-up testing failed."
#		return 1
#	}
#
#	f_LogMsg ${LOG_FILE} "Remove all refcount blocks"
#	${RM_BIN} -rf ${WORK_PLACE}/*
#
#	f_LogMsg ${LOG_FILE} "Reflink stress test."
#	f_LogMsg ${LOG_FILE} "CMD: ${REFCOUNT_TEST_BIN} -i 1 -n 50000 -l 1048576  -d ${DEVICE} -w ${WORK_PLACE} -s"
#	${REFCOUNT_TEST_BIN} -i 1 -n 50000 -l 1048576  -d ${DEVICE} -w ${WORK_PLACE} -s >>${LOG_FILE} 2>&1 || {
#		f_LogMsg ${LOG_FILE} "Reflink stress test failed."
#		return 1
#	}
#
#	f_LogMsg ${LOG_FILE} "Remove all reflink files."
#	${RM_BIN} -rf ${WORK_PLACE}/*
#
#	f_LogMsg ${LOG_FILE} "Reflink & Xattr combination test"
#	f_LogMsg ${LOG_FILE} "CMD: ${REFCOUNT_TEST_BIN} -i 1 -n 10000 -l 1048576  -d ${DEVICE} -w ${WORK_PLACE} -x 1000"
#	${REFCOUNT_TEST_BIN} -i 1 -n 10000 -l 1048576  -d ${DEVICE} -w ${WORK_PLACE} -x 1000 >>${LOG_FILE} 2>&1 || {
#		f_LogMsg ${LOG_FILE} "Reflink & Xattr combination test failed."
#		return 1
#	}
#
#	f_LogMsg ${LOG_FILE} "Remove all reflink files."
#	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Remove all stuffs."
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_dxdir_test()
{
	f_LogMsg ${LOG_FILE} "[*] Activate inode discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t extents -r 2048 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Regular dxdir test."
	${SUDO} ${DXDIR_TEST_BIN} -i 10 -n 20000 -v ${DEVICE} -d 2 -w ${WORK_PLACE} -f >>${LOG_FILE} 2>&1 || {
		return 1
	}

	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Concurrent dxdir test."
	${SUDO} ${DXDIR_TEST_BIN} -i 1 -n 4000 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -c 200 >>${LOG_FILE} 2>&1 || {
		return 1
	}
	${RM_BIN} -rf ${WORK_PLACE}/*


	f_LogMsg ${LOG_FILE} "Multi-processes dxdir test."
	${SUDO} ${DXDIR_TEST_BIN} -i 1 -n 300 -v ${DEVICE} -d 1 -w ${WORK_PLACE} -m 10 >>${LOG_FILE} 2>&1 || {
		return 1
	}
	${RM_BIN} -rf ${WORK_PLACE}/*


	f_LogMsg ${LOG_FILE} "Stress dxdir test."
	${SUDO} ${DXDIR_TEST_BIN} -i 10 -n 500000  -v ${DEVICE} -w ${WORK_PLACE} -s>>${LOG_FILE} 2>&1 || {
		return 1
	}
	${RM_BIN} -rf ${WORK_PLACE}/*
}

function f_single_runner()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Inodes Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Inodes Block Group Test:"
	f_inodes_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Extent Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Extent Block Group Test:"
	f_extents_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Inline File Test:"
	f_LogMsg ${LOG_FILE} "[*] Inline File Test:"
	f_inline_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Xattr Block Test:"
	f_LogMsg ${LOG_FILE} "[*] Xattr Block Group Test:"
	f_xattr_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Refcount Block Test:"
	f_LogMsg ${LOG_FILE} "[*] Refcount Block Group Test:"
	f_refcount_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}
}

function f_multi_inodes_test()
{

	f_LogMsg ${LOG_FILE} "Activate inode discontig-bg on ${DEVICE}"
	${DISCONTIG_ACTIVATE_BIN} -t inode -r 800 -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Spawning inodes from multi-nodes"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${SPAWN_INODES_BIN} -n 20000 -m 100 -w ${WORK_PLACE}"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${SPAWN_INODES_BIN} -n 100 -m 10 -w ${WORK_PLACE} >>${LOG_FILE} 2>&1

	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Do fsck.ocfs2"
	${FSCK_BIN} -fy ${DEVICE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "fsck failed"
	}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Cleanup all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "Umount volume"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_multi_extents_test()
{
	local remain_space=512
	local filesize=
	local -a hosts_array="(`echo ${MPI_HOSTS}|tr "[,]" "[ ]"`)"
	local num_hosts=${#hosts_array[@]}
	local filename=

	f_LogMsg ${LOG_FILE} "Activate extents discontig-bg on ${DEVICE}"
	f_LogMsg ${LOG_FILE} "CMD: ${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	${RM_BIN} -rf ${MOUNT_POINT}/ocfs2-fillup-contig-bg-dir-*
	sync

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	filename=${WORK_PLACE}/multi_nodes_extents_testfile
	filesize=$((${remain_space}/${num_hosts}/2*1024*1024))
	f_LogMsg ${LOG_FILE} "Generate extents from multi-nodes"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1 -m"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1 -m>>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Do fsck.ocfs2"
	${FSCK_BIN} -fy ${DEVICE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "fsck failed"
	}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Cleanup allocated extents"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Cleanup all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "Umount volume"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_multi_xattr_test()
{
	local remain_space=1024

	f_LogMsg ${LOG_FILE} "Activate extents discontig-bg on ${DEVICE}"
	f_LogMsg ${LOG_FILE} "CMD: ${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Stress xattr filling-up test with multiple processes."
        f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 150 -f 500 -r -k ${WORK_PLACE}"
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 2 -n user -t normal -l 50 -s 150 -f 500 -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
                f_LogMsg ${LOG_FILE} "Volume get filled up with xattr blocks."
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Concurrent adding test."
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 200 -s 1000 -o -r -k ${WORK_PLACE}"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 200 -s 1000 -o -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "concurrent xattr adding test failed"
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated xattr blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Stress multi-nodes xattr test."
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORK_PLACE}"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORK_PLACE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "stress multi-nodes xattr test failed."
	}

	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Do fsck.ocfs2"
	${FSCK_BIN} -fy ${DEVICE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "fsck failed"
	}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Cleanup all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "Umount volume"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_multi_refcount_test()
{
	local remain_space=1024

	f_LogMsg ${LOG_FILE} "Activate extents discontig-bg on ${DEVICE}"
	f_LogMsg ${LOG_FILE} "CMD: ${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT}"
	${DISCONTIG_ACTIVATE_BIN} -t extent -r ${remain_space} -b ${BLOCKSIZE} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} -l ${LABELNAME} -m ${MPI_HOSTS} -a ${MPI_ACCESS_METHOD} -s ${CLUSTER_STACK} -n ${CLUSTER_NAME} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "[*] Remote Mount among nodes ${MPI_HOSTS}:"
	f_remote_mount ${LOG_FILE} ${LABELNAME} ${MOUNT_POINT} ${MPI_HOSTS} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogMsg ${LOG_FILE} "Basic multi-refcount test"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 100 -w ${WORK_PLACE} -f "
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 100 -w ${WORK_PLACE} -f >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Basic multi-refcount test failed."
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Concurrent multi-refcount test"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 100 -w ${WORK_PLACE} -c"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 100 -w ${WORK_PLACE} -c>>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Concurrent multi-refcount test failed."
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Combined multi-refcount&xattr test"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 10 -w ${WORK_PLACE} -x 1000"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -l 10485760 -n 10 -w ${WORK_PLACE} -x 1000 >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Combined multi-refcount&xattr test failed."
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*

	f_LogMsg ${LOG_FILE} "Stress multi-refcount test"
	f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -p 1000 -l 10485760 -n 100 -w ${WORK_PLACE} -s"
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${MULTI_REFCOUNT_TEST_BIN} -i 1 -p 1000 -l 10485760 -n 100 -w ${WORK_PLACE} -s>>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "Stress multi-refcount test failed."
	}

	f_LogMsg ${LOG_FILE} "Cleanup allocated refcount blocks"
	${RM_BIN} -rf ${WORK_PLACE}/*
	
	f_LogMsg ${LOG_FILE} "[*] Umount volume from nodes ${MPI_HOSTS}:"
	f_remote_umount ${LOG_FILE} ${MOUNT_POINT} ${MPI_HOSTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Do fsck.ocfs2"
	${FSCK_BIN} -fy ${DEVICE} >>${LOG_FILE} 2>&1 || {
		f_LogMsg ${LOG_FILE} "fsck failed"
	}

	f_LogMsg ${LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	f_LogMsg ${LOG_FILE} "Cleanup all stuffs"
	${RM_BIN} -rf ${MOUNT_POINT}/*

	f_LogMsg ${LOG_FILE} "Umount volume"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_exit_or_not ${RET}
}

function f_multi_runner()
{
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Multi-nodes Inodes Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Multi-nodes Inodes Block Group Test:"
	f_multi_inodes_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Multi-nodes Extents Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Multi-nodes Extents Block Group Test:"
	f_multi_extents_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Multi-nodes Xattr Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Multi-nodes Xattr Block Group Test:"
	f_multi_xattr_test
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Multi-nodes Refcount Block Group Test:"
	f_LogMsg ${LOG_FILE} "[*] Multi-nodes Refcount Block Group Test:"
	f_multi_refcount_test
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

f_setup $*

if [ -z ${BLOCKSIZE} ];then
       bslist="512 4096"
else
       bslist=${BLOCKSIZE}
fi

if [ -z ${CLUSTERSIZE} ];then
       cslist="4096 8192"
else
       cslist=${CLUSTERSIZE}
fi

f_LogRunMsg ${RUN_LOG_FILE} "=====================Discontiguous block group test starts:  `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Discontiguous block groups tests start:  `date`\
====================="

for BLOCKSIZE in $(echo "$bslist");do
	for CLUSTERSIZE in $(echo "$cslist");do
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

f_LogRunMsg ${RUN_LOG_FILE} "=====================Discontiguous block group test ends: `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Discontiguous block groups test ends: `date`\
====================="
