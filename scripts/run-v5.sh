#!/bin/bash

CMD="/usr/bin/time build/bin/matmult"
LOG="run-v5-$$.out"

function run() {
  OBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  eval "echo $CMD $@ >> $LOG"
  eval "$CMD $@ >> $LOG 2>&1"
  # TODO Start monitor here
  # ./mon.sh matmult 4 &
  NBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  printf "  I/O (GB)     = %9.5f\n" `echo "scale=10; ($NBLOCKS - $OBLOCKS) * 512 / 1024^3" | bc` >> $LOG
}

for l in $1 ; do
  for f in 0 1 2 4 8 16 32 64 128 256 440 ; do
    run -l$l -n1760 -m262144 -p1760 -y1    -x1     -z1    -t4 -f$f
    run -l$l -n1760 -m262144 -p1760 -y1760 -x32768 -z1760 -t4 -f$f
  done
done