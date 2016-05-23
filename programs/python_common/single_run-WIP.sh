#!/bin/bash
#

APP=`basename ${0}`
PATH=$PATH:`dirname ${0}`:/sbin
RUNTIME=300			# seconds

AWK=`which awk`
CAT=`which cat`
CHOWN=`which chown`
CUT=`which cut`
DATE=`which date`
ECHO=`which echo`
HOSTNAME=`which hostname`
MD5SUM=`which md5sum`
MKDIR=`which mkdir`
SEQ=`which seq`
SUDO="`which sudo` -u root"
WGET=`which wget`
WHOAMI=`which whoami`
SED=`which sed`

DWNLD_PATH="https://cdn.kernel.org/pub/linux/kernel/v3.x/"
KERNEL_TARBALL="linux-3.2.80.tar.xz"
#KERNEL_TARBALL_CHECK="${KERNEL_TARBALL}.md5sum"
USERID=`${WHOAMI}`

DEBUGFS_BIN="${SUDO} `which debugfs.ocfs2`"
TUNEFS_BIN="${SUDO} `which tunefs.ocfs2`"
MKFS_BIN="${SUDO} `which mkfs.ocfs2`"
MOUNT="${SUDO} `which mount`"
UMOUNT="${SUDO} `which umount`"

# log_message message
log_message()
{
	${ECHO} "`${DATE}  +\"%F %H:%M:%S\"` $@" >> ${LOGFILE}
}

log_start()
{
	log_message $@
	START=$(date +%s)
}

# log_end $?
log_end()
{
	if [ "$#" -lt "1" ]; then
      		${ECHO} "Error in log_end()"
		exit 1
	fi

	END=$(date +%s)
	DIFF=$(( ${END} - ${START} ))

	if [ ${1} -ne 0 ]; then
		log_message "FAILED (${DIFF} secs)"
	else
		log_message "PASSED (${DIFF} secs)"
	fi

	START=0
}

get_bits()
{
	if [ "$#" -lt "1" ]; then
      		${ECHO} "Error in get_bits()"
		exit 1
	fi

	val=$1

	for i in `${SEQ} 1 31`
	do
		if [ $[2 ** $i] -eq ${val} ]; then
			return $i
		fi
	done

	exit 1
}

# get_kernel_source $LOGDIR $DWNLD_PATH $KERNEL_TARBALL $KERNEL_TARBALL_CHECK
get_kernel_source()
{
	if [ "$#" -lt "3" ]; then
		${ECHO} "Error in get_kernel_source()"
		exit 1
	fi

	logdir=$1
	dwnld_path=$2
	kernel_tarball=$3
	#kernel_tarball_check=$4

	cd ${logdir}

	outlog=get_kernel_source.log

#	${WGET} -o ${outlog} ${dwnld_path}/${kernel_tarball_check}
#	if [ $? -ne 0 ]; then
#		${ECHO} "ERROR downloading ${dwnld_path}/${kernel_tarball_check}"
#		cd -
#		exit 1
#	fi

	${WGET} -a ${outlog} ${dwnld_path}/${kernel_tarball}
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR downloading ${dwnld_path}/${kernel_tarball}"
		cd -
		exit 1
	fi

#	${MD5SUM} -c ${kernel_tarball_check} >>${outlog} 2>&1
#	if [ $? -ne 0 ]; then
#		${ECHO} "ERROR ${kernel_tarball_check} check failed"
#		cd -
#		exit 1
#	fi
#	cd -
}

# do_format() ${BLOCKSIZE} ${CLUSTERSIZE} ${FEATURES} ${DEVICE}
do_format() {
	if [ "$#" -lt "4" ]; then
      		${ECHO} "Error in do_format() $@"
		exit 1
	fi

	blocksize=$1
	clustersize=$2
	features=$3
	device=$4

	${MKFS_BIN} -x -b ${blocksize} -C ${clustersize} --fs-features=${features} \
		-N 1 -L single_run -M local ${device} >/dev/null
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: mkfs.ocfs2 -b ${blocksize} -C ${clustersize} " \
			"--fs-features=${features} -N 1 -L single_run -M local ${device}"
		exit 1
	fi
}

# do_mount ${DEVICE} ${MOUNTPOINT} ${MOUNTOPTS}
do_mount() {
	if [ "$#" -lt "3" ]; then
      		${ECHO} "Error in do_mount()"
		exit 1
	fi

	device=$1
	mountpoint=$2
	mountopts=$3	

	${MOUNT} -o ${mountopts} ${device} ${mountpoint} >/dev/null
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: mount -o ${mountopts} ${device} ${mountpoint}"
		exit 1
	fi
	${SUDO} ${CHOWN} -R ${USERID} ${mountpoint}
}

# do_umount ${MOUNTPOINT}
do_umount() {
	if [ "$#" -lt "1" ]; then
		${ECHO} "Error in do_umount()"
		exit 1
	fi

	mountpoint=$1

	${UMOUNT} ${mountpoint}
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: umount ${mountpoint}"
		exit 1
	fi
}

# do_mkdir DIR
do_mkdir() {
	if [ "$#" -lt "1" ]; then
		${ECHO} "Error in do_mkdir()"
		exit 1
	fi

	${SUDO} ${MKDIR} -p $1
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: mkdir $1"
		exit 1
	fi

	${SUDO} ${CHOWN} -R ${USERID} $1
}

run_create_and_open()
{
	log_message "run_create_and_open" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_create_and_open()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workdir=${mountpoint}/create_and_open_test
	blocksize=4096
	clustersize=32768
	features="sparse,unwritten,inline-data"

	mountopts="defaults"

	log_start "create_and_open"

	do_format ${blocksize} ${clustersize} ${features} ${device}
	do_mount ${device} ${mountpoint} ${mountopts}
	do_mkdir ${workdir}

	outlog=${logdir}/create_and_open.log

	create_and_open ${workdir} >${outlog} 2>&1
	RC=$?

	do_umount ${mountpoint}

	log_end ${RC}
}

run_extend_and_write()
{
	log_message "run_extend_and_write" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_extend_and_write()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workfile=${mountpoint}/extend_and_write_testfile
	blocksize=4096
	clustersize=32768
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered;do

		mountopts="data=${mopt}"

		log_start "extend_and_write_test" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}

		outlog=${logdir}/extend_and_write.log

		run_extend_and_write.py -f ${workfile} -l ${outlog} -n 1000 -s 4096
		RC=$?

		do_umount ${mountpoint}

		log_end ${RC}
	done
}

run_directaio()
{
	log_message "run_directaio" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_directaio()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workfile=${mountpoint}/directaio_testfile
	blocksize=4096
	clustersize=32768
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		log_start "direct-aio" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}

		outlog=${logdir}/directaio_${mopt}.log

		partial_aio_direct ${workfile} >${outlog} 2>&1
		RC=$?

		do_umount ${mountpoint}
	
		log_end ${RC}
	done
}

# run_aiostress ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
run_aiostress()
{
	log_message "run_aiostress" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_aiostress()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workdir=${mountpoint}/testme
	blocksize=4096
	clustersize=8192
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		log_start "aio-stress" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}
		do_mkdir ${workdir}

		F1="${workdir}/aiostress1.dat"
		F2="${workdir}/aiostress2.dat"
		F3="${workdir}/aiostress3.dat"
		F4="${workdir}/aiostress4.dat"

		outlog=${logdir}/aiostress_${mopt}.log

		aio-stress -a 4k -b 32 -i 16 -O -l -L -t 8 -v ${F1} ${F2} ${F3} ${F4} >${outlog} 2>&1
		RC=$?

		do_umount ${mountpoint}
	
		log_end ${RC}
	done
}

# run_buildkernel ${LOGDIR} ${DEVICE} {MOUNTPOINT} ${KERNELSRC}
run_buildkernel()
{
	log_message "run_buildkernel" $@
        if [ "$#" -lt "4" ]; then
                echo "Error in run_buildkernel()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3
	kernelsrc=$4

	node=`${HOSTNAME}`
	workdir=${mountpoint}/testme
	blocksize=4096
	clustersize=4096
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}
		do_mkdir ${workdir}

		log_start "buildkernel" ${mountopts}

		outlog=${logdir}/buildkernel_${mopt}.log

		buildkernel.py -e -d ${workdir} -t ${kernelsrc} -n ${node} -l ${outlog}.1
		RC=$?
		if [ ${RC} -eq 0 ]; then
			buildkernel.py -d ${workdir} -t ${kernelsrc} -n ${node} -l ${outlog}.2
			RC=$?
		fi

		do_umount ${mountpoint}

		log_end ${RC}
	done
}

# run_filesizelimits ${LOGDIR} ${DEVICE} {MOUNTPOINT}
run_filesizelimits()
{
	log_message "run_filesizelimits" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_filesizelimits()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	mountopts=defaults
	clustersize=4096
	blocksize=4096

	get_bits $blocksize
	blocksize_bits=$?

	get_bits $clustersize
	clustersize_bits=$?

	workdir=${mountpoint}/testme

	if [ `uname -m` == "i686" ]; then
		bitsperlong=32
	else
		bitsperlong=64
	fi

	log_start "check_file_size_limits"

	do_format ${blocksize} ${clustersize} sparse,unwritten,inline-data ${device}
	do_mount ${device} ${mountpoint} ${mountopts}
	do_mkdir ${workdir}

	file="${workdir}/filesizelimits.dat"
	outlog=${logdir}/filesizelimits.log

	check_file_size_limits -B ${bitsperlong} -b ${blocksize_bits} \
		-c ${clustersize_bits} ${file} >${outlog} 2>&1
	RC=$?

	do_umount ${mountpoint}

	log_end ${RC}
}

# run_fillverifyholes ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
run_fillverifyholes()
{
	log_message "run_fillverifyholes" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_fillverifyholes()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workdir=${mountpoint}/testme

	varfile=${logdir}/fillverifyholes.txt

	${CAT} > ${varfile} <<EOF
2048	4096	nosparse,nounwritten,noinline-data	data=ordered	100000000	0	0
2048	65536	sparse,unwritten,inline-data		data=writeback	2000000000	-M	-U
4096	4096	sparse,unwritten,inline-data		data=ordered	5000000000	0	0
4096	4096	sparse,unwritten,inline-data		data=ordered	2000000000	-M	0
4096	4096	sparse,unwritten,inline-data		data=ordered	5000000000	0	-U
4096	4096	sparse,unwritten,inline-data		data=ordered	2000000000	-M	-U
4096	8192	nosparse,nounwritten,noinline-data	data=writeback	100000000	0	0
4096	131072	nosparse,nounwritten,noinline-data	data=ordered	100000000	-M	0
4096	1048576	sparse,unwritten,inline-data		data=writeback	2000000000	-M	-U
EOF
	if [ $? != 0 ]; then
		${ECHO} "ERROR writing ${varfile}"
		exit 1
	fi

	i=0
	${CAT} ${varfile} | while read LINE
	do
        	blocksize=`echo ${LINE} | cut -f1 -d' '`
        	clustersize=`echo ${LINE} | cut -f2 -d' '`
        	features=`echo ${LINE} | cut -f3 -d' '`
		mountopts=`echo ${LINE} | cut -f4 -d' '`
		filesize=`echo ${LINE} | cut -f5 -d' '`
		mmap=`echo ${LINE} | cut -f6 -d' '`
		punchholes=`echo ${LINE} | cut -f7 -d' '`

		log_start "fill_verify_holes" ${blocksize} ${clustersize} ${features} ${mountopts} ${filesize} ${mmap} ${punchholes}

		if [ ${mmap} = 0 ] ; then
			mmap=
		fi

		if [ ${punchholes} = 0 ] ; then
			punchholes=
		fi

		outlog=${logdir}/fillverifyholes_${i}.log
		ldir=${logdir}/fillverifyholes_${i}
		do_mkdir ${ldir}

		do_format ${blocksize} ${clustersize} ${features} ${device}

		fill_verify_holes.sh -i 100 -s ${filesize} -c 1 -m ${mountpoint} -l ${ldir} \
			-d ${device} -o ${mountopts} ${mmap} ${punchholes} >${outlog} 2>&1
		RC=$?

		log_end ${RC}

		i=$[$i+1]
	done
}

# run_mmaptruncate ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
run_mmaptruncate()
{
	log_message "run_mmaptruncate" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_mmaptruncate()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	runtime=300
	workfile=${mountpoint}/mmaptruncate.txt
	varfile=${logdir}/mmaptruncate.conf

	${CAT} > ${varfile} <<EOF
2048	4096	nosparse,nounwritten,noinline-data	data=ordered
2048	65536	sparse,unwritten,inline-data		data=writeback
4096	4096	sparse,unwritten,inline-data		data=ordered
4096	8192	nosparse,nounwritten,noinline-data	data=writeback
4096	131072	sparse,unwritten,inline-data		data=ordered
4096	1048576	sparse,unwritten,inline-data		data=writeback
EOF
	if [ $? != 0 ]; then
		${ECHO} "ERROR writing ${varfile}"
		exit 1
	fi

	${CAT} ${varfile} | while read LINE
	do
        	blocksize=`echo ${LINE} | cut -f1 -d' '`
        	clustersize=`echo ${LINE} | cut -f2 -d' '`
        	features=`echo ${LINE} | cut -f3 -d' '`
		mountopts=`echo ${LINE} | cut -f4 -d' '`

		get_bits $clustersize
		clustersize_bits=$?

		log_start "mmap_truncate" ${blocksize} ${clustersize} ${features} ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}

		mmap_truncate -c ${clustersize_bits} -s ${runtime} ${workfile}
		RC=$?

		sleep 10
		do_umount ${mountpoint}

		log_end ${RC}
	done
}

# run_renamewriterace ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
run_renamewriterace()
{
	log_message "run_renamewriterace" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_renamewriterace()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workdir=${mountpoint}/testme
	blocksize=4096
	clustersize=4096
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		log_start "rename_write_race" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}
		do_mkdir ${workdir}

                outlog=${logdir}/renamewriterace_${mopt}.log

		rename_write_race.sh -d ${workdir} -i 10000 >${outlog} 2>&1
		RC=$?

		do_umount ${mountpoint}

		log_end ${RC}
	done
}

run_splice()
{
	log_message "run_splice" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_splice()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workdir=${mountpoint}/testme
	blocksize=4096
	clustersize=32768
	features="sparse,unwritten,inline-data"

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		log_start "splice" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}
		do_mkdir ${workdir}

                outlog=${logdir}/splice_${mopt}.log

		splice_test.py -d ${workdir} -c 1000 -f splice_test.dat >${outlog} 2>&1
		RC=$?

		do_umount ${mountpoint}

		log_end ${RC}
	done
}

run_sendfile()
{
	log_message "run_sendfile" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_sendfile()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workfile=${mountpoint}/ocfs2_sendfile_data
	verifyfile=/tmp/sendfile_verify
	
	blocksize=4096
	clustersize=32768
	features="sparse,unwritten,inline-data"
	port=8001

	for mopt in writeback ordered
	do
		mountopts="data=${mopt}"

		log_start "sendfile" ${mountopts}

		do_format ${blocksize} ${clustersize} ${features} ${device}
		do_mount ${device} ${mountpoint} ${mountopts}

		# Generate original date file
		dd if=/dev/random of=${workfile} bs=${clustersize} count=4
                outlog=${logdir}/sendfile_${mopt}.log

		nc -l ${port} >${verifyfile} &
		sendfiletest ${workfile} `hostname` >${outlog} 2>&1
		RC=$?

		md51=`md5sum ${workfile}|cut -d' ' -f1`
		md52=`md5sum ${verifyfile}|cut -d' ' -f1`

		if [ "${md51}" != "${md52}" ];then
			log_message "md5sum verification failed."
			RC=1
		fi

		do_umount ${mountpoint}

		log_end ${RC}
	done
}

run_mmap()
{
	log_message "run_mmap" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_mmap()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workfile=${mountpoint}/mmap_testfile
	features="sparse,unwritten,inline-data"

	for blocksize in 512 1024 2048 4096;do
		for clustersize in 4096 32768 1048576;do
			for mopt in writeback ordered;do
				mountopts="data=${mopt}"

				log_start "mmap" ${blocksize} ${clustersize} ${features} ${mountopts}

				do_format ${blocksize} ${clustersize} ${features} ${device}
				do_mount ${device} ${mountpoint} ${mountopts}

				outlog=${logdir}/mmap_${mopt}_${blocksize}_${clustersize}.log

				dd if=/dev/zero of=${workfile} bs=1M count=1024
				mmap_test ${workfile} >${outlog} 2>&1
				RC=$?

				do_umount ${mountpoint}

				log_end ${RC}
			done
		done
	done
}

run_reserve_space()
{
	log_message "run_reserve_space" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_reserve_space()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	workfile=${mountpoint}/reserve_space_testfile
	features="sparse,unwritten,inline-data"
	space_free=
	iter=1000

	for blocksize in 512 1024 2048 4096;do
		for clustersize in 4096 32768 1048576;do
			for mopt in writeback ordered;do
				mountopts="data=${mopt}"

				log_start "reserve_space" ${blocksize} ${clustersize} ${features} ${mountopts}

				do_format ${blocksize} ${clustersize} ${features} ${device}
				do_mount ${device} ${mountpoint} ${mountopts}

				outlog=${logdir}/reserve_space_${mopt}_${blocksize}_${clustersize}.log
				space_free=`df|grep ${device}|awk '{print $4}'`
				space_free=$((${space_free}*1024))

				for i in `seq ${iter}`;do
					r_whence=0
					r_start=$((${RANDOM}%${space_free}))
					r_len=$((${RANDOM}%(${space_free}-${r_start})))

					reserve_space ${workfile} resv ${r_whence} ${r_start} ${r_len} >>${outlog} 2>&1 || {
						RC=$?
						break
					}
				
					reserve_space ${workfile} unresv ${r_whence} ${r_start} ${r_len} >>${outlog} 2>&1 || {
						RC=$?
						break
					}
				done

				do_umount ${mountpoint}

				log_end ${RC}
			done
		done
	done
}

run_inline_data()
{
	log_message "run_inline_data" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_inline_data()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	log_start "inline_data_test" 
	single-inline-run.sh  -o ${logdir} -d ${device} ${mountpoint}
	RC=$?
	log_end ${RC}
}

run_dx_dir()
{
	log_message "run_dx_dir" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_dx_dir()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3
        kernelsrc=$4

	log_start "index_dir_test" 
	index_dir_run.sh  -o ${logdir} -d ${device} -t ${kernelsrc} ${mountpoint}
	RC=$?
	log_end ${RC}
}

run_xattr_test()
{
	log_message "run_xattr_test" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_xattr_test()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	log_start "xattr_test" 
	xattr-single-run.sh -c -o ${logdir} -d ${device} ${mountpoint}
	RC=$?

	log_end ${RC}
}

run_reflink_test()
{
	log_message "run_reflink_test" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_reflink()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	#ordered mount option
	log_start "reflink_test" "ordered"
	./reflink_test_run.sh -o ${logdir} -d ${device} ${mountpoint} || {
		RC=$?
		log_end ${RC}
	}

	#writeback mount option
	#log_start "reflink_test" "writeback"
	#./reflink_test_run.sh -W -o ${logdir} -d ${device} ${mountpoint}
	RC=$?
	log_end ${RC}
}

run_filecheck_test()
{
	log_message "run filecheck_test" $@
        if [ "$#" -lt "3" ]; then
                echo "Error in run_filecheck()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	log_start "filecheck_test"
	filecheck_test_run.sh -o ${logdir} -d ${device} ${mountpoint}
	RC=$?

	log_end ${RC}
}


# Following cases aim to test tools
run_mkfs()
{
	log_message "run_mkfs" $@
        if [ "$#" -lt "2" ]; then
                echo "Error in run_mkfs()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	log_start "mkfs_test"
	mkfs-test.sh -o ${logdir} -d ${device} -m ${mountpoint}
	RC=$?
	log_end ${RC}
}

run_tunefs()
{
	log_message "run_tunefs" $@
        if [ "$#" -lt "2" ]; then
                echo "Error in run_tunefs()"
                exit 1
        fi

	logdir=$1
	device=$2
	mountpoint=$3

	log_start "tunefs_test"
	tunefs-test.sh -o ${logdir} -d ${device} -m ${mountpoint}
	RC=$?
	log_end ${RC}
}

run_backup_super()
{
	log_message "run_backup_super" $@
        if [ "$#" -lt "2" ]; then
                echo "Error in run_backup_super()"
                exit 1
        fi

	logdir=$1
	device=$2

	log_start "backup_super_test"
	test_backup_super.sh --log-dir=${logdir} ${device}
	RC=$?
	log_end ${RC}
}

#
#
# MAIN
#
#

usage()
{
	${ECHO} "usage: ${APP} [-k kerneltarball] -m mountpoint -l logdir -d device [-t testcases]"
	exit 1
}

while getopts "d:m:k:l:t:h:?" args
do
	case "$args" in
		d) DEVICE="$OPTARG";;
		m) MOUNTPOINT="$OPTARG";;
		k) KERNELSRC="$OPTARG";;
		l) OUTDIR="$OPTARG";;
		t) TESTCASES="$OPTARG";;
    		h) usage;;
    		?) usage;;
  	esac
done

if [ -z ${DEVICE} ] ; then
	${ECHO} "ERROR: No device"
	usage
elif [ ! -b ${DEVICE} ] ; then
	${ECHO} "ERROR: Invalid device ${DEVICE}"
	exit 1
fi

if [ -z ${MOUNTPOINT} ] ; then
	${ECHO} "ERROR: No mountpoint"
	usage
elif [ ! -d ${MOUNTPOINT} ] ; then
	${ECHO} "ERROR: Invalid mountpoint ${MOUNTPOINT}"
	exit 1
fi

if [ -z ${OUTDIR} ]; then
	${ECHO} "ERROR: No logdir"
	usage
fi

if [ -z ${TESTCASES} ]; then
	TESTCASES="all"
fi

SUPPORTED_TESTCASES="all create_and_open directaio fillverifyholes renamewriterace aiostress\
  filesizelimits mmaptruncate buildkernel splice sendfile mmap reserve_space inline xattr\
  reflink mkfs tunefs backup_super filecheck"
for cas in `${ECHO} ${TESTCASES} | ${SED} "s:,: :g"`; do
	echo ${SUPPORTED_TESTCASES} | grep -sqw $cas
	if [ $? -ne 0 ]; then
		echo "testcase [${cas}] not supported."
		echo "supported testcases: [${SUPPORTED_TESTCASES}]"
		exit 1
	fi
done

RUNDATE=`${DATE} +%F_%H:%M`
LOGDIR=${OUTDIR}/${RUNDATE}
LOGFILE=${LOGDIR}/single_run.log

do_mkdir ${LOGDIR}

STARTRUN=$(date +%s)
log_message "*** Start Single Node test ***"

${ECHO} "Output log is ${LOGFILE}"

for tc in `${ECHO} ${TESTCASES} | ${SED} "s:,: :g"`; do

	if [ "$tc"X = "create_and_open"X -o "$tc"X = "all"X ];then
		run_create_and_open ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "directaio"X -o "$tc"X = "all"X ];then
		run_directaio ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "fillverifyholes"X -o "$tc"X = "all"X ];then
		run_fillverifyholes ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "renamewriterace"X -o "$tc"X = "all"X ];then
		run_renamewriterace ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "aiostress"X -o "$tc"X = "all"X ];then
		run_aiostress ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "filesizelimits"X -o "$tc"X = "all"X ];then
		run_filesizelimits ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "mmaptruncate"X -o "$tc"X = "all"X ];then
		run_mmaptruncate ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "buildkernel"X -o "$tc"X = "all"X ];then
		if [ -z ${KERNELSRC} ]; then
			get_kernel_source $LOGDIR $DWNLD_PATH $KERNEL_TARBALL #$KERNEL_TARBALL_CHECK
			KERNELSRC=${LOGDIR}/${KERNEL_TARBALL}
		fi

		if [ ! -f ${KERNELSRC} ]; then
			${ECHO} "No kernel source"
			usage
		fi

		run_buildkernel ${LOGDIR} ${DEVICE} ${MOUNTPOINT} ${KERNELSRC}
	fi

	if [ "$tc"X = "splice"X -o "$tc"X = "all"X ];then
		run_splice ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "sendfile"X -o "$tc"X = "all"X ];then
		run_sendfile ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "mmap"X -o "$tc"X = "all"X ];then
		run_mmap ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "reserve_space"X -o "$tc"X = "all"X ];then
		run_reserve_space ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "inline"X -o "$tc"X = "all"X ];then
		run_inline_data ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "xattr"X -o "$tc"X = "all"X ];then
		run_xattr_test ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "reflink"X -o "$tc"X = "all"X ];then
		run_reflink_test ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "filecheck"X ];then
		run_filecheck_test ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

# For tools test.

	if [ "$tc"X = "mkfs"X -o "$tc"X = "all"X ];then
		run_mkfs ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "tunefs"X -o "$tc"X = "all"X ];then
		run_tunefs ${LOGDIR} ${DEVICE} ${MOUNTPOINT}
	fi

	if [ "$tc"X = "backup_super"X -o "$tc"X = "all"X ];then
		run_backup_super ${LOGDIR} ${DEVICE}
	fi

done

ENDRUN=$(date +%s)

DIFF=$(( ${ENDRUN} - ${STARTRUN} ))
log_message "Total Runtime ${DIFF} seconds"
log_message "*** End Single Node test ***"
