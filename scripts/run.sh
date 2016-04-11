#! /bin/bash

LOG=run.log

HLINE="\n================================================================================\n"
hline="\n--------------------------------------------------------------------------------\n"

function run() {
  echo "$@" >> $LOG
  /usr/bin/time -ao $LOG $@ >> $LOG
}

function build() {
  run build/bin/mm -n1024 -m450560 -p3072                        -t4 $1
  echo -e "$hline" >> $LOG
  run build/bin/mm -n1024 -m450560 -p3072 -y64   -x450560 -z192  -t4 $1
  echo -e "$hline" >> $LOG
  run build/bin/mm -n1024 -m450560 -p3072 -y128  -x225280 -z384  -t4 $1
  echo -e "$hline" >> $LOG
  run build/bin/mm -n1024 -m450560 -p3072 -y256  -x112640 -z768  -t4 $1
  echo -e "$hline" >> $LOG
  run build/bin/mm -n1024 -m450560 -p3072 -y512  -x56320  -z1536 -t4 $1
  echo -e "$hline" >> $LOG
  run build/bin/mm -n1024 -m450560 -p3072 -y1024 -x28160  -z3072 -t4 $1
  echo -e "$HLINE" >> $LOG
}

build -f0
build -f1
build -f16
build -f32
build -f64
build -f128
build -f256
