#! /bin/bash

LOG=run.log

HLINE="\n================================================================================\n"
hline="\n--------------------------------------------------------------------------------\n"

function run() {
  echo "$@" >> $LOG
  $@ >> $LOG
}

run build/bin/mm -n1024 -m225280 -p1024                       -t4
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y64  -x225280 -z256  -t4
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y128 -x112640 -z512  -t4
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y256 -x56320  -z1024 -t4

echo -e "$HLINE" >> $LOG

run build/bin/mm -n1024 -m225280 -p1024                       -t4 -f1
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y64  -x225280 -z256  -t4 -f1
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y128 -x112640 -z512  -t4 -f1
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y256 -x56320  -z1024 -t4 -f1

echo -e "$HLINE" >> $LOG

run build/bin/mm -n1024 -m225280 -p1024                       -t4 -f16
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y64  -x225280 -z256  -t4 -f16
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y128 -x112640 -z512  -t4 -f16
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y256 -x56320  -z1024 -t4 -f16

echo -e "$HLINE" >> $LOG

run build/bin/mm -n1024 -m225280 -p1024                       -t4 -f32
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y64  -x225280 -z256  -t4 -f32
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y128 -x112640 -z512  -t4 -f32
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y256 -x56320  -z1024 -t4 -f32

echo -e "$HLINE" >> $LOG

run build/bin/mm -n1024 -m225280 -p1024                       -t4 -f64
echo -e "$hline" >> $LOG
run build/bin/mm -n1024 -m225280 -p1024 -y64  -x225280 -z256  -t4 -f64
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y128 -x112640 -z512  -t4 -f64
echo -e "$hline" >> $LOG                                      
run build/bin/mm -n1024 -m225280 -p1024 -y256 -x56320  -z1024 -t4 -f64
