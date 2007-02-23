#!/bin/sh
#
# mkfs_test -o <outdir> -d <device>
#

usage() {
    echo "usage: ${MKFS_TEST} -o <outdir> -d <device>"
    echo "       -o output directory for the logs"
    echo "       -d device"
    exit 1
}

verify_sizes() {
    if [ "$#" -lt "4" ] ; then
        echo "verify_size(): blocksize clustersize volsize out" >&2
        exit 1
    fi

    B=$1
    C=$2
    V=$3
    O=$4

    RET=0

    ${DEBUGFS} -R "stats" ${device} >> ${O} 2>/dev/null

    num1=`${AWK} '/Block Size Bits/ {print $4;}' ${O}`
    num2=`${AWK} '/Cluster Size Bits/ {print $8;}' ${O}`
    num3=`${AWK} '/Clusters:/ {print $4;}' ${O}`

    if [ ${num1} -eq 0 ] || [ ${num2} -eq 0 ] || [ ${num3} -eq 0 ]
    then
        echo "error: device not formatted" >&2
        exit 1
    fi

    b=$[$[2**$[${num1} - 9]]*512]
    c=$[$[2**$[${num2} - 9]]*512]
    v=$[${num3} * ${c}/${b}]

     echo -n "verify ..... "

    if [ ${B} -ne ${b} ]; then
        echo "ERROR: Blocksize mismatch - found ${b}, expected ${B}" >> ${O}
        RET=1
    fi
    if [ ${C} -ne ${c} ]; then
        echo "ERROR: Clustersize mismatch - found ${c}, expected ${C}" >> ${O}
        RET=1
    fi
    if [ ${V} -ne ${v} ]; then
        echo "ERROR: Volumesize mismatch - found ${v}, expected ${V}" >> ${O}
        RET=1
    fi

    echo "" >> ${O}

    if [ ${RET} -ne 0 ]; then
        echo "FAILED. Errors in ${O}"
    else
        echo "OK"
    fi

    return ${RET}
}

get_partsz() {
    dev=`echo ${device} | sed 's/\/dev\///'`
    num=`cat /proc/partitions | ${AWK} -v DEV=${dev} '
		BEGIN{dev=DEV} // {split($0, a); if (a[4] == dev) {printf("%u\n", $3); exit 0;} }'`
    if [ ${num} -eq 0 ]; then
        echo "error: unable to find size of device"
        exit 1
    fi

    partsz=$[${num}*1024]
    return 0
}

do_fsck() {
    if [ "$#" -lt "1" ]; then
        echo "do_fsck(): <out>" >&2
        exit 1
    fi
    out=$1
    echo -n "fsck ..... "
    echo ${FSCK} -fn ${device} >>${out}
    ${FSCK} -fn ${device} >>${out} 2>&1
    grep "All passes succeeded" ${out} >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "FAILED. Errors in ${out}"
        exit 1
    else
        echo "OK"
    fi
    echo "" >> ${out}
    return 0
}

do_mkfs() {
    if [ "$#" -lt "5" ] ; then
        echo "do_mkfs(): blocksize clustersize device volsize out" >&2
        exit 1
    fi

    B=$1
    C=$2
    D=$3
    V=$4
    O=$5

    echo -n "mkfs ..... "
    echo ${MKFS} -b ${B} -C ${C} ${D} ${V} >> ${O}
    ${MKFS} -x -F -b ${B} -C ${C} -N 1 -J size=4M ${D} ${V} >> ${O} 2>&1
    echo "OK"
    echo "" >> ${O}
}

MKFS=`which mkfs.ocfs2`
FSCK=`which fsck.ocfs2`
DEBUGFS=`which debugfs.ocfs2`
GREP=`which grep`
DATE=`which date`
AWK=`which awk`

MKFS_TEST=`basename $0`

outdir=
device=
OPTIND=1
while getopts "d:i:o:c" args
do
  case "$args" in
    o) outdir="$OPTARG";;
    d) device="$OPTARG";;
  esac
done

if [ -z "${outdir}" ]; then
    echo "invalid output directory: ${outdir}"
    usage ;
fi

if [ ! -b "${device}" ]; then
    echo "invalid device: ${device}"
    usage ;
fi

echo "create logdir ${outdir}"
mkdir -p ${outdir}

#get partition size
partsz=0
get_partsz


numblks=1048576

testnum=1


### Test all combinations of blocksizes and clustersizes
for blks in 512 1024 2048 4096
do
    for clusts in 4096 8192 16384 32768 65536 131072 262144 524288 1048576
    do
        TAG=mkfs_test_${testnum}
        OUT=${outdir}/${TAG}.log

        echo "Test ${testnum}: -b ${blks} -C ${clusts}"
        do_mkfs ${blks} ${clusts} ${device} ${numblks} ${OUT}
        verify_sizes ${blks} ${clusts} ${numblks} ${OUT}
        do_fsck ${OUT}
        testnum=$[$testnum+1]
    done
done


### Test option '-T mail'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
echo "Test ${testnum}: -T mail"
echo -n "mkfs ..... "
${MKFS} -x -F -b 4K -C 4K -N 2 -T mail ${device} 262144 >>${OUT} 2>&1
echo "OK"
echo -n "verify ..... "
${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
if [ $num -ne 134217728 ]; then
    echo "ERROR: Journal size too small for type mail" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}"
else
    echo "OK"
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test option '-T datafiles'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
echo "Test ${testnum}: -T datafiles"
echo -n "mkfs ..... "
${MKFS} -x -F -b 4K -C 4K -N 2 -T datafiles ${device} 262144 >>${OUT} 2>&1
echo "OK"
echo -n "verify ..... "
${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
if [ $num -ne 33554432 ]; then
    echo "ERROR: Journal size too small for type datafiles" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}"
else
    echo "OK"
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test option '-J size=64M'
### Test option '-J size=256M'
for jrnlsz in 64 256
do
    TAG=mkfs_test_${testnum}
    OUT=${outdir}/${TAG}.log
    echo "Test ${testnum}: -J size=${jrnlsz}M"
    echo -n "mkfs ..... "
    ${MKFS} -x -F -b 4K -C 4K -N 2 -J size=${jrnlsz}M ${device} 262144 >>${OUT} 2>&1
    echo "OK"
    echo -n "verify ..... "
    ${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
    num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
    inbytes=$[$jrnlsz*1024*1024]
    if [ $num -ne ${inbytes} ]; then
        echo "ERROR: Journal size expected ${inbytes} but found ${num}" >> ${OUT}
        echo "" >> ${OUT}
        echo "FAILED. Errors in ${OUT}"
    else
        echo "OK"
    fi
    do_fsck ${OUT}
    testnum=$[$testnum+1]
done


### Test option '-N 4'
### Test option '-N 32'
for slots in 4 32
do
    TAG=mkfs_test_${testnum}
    OUT=${outdir}/${TAG}.log
    echo "Test ${testnum}: -N ${slots}"
    echo -n "mkfs ..... "
    ${MKFS} -x -F -b 4K -C 4K -N ${slots} -J size=4M ${device} 262144 >>${OUT} 2>&1
    echo "OK"
    echo -n "verify ..... "
    ${DEBUGFS} -R "stats" ${device} >>${OUT} 2>&1
    num=`${AWK} '/Max Node Slots:/ {print $4;}' ${OUT}`
    if [ $num -ne ${slots} ]; then
        echo "ERROR: Node slots expected ${slots} but found ${num}" >> ${OUT}
        echo "" >> ${OUT}
        echo "FAILED. Errors in ${OUT}"
    else
        echo "OK"
    fi
    do_fsck ${OUT}
    testnum=$[$testnum+1]
done


### Test option '-L mylabel'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
echo "Test ${testnum}: -L mylabel"
label="my_label_is_very_very_very_long_to_the_point_of_being_useless"
echo -n "mkfs ..... "
${MKFS} -x -F -b 4K -C 4K -N 1 -L ${label} ${device} 262144 >>${OUT} 2>&1
echo "OK"
echo -n "verify ..... "
${DEBUGFS} -R "stats" ${device} >>${OUT} 2>&1
dsklab=`${AWK} '/Label:/ {print $2;}' ${OUT}`
if [ ${label} != ${dsklab} ]; then
    echo "ERROR: Label found \"${dsklab}\" expected \"${label}\"" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}"
else
    echo "OK"
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test --no-backup-super option


### Test option '-M local'


