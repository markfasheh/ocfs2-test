#!/bin/bash

TARFILE=$1
NUM=$2
OUT=`pwd`/results.txt

usage() {
    echo "$0 tarfile numtimes"
    echo "tarfile needs to be an absolute path"
    exit 1;
}

if [ -z "$TARFILE" -o -z "$NUM" ]; then
    usage
fi

if [ ! -r "$TARFILE" ]; then
    echo "\"$TARFILE\" does not exist."
fi

if [ -w $OUT ]; then
    echo -e "\n\n----\n" >> $OUT
fi

echo "TARFILE \"$TARFILE\", untar $NUM times. output to $OUT"
echo "TARFILE \"$TARFILE\", untar $NUM times" >> $OUT

md5sum "$TARFILE" >> $OUT

for i in `seq 1 $NUM`;
do
  mkdir -p $i
  cd $i
  echo "Start Run $i"
  echo "Run $i:" >> $OUT
  /usr/bin/time -pao $OUT tar -zxf $TARFILE
  /usr/bin/time -pao $OUT sync
  echo >> $OUT
  cd ..
done
