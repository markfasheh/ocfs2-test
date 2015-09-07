#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# activate_discontig_bg.sh
#
# Description:  It's a simple script to activate first discontiguous block group.
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
WORK_PLACE=
WORK_PLACE_DIRENT=ocfs2-activate-discontig-bg-dir
TUNEFS_BIN="`which sudo` -u root `which tunefs.ocfs2`"
RESV_UNWRITTEN_BIN="${BINDIR}/resv_unwritten"
GEN_EXTENTS_BIN="${BINDIR}/gen_extents"
CONTIG_BG_FILLER="${BINDIR}/fillup_contig_bg.sh"
SETXATTR="`which sudo` -u root `which setfattr`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=

BLOCKSIZE=4096
CLUSTERSIZE=4096
TYPE=inode
SLOTS=0
JOURNALSIZE=0
LABELNAME="ocfs2-discontig-bg-tests"
MOUNT_OPTS="localalloc=0"
DEVICE=
WORK_PLACE=

ORIG_VOLUME_SIZE=$((3*1024*1024*1024))
ORIG_VOLUME_SIZE_BK=
DISK_FREE_M=
FILE_MAJOR_SIZE_M=
RESV_SIZE_M=500
FILL_CONTIG_EBG_M=
FILL_CONTIG_IBG_NUM=
CONTIG_REG_M=

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
    echo "usage: `basename ${0}` [-t type] [-r resv_size] [-b blocksize] \
[-c clustersize] [-l label] [-m mpi_hosts] [-a access_method] <-d device> \
[-o logdir] <mount point> "
    exit 1;

}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi
	
	while getopts "hd:o:b:c:t:r:l:m:a:" options; do
		case $options in
		d ) DEVICE="$OPTARG";;
		o ) LOG_DIR="$OPTARG";;
		b ) BLOCKSIZE="$OPTARG";;
		c ) CLUSTERSIZE="$OPTARG";;
		r ) RESV_SIZE_M="$OPTARG";;
		t ) TYPE="$OPTARG";;
		l ) LABELNAME="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		m ) MULTI_TEST=1
		    MPI_HOSTS="$OPTARG";;
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
	local -i slots=0

	hosts=`echo ${hosts}|tr "[,]" "[ ]"`

	for host in `echo $hosts`;do
		ping -q -w 2 $host >/dev/null 2>&1 || {
			echo "$host is unreachable."
			return 1
		}
		((slots++))
	done

	if [ "$slots" -eq "0" ];then
		slots=1
	fi

	SLOTS=${slots}

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

	if [ -n "${TYPE}" ];then
		if [ "${TYPE}" != "inode" ] && [ "${TYPE}" != "extent" ];then
			echo "type should be 'inode' or 'extent'"
			f_usage
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
				MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
			else
				MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
				
			fi
		fi
	fi
	
	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1
	
	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-activate-discontig-bg-run.log"
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-activate-discontig-bg.log"

}

function f_get_contig_region()
{
	case $BLOCKSIZE in
		512 )  CONTIG_REG_M=1;;
		1024 ) CONTIG_REG_M=2;;
		2048 ) CONTIG_REG_M=4;;
		4096 ) CONTIG_REG_M=4;;
	esac
}

function f_get_disk_usage()
{
	f_LogMsg ${LOG_FILE} "Calculate the disk total and free size"

	DISK_FREE=`df |grep ${MOUNT_POINT}|awk '{print $4}'`

	if [ -z "${DISK_FREE}" ]; then
		DISK_FREE=`df |grep ${DEVICE}|awk '{print $4}'`
	fi

	DISK_FREE_M=`echo ${DISK_FREE}/1024|bc`
}

function f_get_eb_num_of_contig_bg()
{
	# Note that this func must be called right after a fresh mkfs
	# cause we're using a growing strategy to decide the size of
	# extent_alloc according to volume size.
	local num_eb=

	num_eb=`${DEBUGFS_BIN} -R "stat //extent_alloc:0000" ${DEVICE}|grep "Bitmap Total"|cut -d':' -f2|cut -d' ' -f2`

	echo $num_eb
}

function f_get_inodes_num_of_contig_bg()
{
	local num_inodes=

	#if [ "${BLOCKSIZE}" == "512" ]; then
	#	num_inodes=$(($((1024*1024))/${BLOCKSIZE}))
	#elif [ "${BLOCKSIZE}" == "1024" ];then
	#	num_inodes=$(($((2*1024*1024))/${BLOCKSIZE}))
	#elif [ "${BLOCKSIZE}" == "2048" -o "${BLOCKSIZE}" == "4096" ];then
	#	num_inodes=$(($((4*1024*1024))/${BLOCKSIZE}))
	#fi

	num_inodes=`${DEBUGFS_BIN} -R "stat //inode_alloc:0000" ${DEVICE}|grep "Bitmap Total"|cut -d':' -f2|cut -d' ' -f2`

	echo $num_inodes
}

function f_get_recs_in_eb()
{
	local num_recs

	num_recs=$(($((${BLOCKSIZE}-48-16))/16))

	echo $num_recs
}

# fill up volume with a huge reserved unwritten region
# and fragmented small files.
function f_fillup_volume_almost_full()
{
	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_get_disk_usage

	# RESV_SIZE_M is reserved for further testing operations.
	if [ "${DISK_FREE_M}" -le "${RESV_SIZE_M}" ];then
		RESV_SIZE_M=$((${DISK_FREE_M}/4))
	fi

	local -i extents=`f_get_eb_num_of_contig_bg`
	local -i recs=`f_get_recs_in_eb`

	FILL_CONTIG_IBG_NUM=`f_get_inodes_num_of_contig_bg`
	FILL_CONTIG_EBG_M=$((${extents}*${recs}*${BLOCKSIZE}/1024/1024))


	if [ "${TYPE}" == "extent" ];then
		if [ "${DISK_FREE_M}" -le "$((2*${RESV_SIZE_M}+2*${FILL_CONTIG_EBG_M}*${SLOTS}))" ]; then
			RESV_SIZE_M=$((${DISK_FREE_M}/2-${FILL_CONTIG_EBG_M}*${SLOTS}))
		fi
		FILE_MAJOR_SIZE_M=$((${DISK_FREE_M}-2*${RESV_SIZE_M}-2*${FILL_CONTIG_EBG_M}*${SLOTS}))
	else
		if [ "${DISK_FREE_M}" -le "$((2*${RESV_SIZE_M}))" ]; then
			RESV_SIZE_M=$((${DISK_FREE_M}/4))
		fi
		FILE_MAJOR_SIZE_M=$((${DISK_FREE_M}-2*${RESV_SIZE_M}))
	fi

	f_LogMsg ${LOG_FILE} "[*] Reserve ${FILE_MAJOR_SIZE_M}M space for a LARGE file"
	${RESV_UNWRITTEN_BIN} -f ${WORK_PLACE}/large_testfile -s 0 -l $((${FILE_MAJOR_SIZE_M}*1024*1024)) >>${LOG_FILE} 2>&1
	RET=$?
	f_exit_or_not ${RET}

	f_get_contig_region

	local blocksz=
	if [ ${CONTIG_REG_M} -eq "1" ];then
		blocksz=512K
	else
		blocksz=$((${CONTIG_REG_M}/2))M
	fi

	f_LogMsg ${LOG_FILE} "[*] Fill up left $((${DISK_FREE_M}-${FILE_MAJOR_SIZE_M})) space with ${blocksz} small files"
	local -i i=1
	while :;do
		filename=testfile-small-${i}
		dd if=/dev/zero of=${WORK_PLACE}/${filename} bs=${blocksz} count=1 >/dev/null 2>&1 || {
			f_LogMsg ${LOG_FILE} "Volume gets full being filled with files in ${blocksz} size"
			break
		}
		((i++))
	done
	RET=$?
	f_exit_or_not ${RET}

	sync

	f_LogMsg ${LOG_FILE} "[*] Remove even numbered files to make fs fragmented in ${blocksz} size"
	local -i j=
	for j in `seq ${i}`;do
		if [ "$((${j}%2))" -eq "0" ]; then
			filename=testfile-small-${j}
			${RM_BIN} -rf ${WORK_PLACE}/${filename}
		fi
	done
	RET=$?
	f_exit_or_not ${RET}

	sync
}

function f_activate_discontig()
{
	ORIG_VOLUME_SIZE_BK=$((ORIG_VOLUME_SIZE/${BLOCKSIZE}))
	
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
        f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "refcount,xattr,metaecc,discontig-bg" ${JOURNALSIZE} ${ORIG_VOLUME_SIZE_BK}
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Change Volume Size"
	${TUNEFS_BIN} -S ${DEVICE} 0
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
        RET=$?
        f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Fillup volume with huge unwritten region and small files:"
	f_fillup_volume_almost_full
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
        f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount device ${DEVICE}"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	if [ -n "${MULTI_TEST}" -a "${TYPE}" == "extent" ];then
		f_LogRunMsg ${RUN_LOG_FILE} "[*] Fill up contiguous ${TYPE} block groups among ${MPI_HOSTS}:"
		f_LogMsg ${LOG_FILE} "CMD: ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${CONTIG_BG_FILLER} -t ${TYPE} -e ${FILL_CONTIG_EBG_M} -i ${FILL_CONTIG_IBG_NUM} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} ${MOUNT_POINT}"
		${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_BTL_ARG} ${MPI_BTL_IF_ARG} --host ${MPI_HOSTS} ${CONTIG_BG_FILLER} -t ${TYPE} -e ${FILL_CONTIG_EBG_M} -i ${FILL_CONTIG_IBG_NUM} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	else
		f_LogRunMsg ${RUN_LOG_FILE} "[*] Fill up contiguous ${TYPE} block groups:"
		f_LogMsg ${LOG_FILE} "CMD: ${CONTIG_BG_FILLER} -t ${TYPE} -e ${FILL_CONTIG_EBG_M} -i ${FILL_CONTIG_IBG_NUM} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} ${MOUNT_POINT}"
		${CONTIG_BG_FILLER} -t ${TYPE} -e ${FILL_CONTIG_EBG_M} -i ${FILL_CONTIG_IBG_NUM} -c ${CLUSTERSIZE} -d ${DEVICE} -o ${LOG_DIR} ${MOUNT_POINT} >>${LOG_FILE} 2>&1
	fi
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

f_activate_discontig 
