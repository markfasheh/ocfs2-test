#!/bin/bash

file=$1

echo "Write to file $file"

i=0
while [ 1 ]
do
	echo $i >> $file;
	i=`expr $i + 1`;
done
