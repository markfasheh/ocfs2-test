#!/bin/bash

BINPATH="."
LOGPATH="."
MMAPOPT=

log_run() {
    echo "Run: $@"
    "$@"
}

run_fill() {
    iter="$1";
    filename="$2";
    size="$3";
    logfile="$4"

    log_run "$BINPATH/fill_holes" $MMAPOPT -f -o "$logfile" -i "$iter" "$filename" "$size"
    log_run "$BINPATH/verify_holes" "$logfile" "$filename"

    RET=$?
    if [ $RET -ne 0 ]; then
	exit 1;
    fi;
}

run100() {
    iter="$1"
    size="$2"

    fnamebase="iter$iter.size$size"

    for i in `seq -w 0 99`
    do
      f="$fnamebase.$i.txt"
      l="$LOGPATH/$fnamebase.$i.log"
      run_fill "$iter" "$f" "$size" "$l"
    done
}

USAGE=""
OPTIND=1
while getopts "mb:l:h?" args
do
  case "$args" in
    m) MMAPOPT="-m";;
    b) BINPATH="$OPTARG";;
    l) LOGPATH="$OPTARG";;
    h) USAGE="yes";;
    ?) USAGE="yes";;
  esac
done

if [ -n "$USAGE" ]; then
    echo "usage: burn-in.sh [ -b path-to-binaries ] [ -l path-for-logfiles ]";
    exit 0;
fi

run100 100 1000000
run100 1000 1000000
run100 1000 10000000
run100 10000 10000000
