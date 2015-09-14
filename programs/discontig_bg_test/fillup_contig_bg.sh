#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# fillup_contig_bg.sh
#
# Description:  It's a simple script to fill up contiguous block group.
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
WORK_PLACE_DIRENT="ocfs2-fillup-contig-bg-dir-`hostname`"
GEN_EXTENTS_BIN="${BINDIR}/gen_extents"
SETXATTR="`which sudo` -u root `which setfattr`"

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
LOG_FILE=

CLUSTERSIZE=4096
TYPE=inode
SLOTS=0
DEVICE=
WORK_PLACE=

FILL_CONTIG_EBG_M=
FILL_CONTIG_IBG_NUM=
SLOTNUM=
MULTI_TEST=

OCFS2_LINK_MAX=65000
OCFS2_CONFIG_PATH="/etc/ocfs2/"
################################################################################
# Utility Functions
################################################################################
function f_usage()
{
	echo "usage: `basename ${0}` [-m] [-t type] [-e extent_size] [-i num_inodes] [-c clustersize] [-d device] [-o logs_dir] <mount point>"
	exit 1;

}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi
	
	while getopts "hmd:o:c:t:e:i:" options; do
		case $options in
		d ) DEVICE="$OPTARG";;
		e ) FILL_CONTIG_EBG_M="$OPTARG";;
		i ) FILL_CONTIG_IBG_NUM="$OPTARG";;
		o ) LOG_DIR="$OPTARG";;
		c ) CLUSTERSIZE="$OPTARG";;
		t ) TYPE="$OPTARG";;
		m ) MULTI_TEST=1;;
		h ) f_usage
			exit 1;;
		* ) f_usage
			exit 1;;
		esac
	done

	shift $(($OPTIND -1))
	MOUNT_POINT=${1}
}

function f_get_slotnum()
{
	local tmp_hosts=/tmp/.tmp_hosts
	local tmp_nums=/tmp/.tmp_nums
	local slotnum=

	local -i ptr=1
	local -i pos=1
	local found=0

	cat ${OCFS2_CONFIG_PATH}/*.conf|grep num > $tmp_nums
	cat ${OCFS2_CONFIG_PATH}/*.conf|grep name > $tmp_hosts
	
	while read line;do
		echo $line |grep `hostname` && {
			found=1
			pos=${ptr}
			break
		}
		((ptr++))
	done < $tmp_hosts

	ptr=1
	while read line;do
		if [ "$ptr" -eq "$pos" ];then
			break
		fi
		((ptr++))
	done < $tmp_nums

	slotnum=`echo $line|cut -d'=' -f2`

	${RM_BIN} -rf $tmp_hosts
	${RM_BIN} -rf $tmp_nums

	echo $slotnum
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

	if [ -z "${MULTI_TEST}" ];then
		SLOTNUM=0
	else
		SLOTNUM=`f_get_slotnum`
	fi
	
	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1
	
	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-\
%M-%S`-fillup-contig.log"

}

function f_fillup_ibg()
{
	local -i i=
	local filename_prefix=
	local filename=
	local inode_alloc="//inode_alloc:000${SLOTNUM}"

	f_LogMsg ${LOG_FILE} "Fill inode block groups by touching ${FILL_CONTIG_IBG_NUM} files."
	filename_prefix=fill_contig_igb_testfile
	for i in `seq ${FILL_CONTIG_IBG_NUM}`;do
		filename=${filename_prefix}${i}
		${TOUCH_BIN} ${WORK_PLACE}/$filename >/dev/null  2>&1|| {
			f_LogMsg ${LOG_FILE} "No discontig block group created until volume gets full"
			RET=1
			return $RET
		}
	done

	sync

	${DEBUGFS_BIN} -R "stat ${inode_alloc}" ${DEVICE} |grep -q "Tree Depth" && {
		f_LogMsg ${LOG_FILE} "Oh, boy, now we have activated discontiguous inode block group."
		return 0
	}

	i=1
	filename_prefix=fill_contig_igb_testfile_addup_inode
	while :;do
		filename=${filename_prefix}${i}
		if [ "${i}" -gt "$OCFS2_LINK_MAX" ];then
			mkdir -p ${WORK_PLACE}/${filename}
			i=1
			filename_prefix=${filename}/
			continue
		else
			dd if=/dev/zero of=${WORK_PLACE}/$filename bs=4k count=1 >/dev/null  2>&1|| {
				f_LogMsg ${LOG_FILE} "No discontig block group created until volume gets full."
				RET=1
				break
			}
		fi

		sync

		${DEBUGFS_BIN} -R "stat ${WORK_PLACE_DIRENT}/${filename}" ${DEVICE} |grep -q "Sub Alloc Group" && {
			f_LogMsg ${LOG_FILE} "Oh, boy, now we have activated one discontiguous inode block group."
			break;
		}

		((i++))
	done
	RET=$?

	return $RET
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

function f_fillup_ebg()
{
	local filename=${WORK_PLACE}/fill_contig_egb_testfile_`hostname`
	local fileszie=
	local extent_alloc="//extent_alloc:000${SLOTNUM}"

	filesize=$((${FILL_CONTIG_EBG_M}*1024*1024/2))

	f_LogMsg ${LOG_FILE} "Fill contiguous extent block groups by gen_extents"
	f_LogMsg ${LOG_FILE} "CMD: ${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1"
	${GEN_EXTENTS_BIN} -f ${filename} -l ${filesize} -c ${CLUSTERSIZE} -k 1 >>${LOG_FILE} 2>&1
        if [ "$?" -ne "0" ];then
                return 1
        fi

	sync

	${DEBUGFS_BIN} -R "stat ${extent_alloc}" ${DEVICE} |grep -q "Tree Depth" && {
		f_LogMsg ${LOG_FILE} "Oh, boy, now we have activated one discontiguous extent block group."
		return 0
	}

	# if above operation didn't consume up the contig extent block
	# exactly, we need to slowly force the filling-up by populating
	# xattr blocks from exteng bg.
	local -i i=1
	local filename_prefix=fill_contig_egb_testfile_addup_xattr
	f_LogMsg ${LOG_FILE} "Use additional xattr extents to activate the discontig."
	while :;do
		filename=${filename_prefix}${i}
		if [ "${i}" -gt "$OCFS2_LINK_MAX" ];then
			mkdir -p ${WORK_PLACE}/${filename}
			# We should not trying to make more directories
			# if the test run out of inodes against the test
			# file system, which means that the test failed
			# to make the test storage fragmented this time
			# so that the discontig block group did not took
			# affected.  By breaking the test per mkdir failure,
			# we can fix the test hanging issue with meaningful
			# log information to indicate that.
			# TODO: Figure out a better approach to make discontig
			# block group ASAP.
                        if [ "$?" -ne "0" ];then
                                f_LogMsg ${LOG_FILE} "mkdir ${WORK_PLACE}/${filename} failed."
                                break;
                        fi

			i=1
			filename_prefix=${filename}/
			continue
		else
			${TOUCH_BIN} ${WORK_PLACE}/$filename >>${LOG_FILE} 2>&1|| {
				f_LogMsg ${LOG_FILE} "touch ${filename} failed."
				RET=1
				break
			}

			sync

			for j in $(seq 100);do
				${SETXATTR} -n user.name${j} -v value${j} ${WORK_PLACE}/${filename} >>${LOG_FILE} 2>&1 || {
					f_LogMsg ${LOG_FILE} "setxattr failed."
					break
				}

				f_is_xattr_in_block ${WORK_PLACE_DIRENT}/${filename} ${DEVICE} && {
					break
				}
			done

		fi

		sync

		${DEBUGFS_BIN} -R "stat ${extent_alloc}" ${DEVICE} |grep -q "Tree Depth" && {
			f_LogMsg ${LOG_FILE} "Oh, boy, now we have activated one discontiguous extent block group."
			break;
		}

		((i++))
	done
	RET=$?

	return $RET
}

function f_fillup_bg()
{
	f_LogMsg ${LOG_FILE} "Mount ${DEVICE} to ${MOUNT_POINT}:"
	f_mount ${LOG_FILE} ${DEVICE} ${MOUNT_POINT} ocfs2 ${MOUNT_OPTS}
	f_exit_or_not ${RET}

	WORK_PLACE=${MOUNT_POINT}/${WORK_PLACE_DIRENT}
	${MKDIR_BIN} -p ${WORK_PLACE}

	if [ "${TYPE}" == "inode" ];then
		f_fillup_ibg
	else
		f_fillup_ebg
	fi

	f_LogMsg ${LOG_FILE} "Umount device ${DEVICE} from ${MOUNT_POINT}"
	f_umount ${LOG_FILE} ${MOUNT_POINT}
	RET=$?
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

f_fillup_bg
