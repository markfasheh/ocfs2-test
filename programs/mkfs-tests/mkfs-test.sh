#!/bin/sh
#
# mkfs_test -o <outdir> -d <device>
#
usage() {
    echo "usage: ${MKFS_TEST} -o <outdir> -d <device> -m <mountpoint>"
    echo "       -o output directory for the logs"
    echo "       -d device"
    echo "       -m mountpoint"
    exit 1
}

verify_sizes() {
    if [ "$#" -lt "4" ] ; then
        echo "verify_size(): blocksize clustersize volsize out" |tee -a ${LOGFILE}
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
        echo "error: device not formatted" |tee -a ${LOGFILE}
        exit 1
    fi

    b=$[$[2**$[${num1} - 9]]*512]
    c=$[$[2**$[${num2} - 9]]*512]
    v=$[${num3} * ${c}/${b}]

     echo -n "verify ..... "  |tee -a ${LOGFILE}

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
        echo "FAILED. Errors in ${O}"  |tee -a ${LOGFILE}
    else
        echo "OK"  |tee -a ${LOGFILE}
    fi

    return ${RET}
}

get_partsz() {
    dev=`echo ${device} | sed 's/\/dev\///'`
    num=`cat /proc/partitions | ${AWK} -v DEV=${dev} '
		BEGIN{dev=DEV} // {split($0, a); if (a[4] == dev) {printf("%u\n", $3); exit 0;} }'`
    if [ ${num} -eq 0 ]; then
        echo "error: unable to find size of device"  |tee -a ${LOGFILE}
        exit 1
    fi

    partsz=$[${num}*1024]
    return 0
}

do_fsck() {
    if [ "$#" -lt "1" ]; then
        echo "do_fsck(): <out>" |tee -a ${LOGFILE}
        exit 1
    fi
    out=$1
    echo ${FSCK} -fn ${device} >>${LOGFILE}
    echo -n "fsck ..... " |tee -a ${LOGFILE}
    echo ${FSCK} -fn ${device} >>${out}
    ${FSCK} -fn ${device} >>${out} 2>&1
    grep "All passes succeeded" ${LOGFILE} >/dev/null 2>&1
    grep "All passes succeeded" ${out} >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "FAILED. Errors in ${out}" |tee -a ${LOGFILE}
        exit 1
    else
        echo "OK" |tee -a ${LOGFILE}
    fi
    echo "" >> ${out}
    return 0
}

do_mkfs() {
    if [ "$#" -lt "5" ] ; then
        echo "do_mkfs(): blocksize clustersize device volsize out"  |tee -a ${LOGFILE}
        exit 1
    fi

    B=$1
    C=$2
    D=$3
    V=$4
    O=$5

    echo ${MKFS} -b ${B} -C ${C} ${D} ${V} >> ${LOGFILE}
    echo -n "mkfs ..... " |tee -a ${LOGFILE}
    echo ${MKFS} -b ${B} -C ${C} ${D} ${V} >> ${O}
    ${MKFS} -x -F -b ${B} -C ${C} -N 1 -J size=4M ${D} ${V} >> ${O} 2>&1
    echo "OK" |tee -a ${LOGFILE}
    echo "" >> ${O}
}

do_mount() {
	# mount the device on mntdir
	echo -n "mount ${device} ${mntdir} " |tee -a ${LOGFILE}
	${MOUNT} -t ocfs2 ${device} ${mntdir} 2>/dev/null
	if [ $? -ne 0 ]
	then
		echo -n "FAILED. Check dmesg for errors." 2>&1  |tee -a ${LOGFILE}
		exit 1
	else
		echo "OK"  |tee -a ${LOGFILE}
	fi
}

do_umount() {
	# umount the volume
	echo -n "umount ${mntdir} "  |tee -a ${LOGFILE}
	${UMOUNT} ${mntdir} 2>/dev/null
	if [ $? -ne 0 ]
	then
		echo "FAILED. Check dmesg for errors." 2>&1  |tee -a ${LOGFILE}
		exit 1
	else
		echo "OK"  |tee -a ${LOGFILE}
	fi
}

do_consume() {
	file_type=$1

	# create 1M sized files
	fillbsz=1048576

	# find the free space
	freespace=`df --block-size=${fillbsz} ${device} |
		awk -v DEV=${device} 'BEGIN {dev=DEV;} // { if ($1 == dev) print $4; }'`

	if [ $file_type -eq 0 ]
	then
		echo -n "create ${freespace} files " |tee -a ${LOGFILE}
	else
		freespace=$[${freespace}/2]
		echo -n "create 2 large files " |tee -a ${LOGFILE}
	fi

	j=0
	for((i=0;i<$freespace;i++))
	do
		if [ $[$i % 11] -eq 0 ]
		then
			if [ $j -eq 10 ]
			then
				echo -ne "\b\b\b\b\b\b\b\b\b\b          \b\b\b\b\b\b\b\b\b\b" |tee -a ${LOGFILE}
				j=0
			else
				echo "." |tee -a ${LOGFILE}
				j=$[$j+1]
			fi
		fi

		if [ $file_type -eq 0 ]
		then
			dd if=/dev/zero of=${usedir}/file$i bs=${fillbsz} count=1 >/dev/null 2>&1
		else
			dd if=/dev/zero of=${usedir}/test_file1 bs=${fillbsz} count=1 seek=$i >/dev/null 2>&1
			dd if=/dev/zero of=${usedir}/test_file2 bs=${fillbsz} count=1 seek=$i >/dev/null 2>&1
		fi

		if [ $? -ne 0 ]
		then
			i=0
			echo
			break;
		fi
	done
	if [ $i -ne 0 ] ; then echo ; fi

    return 0
}

do_consume_and_delete() {
	file_type=$1

	do_mount

	# add files to fill up (not necessarily full)
	usedir=${mntdir}/`${DATE} +%Y%m%d_%H%M%S`
	echo "create testdir ${usedir}" |tee -a ${LOGFILE}
	${MKDIR} -p ${usedir}

	do_consume $file_type

	do_umount

	do_fsck ${OUT}

	#delete all the files.
	do_mount
	
	${RM} -rf ${usedir}
	echo "delete all test files" |tee -a ${LOGFILE}

	do_umount

	do_fsck ${OUT}
}

do_bitmap_test() {
	# do_consume_and_delete will consume the disks and free them to see whether
	# it is OK. 0 is to create many small files and 1 is used to creat 2 very
	# large files.
	do_consume_and_delete 0
	do_consume_and_delete 1
}

MKFS="`which sudo` -u root `which mkfs.ocfs2`"
FSCK="`which sudo` -u root `which fsck.ocfs2`"
DEBUGFS="`which sudo` -u root `which debugfs.ocfs2`"
MOUNT="`which sudo` -u root `which mount.ocfs2`"
UMOUNT="`which sudo` -u root `which umount`"
MKDIR="`which sudo` -u root `which mkdir`"
RM="`which sudo` -u root `which rm`"
GREP=`which grep`
DATE=`which date`
AWK=`which awk`

MKFS_TEST=`basename $0`

bindir=`basename ${0}`
outdir=`basename ${bindir}`
device=
mntdir=
OPTIND=1
while getopts "d:i:o:m:c" args
do
  case "$args" in
    o) outdir="$OPTARG";;
    d) device="$OPTARG";;
    m) mntdir="$OPTARG";;
  esac
done
LOGFILE=${outdir}/mkfs-test.log
if [ -f ${LOGFILE} ]; then
	mv ${LOGFILE} `dirname ${LOGFILE}`/`date +%F-%H-%M-%S`-`basename ${LOGFILE}`
fi;

if [ -z "${outdir}" ]; then
    echo "invalid output directory: ${outdir}"
    usage ;
fi

if [ ! -b "${device}" ]; then
    echo "invalid device: ${device}" |tee -a ${LOGFILE}
    usage ;
fi

if [ -z "${mntdir}" ]; then
    echo "invalid mount point: ${mntdir}" |tee -a ${LOGFILE}
    usage ;
fi

echo "create logdir ${outdir}" |tee -a ${LOGFILE}
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
	if [ -f ${OUT} ]; then
		rm -f ${OUT};
	fi;

        echo "Test ${testnum}: -b ${blks} -C ${clusts}" |tee -a ${LOGFILE}
        do_mkfs ${blks} ${clusts} ${device} ${numblks} ${OUT}
        verify_sizes ${blks} ${clusts} ${numblks} ${OUT}
        do_fsck ${OUT}
        testnum=$[$testnum+1]
    done
done


### Test option '-T mail'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
	rm -f ${OUT};
fi;
echo "Test ${testnum}: -T mail" |tee -a ${LOGFILE}
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} -x -F -b 4K -C 4K -N 2 -T mail ${device} 262144 >${OUT} 2>&1
echo "OK" |tee -a ${LOGFILE}
echo -n "verify ..... " |tee -a ${LOGFILE}
${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
if [ $num -ne 134217728 ]; then
    echo "ERROR: Journal size too small for type mail" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
else
    echo "OK" |tee -a ${LOGFILE}
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test option '-T datafiles'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
	rm -f ${OUT};
fi;
echo "Test ${testnum}: -T datafiles" |tee -a ${LOGFILE}
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} -x -F -b 4K -C 4K -N 2 -T datafiles ${device} 262144 >${OUT} 2>&1
echo "OK" |tee -a ${LOGFILE}
echo -n "verify ..... " |tee -a ${LOGFILE}
${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
if [ $num -ne 33554432 ]; then
    echo "ERROR: Journal size too small for type datafiles" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
else
    echo "OK" |tee -a ${LOGFILE}
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test option '-J size=64M'
### Test option '-J size=256M'
for jrnlsz in 64 256
do
    TAG=mkfs_test_${testnum}
    OUT=${outdir}/${TAG}.log
    if [ -f ${OUT} ]; then
	rm -f ${OUT};
    fi;
    echo "Test ${testnum}: -J size=${jrnlsz}M" |tee -a ${LOGFILE}
    echo -n "mkfs ..... " |tee -a ${LOGFILE}
    ${MKFS} -x -F -b 4K -C 4K -N 2 -J size=${jrnlsz}M ${device} 262144 >${OUT} 2>&1
    echo "OK" |tee -a ${LOGFILE}
    echo -n "verify ..... " |tee -a ${LOGFILE}
    ${DEBUGFS} -R "ls -l //" ${device} >>${OUT} 2>&1
    num=`${AWK} '/journal:0000/ {print $6;}' ${OUT}`
    inbytes=$[$jrnlsz*1024*1024]
    if [ $num -ne ${inbytes} ]; then
        echo "ERROR: Journal size expected ${inbytes} but found ${num}" >> ${OUT}
        echo "" >> ${OUT}
        echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
    else
        echo "OK" |tee -a ${LOGFILE}
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
    if [ -f ${OUT} ]; then
	rm -f ${OUT};
    fi;
    echo "Test ${testnum}: -N ${slots}" |tee -a ${LOGFILE}
    echo -n "mkfs ..... " |tee -a ${LOGFILE}
    ${MKFS} -x -F -b 4K -C 4K -N ${slots} -J size=4M ${device} 262144 >${OUT} 2>&1
    echo "OK" |tee -a ${LOGFILE}
    echo -n "verify ..... " |tee -a ${LOGFILE}
    ${DEBUGFS} -R "stats" ${device} >>${OUT} 2>&1
    num=`${AWK} '/Max Node Slots:/ {print $4;}' ${OUT}`
    if [ $num -ne ${slots} ]; then
        echo "ERROR: Node slots expected ${slots} but found ${num}" >> ${OUT}
        echo "" >> ${OUT}
        echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
    else
        echo "OK" |tee -a ${LOGFILE}
    fi
    do_fsck ${OUT}
    testnum=$[$testnum+1]
done


### Test option '-L mylabel'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
	rm -f ${OUT};
fi;
echo "Test ${testnum}: -L mylabel" |tee -a ${LOGFILE}
label="my_label_is_very_very_very_long_to_the_point_of_being_useless"
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} -x -F -b 4K -C 4K -N 1 -L ${label} ${device} 262144 >{OUt} 2>&1
echo "OK" |tee -a ${LOGFILE}
echo -n "verify ..... " |tee -a ${LOGFILE}
${DEBUGFS} -R "stats" ${device} >${OUT} 2>&1
dsklab=`${AWK} '/Label:/ {print $2;}' ${OUT}`
if [ ${label} != ${dsklab} ]; then
    echo "ERROR: Label found \"${dsklab}\" expected \"${label}\"" >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
else
    echo "OK" |tee -a ${LOGFILE}
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test option '--fs-features=inline-data'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
        rm -f ${OUT};
fi;
echo "Test ${testnum}: --fs-features=inline-data" |tee -a ${LOGFILE}
label="Oracle_Home"
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} --fs-features=inline-data -x -F -b 4K -C 4K -N 2 -L ${label} ${device} 262144 >>${OUT} 2>&1
echo "OK" |tee -a ${LOGFILE}
echo -n "verify ..... " |tee -a ${LOGFILE}
${DEBUGFS} -R "stats" ${device} >>${OUT} 2>&1
${DEBUGFS} -R "stats" ${device}|${GREP} -i "Feature Incompat"|${GREP} -q "inline-data"
RC=$?
if [ "${RC}" != "0" ]; then
    echo "ERROR: Did not find InlineData Flag on superblock " >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
else
    echo "OK" |tee -a ${LOGFILE}
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test default support for sparse file'
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
        rm -f ${OUT};
fi;
echo "Test ${testnum}: Default option for sparse file support" |tee -a ${LOGFILE}
label="Oracle_Home"
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} -x -F -b 4K -C 4K -N 2 -L ${label} ${device} 262144 >>${OUT} 2>&1
echo "OK" |tee -a ${LOGFILE}
echo -n "verify ..... " |tee -a ${LOGFILE}
${DEBUGFS} -R "stats" ${device} >>${OUT} 2>&1
${DEBUGFS} -R "stats" ${device}|${GREP} -i "Feature Incompat"|${GREP} -q "sparse"
RC=$?
if [ "${RC}" != "0" ]; then
    echo "ERROR: Did not find Sparse Flag on superblock " >> ${OUT}
    echo "" >> ${OUT}
    echo "FAILED. Errors in ${OUT}" |tee -a ${LOGFILE}
else
    echo "OK" |tee -a ${LOGFILE}
fi
do_fsck ${OUT}
testnum=$[$testnum+1]


### Test bitmap_cpg change
TAG=mkfs_test_${testnum}
OUT=${outdir}/${TAG}.log
if [ -f ${OUT} ]; then
	rm -f ${OUT};
fi;
blocksz=4096
clustsz=1048576
group_bitmap_size=$[$[${blocksz}-64]*8]
blkcount=$[${group_bitmap_size}/2*${clustsz}/${blocksz}]
total_block=$[${partsz}/${blocksz}]
if [ $blkcount -gt $total_block ];
then
	blkcount=$total_block
fi
echo "Test ${testnum}: bitmap_cpg change" |tee -a ${LOGFILE}
echo -n "mkfs ..... " |tee -a ${LOGFILE}
${MKFS} -x -F -b ${blocksz} -C ${clustsz} -N 2 ${device} ${blkcount} >${OUT} 2>&1
echo "OK" |tee -a ${LOGFILE}

#consume the whole volume and then delete all the files.
do_bitmap_test

testnum=$[$testnum+1]

### Test --no-backup-super option


### Test option '-M local'


