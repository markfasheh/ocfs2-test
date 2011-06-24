#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# verify_readonly.sh
#
# Copyright (C) 2011 Oracle.  All rights reserved.
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
WORK_PLACE_DIRENT=ocfs2-readonly-test
GEN_EXTENTS_BIN="${BINDIR}/gen_extents"
PUNCH_HOLE_BIN="${BINDIR}/punch_hole"
SPAWN_INODES_BIN="${BINDIR}/spawn_inodes"
REFLINK_BIN="`which reflink`"
SETXATTR="`which sudo` -u root `which setfattr`"
GETXATTR="`which sudo` -u root `which getfattr`"

USERNAME=`id -un`
GROUPNAME=`id -gn`

QUOTA_BIN="`which sudo` -u root `which quota`"
QUOTACHECK_BIN="`which sudo` -u root `which quotacheck`"
EDQUOTA_BIN="`which sudo` -u root `which edquota`"
SETQUOTA_BIN="`which sudo` -u root `which setquota`"
QUOTAON_BIN="`which sudo` -u root `which quotaon`"
QUOTAOFF_BIN="`which sudo` -u root `which quotaoff`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
VERIFY_LOG_DIR=
VERIFY_LOG_LS=
VERIFY_LOG_XATTR=
VERIFY_LOG_STAT=
VERIFY_LOG_STATF=
VERIFY_LOG_QUOTA_USR=
VERIFY_LOG_QUOTA_GRP=
VERIFY_LOG_MD5=

BLOCKSIZE=
CLUSTERSIZE=
JOURNALSIZE=0
BLOCKS=0
SLOTS=0
LABELNAME="ocfs2-readonly-tests"
MOUNT_OPTS_RW="rw,usrquota,grpquota"
MOUNT_OPTS_RO="ro,usrquota,grpquota"
DEVICE=
WORK_PLACE=
KERN_DIR=
FILE_DIR=

LOOPBACK=
LOOPDEVICE=/dev/loop0
LOOP_MOUNT_POINT=
O_DEVICE=
O_MOUNT_POINT=
LOOPBACK_IMG=

################################################################################
# Utility Functions
################################################################################
function f_usage()
{
    echo "usage: `basename ${0}` <-d device> [-l] [-o logdir] [-k kernel tarball] <mount point>"
    exit 1;
}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi
	
	while getopts "hld:o:k:" options; do
		case $options in
		d ) DEVICE="$OPTARG";;
		o ) LOG_DIR="$OPTARG";;
		k ) KERN_TARBALL="$OPTARG";;
		l ) LOOPBACK="1";;
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

	if [ -z "${KERN_TARBALL}" ];then
		f_usage
	fi

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	VERIFY_LOG_DIR=${LOG_DIR}/verify_logs
	${MKDIR_BIN} -p ${VERIFY_LOG_DIR} || exit 1

	VERIFY_LOG_LS=${VERIFY_LOG_DIR}/verify_log_ls
	VERIFY_LOG_XATTR=${VERIFY_LOG_DIR}/verify_log_xattr
	VERIFY_LOG_MD5=${VERIFY_LOG_DIR}/verify_log_md5
	VERIFY_LOG_STAT=${VERIFY_LOG_DIR}/verify_log_stat
	VERIFY_LOG_STATF=${VERIFY_LOG_DIR}/verify_log_statf
	VERIFY_LOG_QUOTA_USR=${VERIFY_LOG_DIR}/verify_log_quota_usr
	VERIFY_LOG_QUOTA_GRP=${VERIFY_LOG_DIR}/verify_log_quota_grp
	
	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-verify-readonly-run.log"
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-verify-readonly.log"

}

function f_untar_kernel()
{
	KERN_DIR=${WORK_PLACE}/kernel
	
	f_LogMsg ${LOG_FILE} "Untaring kernel tarball ${KERN_TARBALL} to ${KERN_DIR}"
	mkdir -p ${KERN_DIR} || exit 1
	tar -xzvf ${KERN_TARBALL} -C ${KERN_DIR} >/dev/null 2>&1|| {
		return 1
	}

	sync

	f_LogMsg ${LOG_FILE} "Generating logfile for 'ls -lR'"
	ls -lR ${KERN_DIR} >${VERIFY_LOG_LS} 2>&1
}

function f_verify_kernel()
{
	local ret=0
	KERN_DIR=${WORK_PLACE}/kernel	

	f_LogMsg ${LOG_FILE} "Verify kernel ${KERN_DIR} with logfile ${VERIFY_LOG_LS}"
	ls -lR ${KERN_DIR} | diff - ${VERIFY_LOG_LS} >/dev/null 2>&1 || {
		f_LogMsg ${LOG_FILE} "Verify kernel tree failed."
		ls -lR ${KERN_DIR} > ${VERIFY_LOG_LS}.diff
		return 1
	}
}

function f_generate_file()
{
	FILE_DIR=${WORK_PLACE}/file_repo
	local filename=${FILE_DIR}/extents
	local filesize=1048576000
	local offset=0
	local num_holes=100
	local hole_len=0
	local gap=$((${filesize}/${num_holes}/2))
	local hop=
	local num_xattrs=100
	local num_reflinks=100
	local reflink=

	mkdir -p ${FILE_DIR} || exit 1

	f_LogMsg ${LOG_FILE} "Generate file ${filename} with extents."
	${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1 >>${LOG_FILE} 2>&1 || {
                return 1
        }

	f_LogMsg ${LOG_FILE} "Punch hole on file ${filename}"
	for i in `seq ${num_holes}`;do
		hole_len=$((${RANDOM}%${gap}))
		${PUNCH_HOLE_BIN} -f ${filename} -s ${offset} -l ${hole_len} >/dev/null 2>&1|| {
			return 1
		}
		hop=$((${RANDOM}%${gap}))
		offset=$((${offset}+${hole_len}+${hop}))
	done

	f_LogMsg ${LOG_FILE} "Attach xattr on file ${filename}"
	for i in `seq ${num_xattrs}`;do
		${SETXATTR} -n user.name-${i} -v "xattr-value-${i}" ${filename} || {
			return 1
		}
	done

	f_LogMsg ${LOG_FILE} "Make reflinks on file ${filename}"
	for i in `seq ${num_reflinks}`;do
		reflink=${filename}-ref-${i}
		${REFLINK_BIN} ${filename} ${reflink} || {
			return 1
		}
	done

	sync

	f_LogMsg ${LOG_FILE} "Generate md5sum logfile"
	md5sum ${filename} |cut -d' ' -f1 > ${VERIFY_LOG_MD5} || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Generate xattr logfile"
	${GETXATTR} -d ${filename} > ${VERIFY_LOG_XATTR} || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Generate stat logfile"
	stat ${filename} > ${VERIFY_LOG_STAT} || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Generate user quota logfile"
	${QUOTA_BIN} -u ${USERNAME} > ${VERIFY_LOG_QUOTA_USR} 2>&1|| {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Generate group quota logfile"
	${QUOTA_BIN} -g ${GROUPNAME} > ${VERIFY_LOG_QUOTA_GRP} 2>&1|| {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Generate statf logfile"
	stat -f ${filename} > ${VERIFY_LOG_STATF} || {
		return 1
	}
}

function f_verify_file()
{
	FILE_DIR=${WORK_PLACE}/file_repo
	local filename=${FILE_DIR}/extents
	local num_reflinks=100
	local reflink=

	f_LogMsg ${LOG_FILE} "Verify file ${filename} with md5 logfile ${VERIFY_LOG_MD5}"
	md5sum ${filename} | cut -d' ' -f1 | diff - ${VERIFY_LOG_MD5} >/dev/null 2>&1 || {
		f_LogMsg ${LOG_FILE} "Verify file failed."
		md5sum ${filename} | cut -d' ' -f1 > ${VERIFY_LOG_MD5}.diff
		return 1
	}

	f_LogMsg ${LOG_FILE} "Verify xattrs on ${filename} with logfile ${VERIFY_LOG_XATTR}"
	${GETXATTR} -d ${filename} | diff - ${VERIFY_LOG_XATTR} >/dev/null 2>&1 || {
		f_LogMsg ${LOG_FILE} "Verify xattrs failed."
		${GETXATTR} -d ${filename} > ${VERIFY_LOG_XATTR}.diff
		return 1
	}

	f_LogMsg ${LOG_FILE} "Verify stat on ${filename} with logfile ${VERIFY_LOG_STAT}"
	stat ${filename} | diff - ${VERIFY_LOG_STAT} >/dev/null 2>&1 || {
		f_LogMsg ${LOG_FILE} "Verify stat failed."
		stat ${filename} > ${VERIFY_LOG_STAT}.diff
		return 1
	}

	f_LogMsg ${LOG_FILE} "Verify user quota on ${USERNAME} with logfile ${VERIFY_LOG_QUOTA_USR}"
	${QUOTA_BIN} -u ${USERNAME} | diff - ${VERIFY_LOG_QUOTA_USR} >/dev/null 2>&1 || {
		${QUOTA_BIN} -u ${USERNAME} &>${VERIFY_LOG_QUOTA_USR}.diff
		return 1
	}

	f_LogMsg ${LOG_FILE} "Verify group quota on ${GROUPNAME} with logfile ${VERIFY_LOG_QUOTA_GRP}"
	${QUOTA_BIN} -g ${GROUPNAME} | diff - ${VERIFY_LOG_QUOTA_GRP} >/dev/null 2>&1 || {
		${QUOTA_BIN} -g ${GROUPNAME} &>${VERIFY_LOG_QUOTA_GRP}.diff
		return 1
	}

	for i in `seq ${num_reflinks}`;do
		reflink=${filename}-ref-${i}
		f_LogMsg ${LOG_FILE} "Verify reflinks on ${reflink} with logfile ${VERIFY_LOG_MD5}"
		md5sum ${reflink} | cut -d' ' -f1 | diff - ${VERIFY_LOG_MD5} >/dev/null 2>&1 || {
			f_LogMsg ${LOG_FILE} "Verify reflink failed."
			md5sum ${reflink} | cut -d' ' -f1  > ${VERIFY_LOG_MD5}-${reflink}.diff
			return 1
		}
	done

	f_LogMsg ${LOG_FILE} "Verify statf on ${filename} with logfile ${VERIFY_LOG_STATF}"
	stat -f ${filename} | diff - ${VERIFY_LOG_STATF} >/dev/null 2>&1 || {
		f_LogMsg ${LOG_FILE} "Verify statf failed."
		stat -f ${filename} > ${VERIFY_LOG_STATF}.diff
		return 1
	}
}

function f_setup_quota()
{
	f_LogMsg ${LOG_FILE} "Check on quota:"
	${QUOTACHECK_BIN} -agumf  >>${LOG_FILE} 2>&1 || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Turn on quota:"
        ${QUOTAON_BIN} ${MOUNT_POINT} >>${LOG_FILE} 2>&1 || {
		return 1
	}

	SOFT_SPACE_LIMIT=4194304         # space soft limit,4G
	HARD_SPACE_LIMIT=8388608         # space hard limit,16G
	SOFT_INODES_LIMIT=1000000         # inodes soft limit
	HARD_INODES_LIMIT=2000000         # inodes hard limit

	f_LogMsg ${LOG_FILE} "set inode quota(SPACE_SOFT:${SOFT_SPACE_LIMIT},SPACE_HARD:${HARD_SPACE_LIMIT},INODE_SOFT:${SOFT_INODES_LIMIT},INODE_HARD:${HARD_INODES_LIMIT}) for user ${USERNAME}:"
	${SETQUOTA_BIN} -u ${USERNAME} ${SOFT_SPACE_LIMIT} ${HARD_SPACE_LIMIT} ${SOFT_INODES_LIMIT} ${HARD_INODES_LIMIT} -a ${DEVICE} >>${LOG_FILE} 2>&1 || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "set inode quota(SPACE_SOFT:${SOFT_SPACE_LIMIT},SPACE_HARD:${HARD_SPACE_LIMIT},INODE_SOFT:${SOFT_INODES_LIMIT},INODE_HARD:${HARD_INODES_LIMIT}) for group ${GROUPNAME}:"
	${SETQUOTA_BIN} -g ${GROUPNAME} ${SOFT_SPACE_LIMIT} ${HARD_SPACE_LIMIT} ${SOFT_INODES_LIMIT} ${HARD_INODES_LIMIT} -a ${DEVICE} >>${LOG_FILE} 2>&1 || {
		return 1
	}
}

function f_loopback_preamble()
{
	f_LogMsg ${LOG_FILE} "Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "usrquota,grpquota,metaecc,refcount,xattr" ${JOURNALSIZE} ${BLOCKS} || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS_RW} || {
		return 1
	}

	WORK_PLACE=${MOUNT_POINT}

	LOOPBACK_IMG=${WORK_PLACE}/readonly.img
	LOOP_MOUNT_POINT=${WORK_PLACE}/readonly-mnt
	${MKDIR_BIN} -p ${LOOP_MOUNT_POINT}

	f_LogMsg "${LOG_FILE}" "Creating image for loopback device"
	dd if=/dev/zero of=${LOOPBACK_IMG} bs=1M count=10240 >/dev/null 2>&1 || {
		return 1
	}

	f_LogMsg ${LOG_FILE} "Setup loopback device from ${LOOPBACK_IMG} to ${LOOPDEVICE}"
	losetup ${LOOPDEVICE} ${LOOPBACK_IMG} || {
		return 1
	}

	O_DEVICE=${DEVICE}
	O_MOUNT_POINT=${MOUNT_POINT}

	DEVICE=${LOOPDEVICE}
	MOUNT_POINT=${LOOP_MOUNT_POINT}
}

function f_loopback_postamble()
{
	f_LogMsg "${LOG_FILE}" "Deleting loopback device ${LOOPDEVICE}"
	losetup -d ${LOOPDEVICE} || {
		return 1
	}

	DEVICE=${O_DEVICE}
	MOUNT_POINT=${O_MOUNT_POINT}

	f_LogMsg ${LOG_FILE} "Umount device ${DEVICE} from ${MOUNT_POINT}:"
        f_umount ${LOG_FILE} ${MOUNT_POINT} || {
		return 1
	}
}

function f_run_test()
{
	if [ "${LOOPBACK}" == "1" ];then
		f_LogRunMsg ${RUN_LOG_FILE} "[*] Preparing Loopback Device:"
		f_loopback_preamble
		RET=$?
		f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
		f_exit_or_not ${RET}
	fi

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mkfs device ${DEVICE}:"
	f_mkfs ${LOG_FILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} "usrquota,grpquota,metaecc,refcount,xattr" ${JOURNALSIZE} ${BLOCKS}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS_RW}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Set Up Quota:"
	f_LogMsg ${LOG_FILE} "[*] Set Up Quota:"
	f_setup_quota
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Untar Kernel Tarball:"
	f_LogMsg ${LOG_FILE} "[*] Untar Kernel Tarball:"
	f_untar_kernel
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Comprehensive File Test:"
	f_LogMsg ${LOG_FILE} "[*] Comprehensive File Test:"
	f_generate_file
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_LogMsg ${LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Set ${DEVICE} Read-only"
	f_LogMsg ${LOG_FILE} "[*] Set ${DEVICE} Read-only"
	if [ "${LOOPBACK}" == "1" ];then
		losetup -rf ${DEVICE}
	else
		blockdev --setro ${DEVICE}
	fi
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Read-only mount ${DEVICE} to ${MOUNT_POINT}:"
	f_LogMsg ${LOG_FILE} "[*] Read-only mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS_RO}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	#verify all items
	f_LogRunMsg ${RUN_LOG_FILE} "[*] Verify Kernel:"
	f_LogMsg ${LOG_FILE} "[*] Verify Kernel:"
	f_verify_kernel
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Verify Files:"
	f_LogMsg ${LOG_FILE} "[*] Verify Files:"
	f_verify_file
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Umount device ${DEVICE} from ${MOUNT_POINT}:"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	RET=$?
	f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
	f_exit_or_not ${RET}

	if [ "${LOOPBACK}" == "1" ];then
		f_LogRunMsg ${RUN_LOG_FILE} "[*] Removing Loopback Device:"
		f_loopback_postamble
		RET=$?
		f_echo_status ${RET}| tee -a ${RUN_LOG_FILE}
		f_exit_or_not ${RET}
	fi

	f_LogRunMsg ${RUN_LOG_FILE} "[*] Set ${DEVICE} Read-Write"
	f_LogMsg ${LOG_FILE} "[*] Set ${DEVICE} Read-Write"
	blockdev --setrw ${DEVICE}
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

f_LogRunMsg ${RUN_LOG_FILE} "=====================Readonly verify test starts:  `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Readonly verify test starts:  `date`\
====================="

for BLOCKSIZE in 512 1024 2048 4096;do
	for CLUSTERSIZE in 4096 8192 32768 1048576;do
		f_LogRunMsg ${RUN_LOG_FILE} "<- Running test with ${BLOCKSIZE} \
bs and ${CLUSTERSIZE} cs ->\n"
		f_LogMsg ${LOG_FILE} "<- Running test with ${BLOCKSIZE} bs \
and ${CLUSTERSIZE} cs ->"
		f_run_test
	done
done

f_LogRunMsg ${RUN_LOG_FILE} "=====================Readonly verify test ends: `date`\
=====================\n"
f_LogMsg ${LOG_FILE} "=====================Readonly verify test ends: `date`\
====================="
