#!/bin/bash
#
#
# kernel_build_parallel_find.sh -n node1,node2,node3
#	-k kernelversion -m /ocfs2/mnt/path
# 

usage() {
	echo "usage: kernel_build_parallel_find.sh -n node1,node2,node3 \
-k linux-2.6.15.6 -m /ocfs2"
	exit 1;
}

OPTIND=1
while getopts "n:k:m:" args
do
  case "$args" in
    n) hosts="$(echo $OPTARG | sed 's/,/ /g')";;
    k) kernversion="$OPTARG";;
    m) ocfs2="$OPTARG";;
  esac
done

if [ -z "$hosts" ] ;
then
  usage ;
fi

if [ -z "$kernversion" ] ;
then
  usage ;
fi

if [ -z "$ocfs2" ] ;
then
  usage ;
fi

hostname=`hostname -s`
tarfile="$ocfs2/$kernversion.tar.gz"
logfile="$ocfs2/build.log"

if [ ! -f "$tarfile" ] ;
then
  echo "\"$tarfile\" not found"
  exit 1;
fi

run_finds() {
    for i in ${hosts}
    do
      if [ ${hostname} != ${i} ]; then
	  echo "run background find for host $i"
	  echo "find $ocfs2/$i -ls >> ${logfile} &"
	  find "$ocfs2/$i" -ls >> ${logfile} &
      fi
    done
}

found=0
for i in ${hosts}
do
  if [ ${hostname} = ${i} ]; then
      found=1;
  fi
done

if [ ${found} = "0" ]; then
    echo "My hostname \"$hostname\" was not found in hosts list.";
    exit 1;
fi

mydir="$ocfs2/$hostname"

if [ ! -d ${mydir} ]; then
    echo "Directory $mydir does not exist, so creating working directory."
    mkdir $mydir
    cd $mydir
    echo "Untarring \"$tarfile\"..."
    tar -zxf $tarfile
    cd ${kernversion}
    echo "make defconfig"
    make defconfig
fi

cd "$mydir/$kernversion"

echo "make clean"
make clean

echo "finds...."
run_finds

/usr/bin/time -p -o /tmp/buildtime.log make -j4 V=1 2>&1 >> ${logfile};
echo -e "Build time:" >> ${logfile};
cat /tmp/buildtime.log >> ${logfile};
cd  ${WORKDIR};
echo -e "Build test ended on ${hostname} - `date`">> ${logfile};

exit 0
