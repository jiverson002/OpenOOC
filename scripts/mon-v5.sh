#!/bin/bash

LOG="mon-v5-$$.out"

if [ $# -ne 2 ] ; then
  echo "usage mon.sh <name> <np>"
  exit 1
fi

# So that script can be started before $1 processes are started
sleep 5

while : ; do
  # check exit condition
  ps -C "$1" > /dev/null || break

  # print [cpu mem] for each relevant process, all on one line
  tm=`date +%s`
  top -d 1.0 -Hbn3 -o -PID | grep $1 | awk -v np=$2 -v tm=$tm \
    '{                                                        \
      if (1 == NR%np)                                         \
        printf "%lu ", tm++;                                  \
      printf "%6.2f %6.2f ", $9, $10;                         \
      if (0 == NR%np)                                         \
        printf "\n";                                          \
    }' >> $LOG
  sleep 1
done
