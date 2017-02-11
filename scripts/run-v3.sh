#!/bin/bash

CMD="/usr/bin/time build/bin/matmult"
LOG="run-v3.out"

function run() {
  eval "echo $CMD $@ >> $LOG"
  eval "$CMD $@ >> $LOG 2>&1"
}

for l in $1 ; do
  for f in 0 1 2 4 8 16 32 64 128 256 440 ; do
    run -l$l -n1760 -m262144 -p1760 -y1    -x1     -z1    -t4 -f$f
    run -l$l -n1760 -m262144 -p1760 -y1760 -x32768 -z1760 -t4 -f$f
  done
done
