#!/bin/sh
#
# resize_test -c -o <outdir> -d <device> -i <iters>
#
# Requires the device to be formatted with enough space
# left over to extend the device in <iters> chunks of
# blocks, where each chunk has to be greater than a cluster
#


usage() {
    echo "usage: resize_test.sh -c -o <outdir> -d <device> -i <iters>"
    echo "       -i number of resize iterations"
    echo "       -o output directory for the logs"
    echo "       -d device"
    echo "       -c consume space after resize"
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

do_consume() {
    # mount the device on mntdir
    echo -n "mount "
    mount -t ocfs2 ${device} ${mntdir} 2>/dev/null
    if [ $? -ne 0 ]
    then
        echo -n "FAILED. Check dmesg for errors." 2>&1
        exit 1
    else
        echo "OK"
    fi

    # create 1M sized files
    fillbsz=1048576

    # find the free space
    freespace=`df --block-size=${fillbsz} ${device} |
		awk -v DEV=${device} 'BEGIN {dev=DEV;} // { if ($1 == dev) print $4; }'`

    # add files to fill up (not necessarily full)
    usedir=${mntdir}/`${DATE} +%Y%m%d_%H%M%S`
    mkdir -p ${usedir}
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
        dd if=/dev/zero of=${usedir}/file$i bs=${fillbsz} count=1 >/dev/null 2>&1
        if [ $? -ne 0 ]
        then
            i=0
            echo
            break;
        fi
    done
    if [ $i -ne 0 ] ; then echo ; fi

    # umount the volume
    echo -n "umount "
    umount ${mntdir} 2>/dev/null
    if [ $? -ne 0 ]
    then
        echo "FAILED. Check dmesg for errors." 2>&1
        exit 1
    else
        echo "OK"
    fi
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
    echo ${TUNEFS} -S ${device} ${blk} > ${out}
    ${TUNEFS} -x -S ${device} ${blk} >>${out} 2>&1
    ${GREP} "Cannot grow volume size" ${out} >/dev/null 2>&1
    if [ $? -eq 0 ]
    then
         echo "OK (ENOSPC)"
         return 1
    fi

    ${GREP} "Wrote Superblock" ${out} >/dev/null 2>&1
    if [ $? -ne 0 ] ;
    then
        echo "FAILED. Errors in ${out}"
        exit 1
    fi
    echo "OK"
    return 0
}

TUNEFS=`which tunefs.ocfs2`
MKFS=`which mkfs.ocfs2`
FSCK=`which fsck.ocfs2`
DEBUGFS=`which debugfs.ocfs2`
GREP=`which grep`
DATE=`which date`

outdir=
device=
iters=0
consume=0
OPTIND=1
while getopts "d:i:o:c" args
do
  case "$args" in
    o) outdir="$OPTARG";;
    d) device="$OPTARG";;
    i) iters="$OPTARG";;
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
mkdir -p ${outdir}

if [ ${consume} -eq 1 ]
then
    mntdir=/tmp/`${DATE} +%Y%m%d_%H%M%S`
    echo "create mntdir ${mntdir}"
    mkdir -p ${mntdir}
fi

blocksz=0
clustsz=0
numclst=0
get_stats

partsz=0
get_partsz

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

    do_tunefs ${tuneout} ${blocks}
    all_done=$?

    do_debugfs ${dbgout}

    do_fsck ${fsckout}

    if [ $alldone -eq 1 ] || [ ${blocks} -eq 0 ]
    then
        break;
    fi

    if [ ${consume} -eq 1 ]
    then
        do_consume
        do_debugfs ${dbgout}.2
    fi
done

if [ ! -z ${mntdir} ]
then
    rmdir ${mntdir} 2>/dev/null 2>&1
fi

echo "resize test successful"

exit 0
