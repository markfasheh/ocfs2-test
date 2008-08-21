#!/bin/bash
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
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#

################################################################
#
# inode_stealing.sh -d <device> -n <node_list> [-o output_dir]
#
# Requires mpi can be runned in all the nodes and the user have
# sudo privilege to run as a root. the node_list must have at
# least 3 nodes and they are divided by comma and the first node
# must be the node which run this test.
# output_dir must exist in the all the nodes.
#
#

PATH=$PATH:/sbin

TUNEFS="`which sudo` -u root `which tunefs.ocfs2`"
MKFS="`which sudo` -u root `which mkfs.ocfs2`"
FSCK="`which sudo` -u root `which fsck.ocfs2`"
DEBUGFS="`which sudo` -u root `which debugfs.ocfs2`"
DD="`which sudo` -u root `which dd`"
MKDIR="`which sudo` -u root `which mkdir`"
MOUNT="`which sudo` -u root `which mount`"
TOUCH="`which sudo` -u root `which touch`"
CHMOD="`which sudo` -u root `which chmod`"
RM="`which sudo` -u root `which rm`"
SYNC=`which sync`
GREP=`which grep`
DATE=`which date`
BASH="`which sudo` -u root `which bash`"
SSH=`which ssh`
REMOTE_MOUNT=`which remote_mount.py`
REMOTE_UMOUNT=`which remote_umount.py`
MAX_INODES_TO_STEAL=1024	# This is copied from fs/ocfs2/suballoc.c.
RAND=0

#
# usage			Display help information and exit.
#
function usage()
{
	local script="${0##*/}"
	cat <<-EOF
	Usage: $script [options] device

	Options:
	    -d device
	    -n node list for mount and umount
	    -o output dir
	    -l label

	Examples:

	    $script -d /dev/sde1 -n ocfs2-test1,ocfs2-test2,ocfs2-test3 -o /tmp
	EOF
	exit 1
}

#
# warn_if_bad
#
#	$1	the result to check
#	$2	the result we want
#	$3	the error messge
#	$4	the line num of the caller
#
#
function warn_if_bad()
{
	local -i rc="$1"
	local -i wanted="$2"
	local script="${0##*/}"

	# Ignore if no problems
	[ "$rc" -eq "$wanted" ] && return 0

	# Broken
	shift
	echo "$script: $@">&2
	echo "$script: $@">&3
	echo "$script: $@"
	return 1
}

#
# exit_if_bad		Put out error message(s) if $1 has bad RC.
#
#	$1	the result to check
#	$2	the result we want
#	$3	the error messge
#	$4	the line num of the caller
#
#       Exits with 1 unless $1 is 0
#
function exit_if_bad()
{
	warn_if_bad "$@" || exit 1
	return 0
}

function set_log_file()
{
	output_log="$outdir/`date +%F-%H-%M-%S`-output.log"
	exec 3>&1
	exec 1>$output_log 2>&1
}

function get_mount_slot()
{
	MOUNT_SLOT=0
	for((i=0;i<3;i++))
	do
		if [ $1 = ${mount_seq[${i}]} ]
		then
			MOUNT_SLOT=$i
			return
		fi
	done
}
function do_mount()
{
	# mount the device
	echo -n "mount "

	# if the caller has given us some parameters, we will
	# mount them one by one first to let them use slot
	# 0,1,2.
	if [ $# = 3 ]; then
		mount_seq[0]=$1
		mount_seq[1]=$2
		mount_seq[2]=$3
		# mount the device in mount1 first, then mount2, mount3.
		# so that they will use inode_alloc:0000, 0001 and 0002
		# in this sequence.
		for ((i=0;i<3;i++))
		do
			${SSH} ${mount_seq[${i}]} ${MOUNT} -t ocfs2 -L ${label} ${mntdir}
			if [ $? -ne 0 ]
			then
				echo -n "FAILED. Check dmesg for errors in ${node_list[1]}." 2>&1
				exit 1
			fi
		done
	fi

	$REMOTE_MOUNT -m ${mntdir} -l ${label} -n ${nodelist} >/dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo -n "Remote mount FAILED. Check dmesg of other nodes for errors." 2>&1
		exit 1
	else
		echo "OK"
	fi
}

do_umount()
{
	# umount the volume
	echo -n "umount "

	$REMOTE_UMOUNT -m ${mntdir} -n ${nodelist} >/dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo "FAILED. Check dmesg for errors." 2>&1
		exit 1
	else
		echo "OK"
	fi
}

inode_free=0
function get_inode_free()
{
	inode_number=$1

	inode_free=`echo "stat //inode_alloc:000$inode_number"|${DEBUGFS} ${device}|${GREP} "Bitmap"|awk '{print $7}'`
}

#
# normal_inode_steal_test implement the basic inode steal test.
# it will always use node_list[0] as the test machine, but it
# don't need node_list[0] to use slot 0. The "slot" information
# is decided by the option given to this function.
#
function normal_inode_steal_test()
{
	echo "y"|${MKFS} -b 1K -C 1M -N 8 -L ${label} ${device} >/dev/null

	#save the perfect fsck output first for our future use.
	fsck_output="${outdir}/fsck.ocfs2.output"
	fsck_output_std="${outdir}/fsck.ocfs2.output.std"
	${FSCK} -f ${device} >${fsck_output_std}

	# mount the volume in all the nodes.
	do_mount "$@"

	# create file in node2 and node3. so that some blocks are reserved
	# in these nodes.
	# Our test block size is 1K, so we will allocate 2048 inodes from
	# global_bitmap once a time. This number may be changed in the future.
	# you can find  in the function ocfs2_clusters_per_group of mkfs.c.
	INODE_PER_GROUP=2048

	for ((ii=1;ii<3;ii++))
	do
		test_dir="${mntdir}/${node_list[${ii}]}"
		# We want to make the inode_alloc:000X just have 2 groups
		# allocated and only 2 free inodes.
		# So totally, we need to create
		# 2048 - 2(for 2 group descriptor) - 1(for the dir) - 2(free)
		# if this node is the slot 0. then "lost+found" use one inode,
		# so we have to remove that one also.
		new_inode=$[${INODE_PER_GROUP}*2-5]
		get_mount_slot ${node_list[${ii}]}
		slot=$MOUNT_SLOT
		if [ $slot = 0 ]; then
			new_inode=$[${new_inode}-1]
		fi
		${SSH} ${node_list[$ii]} ${MKDIR} -p ${test_dir}
		${SSH} ${node_list[$ii]} "for ((j=0;j<${new_inode}; j++)) do ${TOUCH} ${test_dir}/\${j}; done"
		${SSH} ${node_list[$ii]} ${SYNC}
	done

	# check whether inode_alloc in node 1 and 2 are good.
	get_mount_slot ${node_list[1]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	exit_if_bad $inode_free 2 "Create inode failed for ${node_list[1]}." $LINENO
	get_mount_slot ${node_list[2]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	exit_if_bad $inode_free 2 "Create inode failed for node ${node_list[2]}." $LINENO

	# create 2 dirs which will be used in future test.
	# one is used for creating enough inodes to make node 0 full.
	# the other one is used for inode stealing.
	used_up_dir="${mntdir}/used_up_dir"
	${SSH} ${node_list[0]} ${MKDIR} -p ${used_up_dir}
	${SSH} ${node_list[0]} ${CHMOD} o+rwx "${used_up_dir}"
	test_dir="test_dir"
	${SSH} ${node_list[0]} ${MKDIR} -p "${mntdir}/${test_dir}"
	${SSH} ${node_list[0]} ${CHMOD} o+rwx "${mntdir}/${test_dir}"

	#use up all the clusters in the volume.
	consume_dir="${mntdir}/${node_list[0]}"
	${SSH} ${node_list[0]} ${MKDIR} -p ${consume_dir}
	${SSH} ${node_list[0]} ${CHMOD} o+rwx "${consume_dir}"
	j=0
	tmp_output="/tmp/tmp_output"
	while [ 1 ]
	do
		${SSH} ${node_list[0]} ${BASH} -c "echo $j > ${consume_dir}/${j}" >${tmp_output} 2>&1
		if [ $? != 0 ]
		then
			${GREP} "No space left on device" ${tmp_output}
			exit_if_bad $? 0 "can't create file ${consume_dir}/${j}" $LINENO
			break
		fi
		((j++))
	done

	# use up all the inodes reserved in the inode_alloc for node 0.
	${SSH} ${node_list[0]} ${SYNC}
	get_mount_slot ${node_list[0]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	for ((j=0;j<${inode_free};j++))
	do
		${SSH} ${node_list[0]} ${TOUCH} "${used_up_dir}/${j}"
		exit_if_bad $? 0 "Can't touch ${used_up_dir}/${j}" $LINEO
	done
	${SSH} ${node_list[0]} ${SYNC}

	steal_j=$j
	# check whether node 0's slot is full and other nodes still have
	# free inodes.
	get_mount_slot ${node_list[0]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	exit_if_bad ${inode_free} 0 "inode:alloc:000${slot} isn't full." $LINENO
	get_mount_slot ${node_list[1]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	exit_if_bad ${inode_free} 2 "inode_alloc:000${slot} don't have free inodes now." $LINENO
	get_mount_slot ${node_list[2]}
	slot=$MOUNT_SLOT
	get_inode_free ${slot}
	exit_if_bad ${inode_free} 2 "inode_alloc:000${slot} don't have free inodes now." $LINENO

	# In node 0, steal inode from the next slot.
	# since we only use 3 node, so if next_slot=3, use 0 instead.
	get_mount_slot ${node_list[0]}
	slot=$MOUNT_SLOT
	((slot++))
	if [ $slot = 3 ]
	then
		slot=0
	fi
	for ((ii=0;ii<2;ii++))
	do
		${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
		exit_if_bad $? 0 "Can't touch ${mntdir}/${test_dir}/${j}" $LINENO
		${SSH} ${node_list[0]} ${SYNC}
		real_slot=`${DEBUGFS} -R "stat ${test_dir}/${j}" ${device}|${GREP} "Alloc"|awk '{print $4}'`
		exit_if_bad ${real_slot} ${slot} "We want ${slot}, but get ${real_slot}" $LINENO
		((j++))
	done
	get_inode_free ${slot}
	exit_if_bad $inode_free 0 "inode_alloc:000${slot} should be full." $LINENO

	# In node 0, steal inode next.
	((slot++))
	if [ $slot = 3 ]
	then
		slot=0
	fi
	for ((ii=0;ii<2;ii++))
	do
		${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
		exit_if_bad $? 0 "Can't touch ${mntdir}/${test_dir}/${j}" $LINENO
		${SSH} ${node_list[0]} ${SYNC}
		real_slot=`${DEBUGFS} -R "stat ${test_dir}/${j}" ${device}|${GREP} "Alloc"|awk '{print $4}'`
		exit_if_bad ${real_slot} ${slot} "We want ${slot}, but get ${real_slot}" $LINENO
		((j++))
	done
	get_inode_free ${slot}
	exit_if_bad $inode_free 0 "inode_alloc:000${slot} should be full." $LINENO

	# now no new inodes can be created in node 0.
	# try  MAX_INODES_TO_STEAL-4 times since we have successfully steal
	# 4 inodes above.
	for ((ii=0;ii<$[${MAX_INODES_TO_STEAL}-4];ii++))
	do
		${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
		exit_if_bad $? 1 "We shouldn't touch ${mntdir}/${test_dir}/${j}" $LINENO
	done

	# Go on a second loop first, and then delete 2 inode from node_list[0].
	# According to the schema, we should should be able to allocate new
	# inodes after we try MAX_INODES_TO_STEAL from other slots.
	${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
	exit_if_bad $? 1 "We shouldn't touch ${mntdir}/${test_dir}/${j}" $LINENO
	for ((ii=0;ii<2;ii++))
	do
		${SSH} ${node_list[0]} ${RM} "${used_up_dir}/${ii}"
	done

	for ((ii=0;ii<$[${MAX_INODES_TO_STEAL}-1];ii++))
	do
		${SSH} ${node_list[0]} ${TOUCH} ${mntdir}/${test_dir}/${j}
		exit_if_bad $? 1 "We shouldn't touch ${mntdir}/${test_dir}/${j}" $LINENO
	done

	get_mount_slot ${node_list[0]}
	slot=$MOUNT_SLOT
	for ((ii=0;ii<2;ii++))
	do
		${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
		exit_if_bad $? 0 "We should be able to touch ${mntdir}/${test_dir}/${j}" $LINENO
		${SSH} ${node_list[0]} ${SYNC}
		real_slot=`${DEBUGFS} -R "stat ${test_dir}/${j}" ${device}|${GREP} "Alloc"|awk '{print $4}'`
		exit_if_bad ${real_slot} ${slot} "We want ${slot}, but get ${real_slot}" $LINENO
		((j++))
	done

	# now we can't create any more inode once again.
	${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
	exit_if_bad $? 1 "We shouldn't touch ${mntdir}/${test_dir}/${j}" $LINENO

	# delete one inode from node 2, and then node 0 should be able to
	# steal from it.
	get_mount_slot ${node_list[2]}
	slot=$MOUNT_SLOT
	test_dir="${mntdir}/${node_list[2]}"
	${SSH} ${node_list[2]} "${RM} ${test_dir}/0"
	${SSH} ${node_list[2]} ${SYNC}
	test_dir="test_dir"
	${SSH} ${node_list[0]} ${TOUCH} "${mntdir}/${test_dir}/${j}"
	exit_if_bad $? 0 "We should be able to touch ${mntdir}/${test_dir}/${j}" $LINENO
	${SSH} ${node_list[0]} ${SYNC}
	real_slot=`${DEBUGFS} -R "stat ${test_dir}/${j}" ${device}|${GREP} "Alloc"|awk '{print $4}'`
	exit_if_bad ${real_slot} ${slot} "We want ${slot}, but get ${real_slot}" $LINENO
	((j++))

	${SSH} ${node_list[0]} ${TOUCH} ${mntdir}/${test_dir}/${j}
	exit_if_bad $? 1 "We shouldn't touch ${mntdir}/${test_dir}/${j}" $LINENO

	# create a new group in node 2 by touch a node directly there. The
	# local alloc there will allocate a new slot so that other nodes
	# can benefit from it.
	get_mount_slot ${node_list[2]}
	slot=$MOUNT_SLOT
	${SSH} ${node_list[2]} ${TOUCH} ${mntdir}/${test_dir}/${j}
	exit_if_bad $? 0 "We should be able to touch ${mntdir}/${test_dir}/${j}" $LINENO
	${SSH} ${node_list[${slot}]} ${SYNC}

	# steal inode from node 2 now.
	((j++))
	${SSH} ${node_list[0]} ${TOUCH} ${mntdir}/${test_dir}/${j}
	exit_if_bad $? 0 "We should be able to touch ${mntdir}/${test_dir}/${j}" $LINENO
	${SSH} ${node_list[0]} ${SYNC}
	real_slot=`${DEBUGFS} -R "stat ${test_dir}/${j}" ${device}|${GREP} "Alloc"|awk '{print $4}'`
	exit_if_bad ${real_slot} ${slot} "We want ${slot}, but get ${real_slot}" $LINENO

	# try to delete the stealed nodes.
	j=${steal_j}
	for ((ii=0;ii<4;ii++))
	do
		${SSH} ${node_list[0]} ${RM} ${mntdir}/${test_dir}/${j}
		${SSH} ${node_list[0]} dmesg|tail -n 1|${GREP} "ERROR"
		exit_if_bad $? 1 "rm ${mntdir}/${test_dir}/${j} failed." $LINENO
		((j++))
	done

	#umount and check error
	do_umount
	${FSCK} -fy ${device} >${fsck_output}
	diff ${fsck_output} ${fsck_output_std}>/dev/null
	exit_if_bad $? "0" "fsck find errors." $LINENO
}

function inode_steal_test()
{
	# As the normal_inode_steal_test always assuming that node_list[0]
	# steal from node_list[1] and node_list[2].
	# so we may let node_list[0] to mount in differnt slots(0,1,2) to see
	# whether it works.
	normal_inode_steal_test ${node_list[0]} ${node_list[1]} ${node_list[2]}
	normal_inode_steal_test ${node_list[2]} ${node_list[1]} ${node_list[0]}
	normal_inode_steal_test ${node_list[1]} ${node_list[0]} ${node_list[2]}
}

# get a random slot
function get_rand_slot()
{
	RAND=$[${RANDOM}%${node_num}]
}

# This function will touch inode randomly in every nodes.
# At last if we can't touch an inode in one node, the operations in all
# the other nodes should fail.
function random_inode_allocation_test()
{
	echo "y"|${MKFS} -b 4K -C 1M -N 8 -L ${label} ${device} >/dev/null

	#save the perfect fsck output first for our future use.
	fsck_output="${outdir}/fsck.ocfs2.output"
	fsck_output_std="${outdir}/fsck.ocfs2.output.std"
	${FSCK} -f ${device} >${fsck_output_std}

	# mount the volume in all the nodes.
	do_mount

	for ((i=0;i<${node_num};i++))
	do
		test_dir="${mntdir}/${node_list[${i}]}"
		${SSH} ${node_list[$i]} ${MKDIR} -p ${test_dir}
	done

	# create a reserved dir and it will be used when we
	# have inode space but no disk space for directory entry.
	reserved_dir="${mntdir}/reserved_dir"
	${MKDIR} -p ${reserved_dir}

	j=0
	echo_file=1
	tmp_output="${outdir}/tmp_output"
	while [ 1 ]
	do
		((j++))
		get_rand_slot
		slot=${RAND}
		test_dir="${mntdir}/${node_list[${slot}]}"
		file="${test_dir}/${j}"
		if [ $echo_file = 1 ]
		then
			echo "touch file ${file} in ${node_list[${slot}]}"
			${SSH} ${node_list[${slot}]} ${TOUCH} ${file} > ${tmp_output} 2>&1
			if [ $? != 0 ]
			then
				${GREP} "Connection reset by peer" ${tmp_output}
				if [ $? = 0 ]
				then
					sleep 5
					continue
				fi
				exit_if_bad 1 0 "touch ${file} failed, errno in ${tmp_output}" $LINENO
			fi

			echo "echo file ${file} in ${node_list[${slot}]}"
			${SSH} ${node_list[${slot}]} ${CHMOD} o+rwx ${file} > ${tmp_output} 2>&1
			if [ $? != 0 ]
			then
				${GREP} "Connection reset by peer" ${tmp_output}
				if [ $? = 0 ]
				then
					sleep 5
					continue
				fi
				exit_if_bad 1 0 "chmod ${file} failed, errno in ${tmp_output}" $LINENO
			fi

			${SSH} ${node_list[${slot}]} ${BASH} -c "echo $j > ${file}" >${tmp_output} 2>&1
			if [ $? != 0 ]
			then
				# we can't echo file any more.
				# try touch from now on.
				${GREP} "Connection reset by peer" ${tmp_output} 2>&1
				if [ $? = 0 ]
				then
					sleep 5
					continue
				fi
				${GREP} "No space left on device" ${tmp_output} 2>&1
				if [ $? = 0 ]
				then
					echo_file=0
					continue
				fi
				exit_if_bad 1 0 "echo $j to ${file} failed, errno in ${tmp_output}" $LINENO
			fi
			continue
		else
			echo "touch file ${file} in ${node_list[${slot}]}"
			${SSH} ${node_list[$slot]} ${TOUCH} ${file} > ${tmp_output} 2>&1
		fi
		if [ $? = 0 ]
		then
			continue
		fi

		try=0
		create_succ=0
		${GREP} "Connection reset by peer" ${tmp_output}
		while [ $? = 0 ]
		do
			if [ ${try} -gt 10 ]
			then
				break
			fi

			# try again.
			sleep 5
			${SSH} ${node_list[$slot]} ${TOUCH} ${file} > ${tmp_output} 2>&1
			if [ $? = 0 ]
			then
				create_succ=1
				break;
			fi
			${GREP} "Connection reset by peer" ${tmp_output}
		done

		if [ ${create_succ} = 1 ]
		then
			continue
		fi

		${GREP} "No space left on device" ${tmp_output}
		exit_if_bad $? 0 "can't create file ${file}" $LINENO

		# test whether we can succeed in reserved_dir.
		# If OK, it means that we have used up all the disk space for
		# directory, but inode can still be created. This situation
		# is rare, but we should handle it.
		# So go on the test.
		file="${reserved_dir}/${j}"
		echo "touch file ${file} in node ${slot}"
		${SSH} ${node_list[$slot]} ${TOUCH} ${file}
		if [ $? = 0 ]
		then
			continue
		fi

		# test whether we can succeed in other slots.
		for ((i=0;i<${node_num};i++))
		do
			if [ $i = ${slot} ]
			then
				continue
			fi
			test_dir="${mntdir}/${node_list[${i}]}"
			echo "touch file ${file} in ${node_list[${i}]}"
			${SSH} ${node_list[$i]} ${TOUCH} ${file}
			if [ $? != 0 ]
			then
				continue
			fi

			((j++))
			# we success in other slots, there is a chance
			# that they allocate a new inode alloc from their
			# local alloc. So now we should be able to steal
			# from it now.
			file="${reserved_dir}/${j}"
			echo "touch file ${file} in ${node_list[${slot}]}"
			${SSH} ${node_list[$slot]} ${TOUCH} ${file}
			exit_if_bad $? 0 "We should succeed." $LINENO
			break
		done

		# We success in creating inodes in other slot and steal
		# from it, so continue the test.
		if [ $i != ${node_num} ]
		then
			continue
		fi

		# if we reach here, it means that we can't touch file
		# in all the nodes. Perfect.
		echo "wow. random_inode_allocation_test succeed."

		do_umount
		${FSCK} -fy ${device} >${fsck_output}
		diff ${fsck_output} ${fsck_output_std}>/dev/null
		exit_if_bad $? "0" "fsck find errors." $LINENO
		return
	done
}

declare -a mount_seq
outdir="/tmp"
device=
nodelist=
label="inode_stealing"
iters=0
consume=0
OPTIND=1
while getopts "d:n:o:l:" args
do
	case "$args" in
		d) device="$OPTARG";;
		n) nodelist="$OPTARG";;
		o) outdir="$OPTARG";;
		l) label="$OPTARG";;
	esac
done

if [ ! -b "${device}" ]
then
    echo "invalid device: ${device}"
    usage
fi

if [ -z "${nodelist}" ]
then
    echo "invalid nodelist: ${nodelist}"
    usage
fi

#split node.
declare -a node_list
OIFS=$IFS
IFS=,
i=0
for n in $nodelist
do
	node_list[$i]=$n
	((i++))
done
IFS=$OIFS

node_num=$i
if [ $i -lt 3 -o $i -gt 8 ]
then
	echo "invalid nodelist: node number between 3 and 8."
	usage
fi

YMD=`${DATE} +%Y%m%d_%H%M%S`
mntdir="${outdir}/${YMD}"

set_log_file
# set -x
# mkdir in all the nodes.
$MKDIR -p ${mntdir}
${SSH} ${node_list[1]} ${MKDIR} -p ${mntdir}
${SSH} ${node_list[2]} ${MKDIR} -p ${mntdir}

inode_steal_test

random_inode_allocation_test

exit 0
