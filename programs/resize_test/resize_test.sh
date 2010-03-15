#!/bin/sh
#
# resize_test -c -o <outdir> -d <device> -i <iters> -l <label> -m <mntdir>
#
# Requires the device to be formatted with enough space
# left over to extend the device in <iters> chunks of
# blocks, where each chunk has to be greater than a cluster
#

if [ -f `dirname ${0}`/config.sh ]; then
	. `dirname ${0}`/config.sh
fi

PATH=$PATH:/sbin:${BINDIR}        # Add /sbin to the path for ocfs2 tools

usage() {
    echo "usage: resize_test.sh -c -o <outdir> -d <device> -l <label> -i <iters> -m <mntdir>"
    echo "       -i number of resize iterations"
    echo "       -o output directory for the logs"
    echo "       -d device"
    echo "       -l label"
    echo "       -c consume space after resize"
	echo "       -m mount dir for moutn and umount"
    exit 1;
}

get_stats() {
    num1=`${DEBUGFS} -R "stats" ${device} 2>/dev/null | awk '/Block Size Bits/ {print $4;}'`
    num2=`${DEBUGFS} -R "stats" ${device} 2>/dev/null | awk '/Cluster Size Bits/ {print $8;}'`
    num3=`${DEBUGFS} -R "stats" ${device} 2>/dev/null | awk '/Clusters:/ {print $4;}'`

    if [ ${num1} -eq 0 ] || [ ${num2} -eq 0 ] || [ ${num3} -eq 0 ]
    then
        echo "error: device not formatted"
        exit 1
    fi

    #in bytes
    blocksz=$[$[2**$[${num1} - 9]]*512]
    clustsz=$[$[2**$[${num2} - 9]]*512]

    #in blocks
    numclst=$[${num3} * ${clustsz}/${blocksz}]

	bpc=$[${clustsz}/${blocksz}]
}

get_partsz() {
    dev=`echo ${device} | sed 's/\/dev\///'`
    num=`cat /proc/partitions | awk -v DEV=${dev} '
		BEGIN{dev=DEV} // {split($0, a); if (a[4] == dev) {printf("%u\n", $3); exit 0;} }'`
    if [ ${num} -eq 0 ]
    then
        echo "error: unable to find size of device"
        exit 1
    fi

    #partsz in blocksz blocks
    if [ ${blocksz} == 512 ]
    then
        partsz=$[${num}*2]
    else
        partsz=$[${num}/$[${blocksz}/1024]]
    fi
    return 0
}

do_mount() {
    # mount the device
    echo -n "mount "
    $MOUNT_BIN ${device} ${mntdir} >/dev/null 2>&1
    if [ $? -ne 0 ]
    then
        echo -n "FAILED. Check dmesg for errors." 2>&1
        exit 1
    else
        echo "OK"
    fi

}

do_umount() {
    # umount the volume
    echo -n "umount "
    ${UMOUNT_BIN} ${mntdir} >/dev/null 2>&1
    if [ $? -ne 0 ]
    then
        echo "FAILED. Check dmesg for errors." 2>&1
        exit 1
    else
        echo "OK"
    fi
}

do_consume() {
    do_mount

    # create 1M sized files
    fillbsz=1048576

    # find the free space
    freespace=`df --block-size=${fillbsz} ${device} |
		awk -v DEV=${device} 'BEGIN {dev=DEV;} // { if ($1 == dev) print $4; }'`

    # add files to fill up (not necessarily full)
    usedir=${mntdir}/`${DATE} +%Y%m%d_%H%M%S`
    ${MKDIR} -p ${usedir}
    echo -n "create ${freespace} files "
    j=0
    for i in `seq ${freespace}`
    do
        if [ $[$i % 11] -eq 0 ]
        then
            if [ $j -eq 10 ]
            then
                echo -ne "\b\b\b\b\b\b\b\b\b\b          \b\b\b\b\b\b\b\b\b\b"
                j=0
            else
                echo -n "."
                j=$[$j+1]
            fi
        fi
        ${DD} if=/dev/zero of=${usedir}/file$i bs=${fillbsz} count=1 >/dev/null 2>&1
        if [ $? -ne 0 ]
        then
            i=0
            echo
            break;
        fi
    done
    if [ $i -ne 0 ] ; then echo ; fi

    do_umount

    return 0
}

do_fsck() {
    if [ "$#" -lt "1" ]
    then
        echo "do_fsck(): <out>" >&2
        exit 1
    fi
    out=$1
    echo -n "fsck "
    echo ${FSCK} -fn ${device} >${out}
    ${FSCK} -fn ${device} >>${out} 2>&1
    grep "All passes succeeded" ${out} >/dev/null 2>&1
    if [ $? -ne 0 ] ;
    then
        echo "FAILED. Errors in ${out}"
        exit 1
    else
        echo "OK"
    fi
    return 0
}

do_debugfs() {
    if [ "$#" -lt "1" ]
    then
        echo "do_debugfs(): <out>" >&2
        exit 1
    fi
    out=$1
    ${DEBUGFS} -R "stat //global_bitmap" -n ${device} >${out} 2>&1
    return 0
}

do_tunefs() {
    if [ "$#" -lt "2" ]
    then
        echo "do_tunefs(): <out> <blk>" >&2
        exit 1
    fi
    out=$1
    blk=$2
    if [ $blk -eq 0 ]
    then
        blk=
        echo -n "grow device to e-o-d "
    else
        echo -n "grow device to ${blk} blocks "
    fi

    echo ${TUNEFS} -v -S ${device} ${blk} > ${out}
    ${TUNEFS} -v -S ${device} ${blk} >>${out} 2>&1

    if [ $? -eq 0 ]; then
	 sync
	 g_size=`${DEBUGFS} -R "stat //global_bitmap" ${device} 2>/dev/null | awk '/Size:/ {print $8;}'`
	 blocks=$((${g_size}/${blocksz}))
	 if [ -z ${blk} ];then
		blk=${partsz}
	 fi
	 #In the case when blk number didn't align to cluster
	 if [ "$((${blk}%${bpc}))" != "0" ];then
		blk=$((${blk}+${bpc}-$((${blk}%${bpc}))))
		if [ ${blk} -gt ${partsz} ]; then
			blk=$((${blk}-${bpc}))
		fi
	 fi
	 if [ "${blocks}" != "${blk}" ]; then
		echo "FAILED, wanted to grow ${blk}, but got ${blocks} instead"
		return 1
	 fi
    else
	 echo "FAILED. Errors in ${out}"
	 exit 1
    fi

    echo "OK"
    return 0
}

normal_resize_test() {
	online=$1

	incblk=$[$[${partsz}-${numclst}]/${iters}]
	if [ ${incblk} -lt $[${clustsz}/${blocksz}] ]
	then
		echo "error: reduce number of iterations"
		exit 1
	fi

	blocks=${numclst}

	echo "resize ${device} from ${numclst} to ${partsz} blocks in ${incblk} block chunks"

	alldone=0
	while [ 1 ]
	do
		YMD=`${DATE} +%Y%m%d_%H%M%S`
		tuneout=${outdir}/${YMD}.tune
		fsckout=${outdir}/${YMD}.fsck
		dbgout=${outdir}/${YMD}.dbg

		blocks=$[${blocks}+${incblk}]
		if [ ${blocks} -gt ${partsz} ]
		then
			blocks=0
		fi

		if [ $online -eq 1 ]
		then
			do_mount
		fi

		do_tunefs ${tuneout} ${blocks}
		all_done=$?
		sync

		if [ $online -eq 1 ]
		then
			do_umount
		fi

		do_debugfs ${dbgout}

		if [ ${blocks} -ne 0 ]
		then 
			desired_clusters=$[${blocks}/${bpc}]
			clusters=`${GREP} "Clusters:" ${dbgout}|awk '{print $4}'`
			if [ ${desired_clusters} -ne ${clusters} ]
			then
				echo "Resize failed, the desired new clusters should be ${desired_clusters}"
				exit 1
			fi
		fi

		do_fsck ${fsckout}

		if [ $alldone -eq 1 ] || [ ${blocks} -eq 0 ] || [ ${blocks} -eq ${partsz} ]
		then
			break;
		fi

		if [ ${consume} -eq 1 ]
		then
			do_consume
			do_debugfs ${dbgout}.2
		fi
	done
}

do_backup_test() {
	start=$1
	end=$2

	YMD=`${DATE} +%Y%m%d_%H%M%S`
	tuneout=${outdir}/${YMD}.tune
	fsckout=${outdir}/${YMD}.fsck
	dbgout=${outdir}/${YMD}.dbg

	echo "y"|${MKFS} -b ${blocksz} -C ${clustsz} -N 4 -L ${label} --fs-features=backup-super ${device} ${start} >/dev/null

	# empty the backup at first.
	${DD} if=/dev/zero of=${device} bs=${blocksz} count=1 seek=${backup_blkno} >/dev/null 2>&1

	${FSCK} -r 1 -y ${device} >${fsckout} 2>&1
	${GREP} "Bad magic number" ${fsckout} >/dev/null 2>&1
	if [ $? -ne 0 ] ;
	then
		echo "Corrupt backup super block failed. Errors in ${fsckout}"
		exit 1
	fi
	
	do_mount
	do_tunefs ${tuneout} ${end}
	do_umount
	do_fsck ${fsckout}

	# empty the super block.
	${DD} if=/dev/zero of=${device} bs=${blocksz} count=1 seek=2 >/dev/null 2>&1

	${DEBUGFS} -R "stats" ${device} >${dbgout} 2>&1
	${GREP} "Bad magic number" ${dbgout} >/dev/null 2>&1
	if [ $? -ne 0 ] ;
	then
		echo "Corrupt super block failed. Errors in ${dbgout}"
		exit 1
	fi

	# recover from it.
	${FSCK} -r 1 -y ${device} >${fsckout}
	${GREP} "RECOVER_BACKUP_SUPERBLOCK" ${fsckout} >/dev/null 2>&1
	if [ $? -ne 0 ] ;
	then
		echo "Recover backup super block failed. Errors in ${fsckout}"
		exit 1
	fi
}

online_boundary_test() {
	group_bitmap_size=$[$[${blocksz}-64]*8]

	YMD=`${DATE} +%Y%m%d_%H%M%S`
	tuneout=${outdir}/${YMD}.tune
	fsckout=${outdir}/${YMD}.fsck
	dbgout=${outdir}/${YMD}.dbg

	# normal backup super block test,
	start=$[${backup_blkno}/2]
	end=$partsz
	do_backup_test $start $end

	# test when the last group descriptor contains the backup superblocks.
	start=$[${backup_blkno}-1]
	end=$[${backup_blkno}*2]
	do_backup_test $start $end

	# resize from only one group descriptor to many.
	start=$[${group_bitmap_size}*${bpc}-1]

	if [ ${start} -gt ${partsz} ]
	then
		echo "volume size is too small or cluster size is too large."
		echo "Using cs=${clustsz}, there is only one group for the volume."
		echo "Skip online_boundary_test."
		return
	fi
	echo "y"|${MKFS} -b ${blocksz} -C ${clustsz} -N 4 -L ${label} ${device} ${start} >/dev/null

	do_mount
	do_tunefs ${tuneout} ${partsz}
	do_umount
	do_fsck ${fsckout}

}

TUNEFS="`which sudo` -u root `which tunefs.ocfs2`"
MKFS="`which sudo` -u root `which mkfs.ocfs2`"
FSCK="`which sudo` -u root `which fsck.ocfs2`"
DEBUGFS="`which sudo` -u root `which debugfs.ocfs2`"
DD="`which sudo` -u root `which dd`"
MKDIR="`which sudo` -u root `which mkdir`"
GREP=`which grep`
DATE=`which date`
CHOWN_BIN=`which chown`
CHMOD_BIN=`which chmod`
SUDO="`which sudo` -u root"
MOUNT_BIN="`which sudo` -u root `which mount.ocfs2`"
UMOUNT_BIN="`which sudo` -u root `which umount`"

outdir=
device=
label=
iters=0
consume=0
OPTIND=1
while getopts "d:i:o:m:l:c" args
do
  case "$args" in
    o) outdir="$OPTARG";;
    d) device="$OPTARG";;
    i) iters="$OPTARG";;
    m) mntdir="$OPTARG";;
    l) label="$OPTARG";;
    c) consume=1;
  esac
done

if [ -z "${outdir}" ]
then
    echo "invalid output directory: ${outdir}"
    usage ;
fi

if [ ! -b "${device}" ]
then
    echo "invalid device: ${device}"
    usage ;
fi

if [ ${iters} -eq 0 ]
then
    echo "invalid number of iterations: ${iters}"
    usage ;
fi

echo "create logdir ${outdir}"
${MKDIR} -p ${outdir}

${SUDO} ${CHMOD_BIN} -R 777 ${outdir}
${SUDO} ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${outdir}

blocksz=0
clustsz=0
numclst=0
bpc=0
get_stats
backup_offset=1073741824	#1G offset
backup_blkno=$[${backup_offset}/${blocksz}]

partsz=0
get_partsz

do_umount
normal_resize_test 0

online_boundary_test

echo "y"|${MKFS} -b ${blocksz} -C ${clustsz} -N 4 -L ${label} ${device} ${numclst} >/dev/null
normal_resize_test 1

echo "resize test successful"

exit 0
