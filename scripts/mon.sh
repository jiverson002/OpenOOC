#! /bin/bash

# So that script can be started before $1 processes are started
sleep 5

while : ; do
  # check exit condition
  ps -C "$2" > /dev/null || break

  # print [cpu mem] for each relevant process, all on one line
  tm=`date +%s`
  top -d 1.0 -Hbn3 -o -PID | grep $1 | awk -v np=$3 -v tm=$tm \
    '{                                                        \
      if (1 == NR%np)                                         \
        printf "%lu ", tm++;                                  \
      printf "%6.2f %6.2f ", $9, $10;                         \
      if (0 == NR%np)                                         \
        printf "\n";                                          \
    }' >> $1
  sleep 1
done
