#! /bin/bash

LOG=run-bd4.log

HLINE="\n================================================================================\n"
hline="\n--------------------------------------------------------------------------------\n"

function run() {
  echo "$@" >> $LOG
  $@ >> $LOG 2>&1
}

run build/bin/mm -n128 -p24576 -t4
echo -e "$hline" >> $LOG
run build/bin/mm -n256 -p24576 -t4
echo -e "$hline" >> $LOG
run build/bin/mm -n512 -p24576 -t4
