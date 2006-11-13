#!/bin/bash

#TRUNC=/home/mfasheh/src/ocfs2_utils/truncate64
TRUNC=./truncate_direct

file1=$1
file2=$2
extend=$3
num=$4

if test -z "$file1" -o -z "$file2" -o -z "$extend" -o -z "$num"; then
    echo "$0 file1 file2 extend_amount numtimes"
    echo "u r a tool"
    exit 1
fi

size1=`stat $file1 -c %s`
size2=`stat $file2 -c %s`

echo "extend $file1 and $file2 by $extend bytes $num times"
#echo "$size1, $size2"

i=0;
while [ $i -le $num ]
do
  size1=$[$size1 + $extend]
  $TRUNC $file1 $size1

  size2=$[$size2 + $extend]
  $TRUNC $file2 $size2

  ((i=i + 1));
done

#for i in `seq 1 $num`
#do
#  size1=$[$size1 + $extend]
#  size2=$[$size2 + $extend]
#  $TRUNC $file1 $size1
#  $TRUNC $file2 $size2
#done
