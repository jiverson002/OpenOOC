#! /bin/bash

CMD="/usr/bin/time build/bin/matmult"
LOG="run.out"

function run() {
  eval "echo $CMD $@ >> $LOG"
  eval "$CMD $@ >> $LOG 2>&1"
}

for l in $1 ; do
  # F0
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f0
  run -l$l -n1024 -m450560 -p3072 -y64   -x450560 -z192  -t4 -f0
  run -l$l -n1024 -m450560 -p3072 -y128  -x225280 -z384  -t4 -f0
  run -l$l -n1024 -m450560 -p3072 -y256  -x112640 -z768  -t4 -f0
  run -l$l -n1024 -m450560 -p3072 -y512  -x56320  -z1536 -t4 -f0
  run -l$l -n1024 -m450560 -p3072 -y1024 -x28160  -z3072 -t4 -f0

  # F1
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f1
  run -l$l -n1024 -m450560 -p3072 -y64   -x450560 -z192  -t4 -f1
  run -l$l -n1024 -m450560 -p3072 -y128  -x225280 -z384  -t4 -f1
  run -l$l -n1024 -m450560 -p3072 -y256  -x112640 -z768  -t4 -f1
  run -l$l -n1024 -m450560 -p3072 -y512  -x56320  -z1536 -t4 -f1
  run -l$l -n1024 -m450560 -p3072 -y1024 -x28160  -z3072 -t4 -f1

  # F16
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f16
  run -l$l -n1024 -m450560 -p3072 -y64   -x450560 -z192  -t4 -f16

  # F32
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f32
  run -l$l -n1024 -m450560 -p3072 -y128  -x225280 -z384  -t4 -f32

  # F64
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f64
  run -l$l -n1024 -m450560 -p3072 -y256  -x112640 -z768  -t4 -f64

  # F128
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f128
  run -l$l -n1024 -m450560 -p3072 -y512  -x56320  -z1536 -t4 -f128

  # F256
  run -l$l -n1024 -m450560 -p3072 -y1    -x1      -z1    -t4 -f256
  run -l$l -n1024 -m450560 -p3072 -y1024 -x28160  -z3072 -t4 -f256
done
