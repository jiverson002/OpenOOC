#!/bin/bash

CMD="/usr/bin/time build/bin/matmult"
COMMIT=`git rev-parse --short HEAD`
LOG="results/$COMMIT/run.out"

function run() {
  OBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  eval "echo $CMD $@ >> $LOG"
  eval "$CMD $@ >> $LOG 2>&1"
  NBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  printf "  I/O (GB)     = %9.5f\n" `echo "scale=10; ($NBLOCKS - $OBLOCKS) * 512 / 1024^3" | bc` >> $LOG
}

[[ -d results/$COMMIT ]] || mkdir results/$COMMIT

for l in $1 ; do
  for f in 0 1 2 4 8 16 32 64 128 256 440 ; do
    sudo fstrim /scratch-ssd
    run -l$l -n1760 -m65536 -p1760 -y1    -x1     -z1    -t4 -f$f
    #sudo fstrim /scratch-ssd
    #run -l$l -n1760 -m65536 -p1760 -y1760 -x32768 -z1760 -t4 -f$f
  done
done
