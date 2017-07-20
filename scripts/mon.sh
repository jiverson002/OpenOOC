#! /bin/bash

# So that script can be started before processes are started
sleep 5

while : ; do
  # check exit condition
  ps -C "$2" > /dev/null || break

  # print [cpu mem] for each relevant process, all on one line
  top -d 1.0 -Hbn1 -o -PID | grep $2 | awk -v tm=`date +%s` \
    'BEGIN {                          \
      printf "%lu ", tm;              \
    }                                 \
    {                                 \
      printf "%6.2f %6.2f ", $5, $6;  \
    }                                 \
    END {                             \
      printf "\n";                    \
    }' >> $1
  sleep 1
done
