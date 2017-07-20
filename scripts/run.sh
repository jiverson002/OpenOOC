#! /bin/bash

#EXE="floyd"
#ARG="-n23040 -x1"
#ARG="-n23040 -x6144"

EXE="matmult"
#ARG="-n1760 -m65536  -p1760 -y1    -x1     -z1"
#ARG="-n1760 -m65536  -p1760 -y1760 -x32768 -z1760"
ARG="-n1760 -m262144 -p1760 -y1760 -x32768 -z1760"

# bin directory
BIN="build/bin"

# results directory
RES="results"

# sub-directory in $RES for this git commit
COMMIT=`git rev-parse --short HEAD`
[[ -d $RES/$COMMIT ]] || mkdir $RES/$COMMIT

# unique log file
TMP="`mktemp -u $RES/$COMMIT/XXX`"
LOG="$TMP-run.out"
MON="$TMP-mon.out"

# command to be executed, not including -l, -t, or -f flags
CMD="/usr/bin/time $BIN/$EXE $ARG"

function mon() {
  echo "$CMD $@" >> $MON
  # start cpu monitor
  scripts/mon.sh "$MON" "$EXE" &
}

function run() {
  # start command, measuring disk I/O before and after
  OBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  echo "$CMD $@" >> $LOG
  $CMD $@ >> $LOG 2>&1
  NBLOCKS=`cat /proc/diskstats | grep sdb1 | awk '{print $6}'`
  printf "  I/O (GB)     = %9.5f\n" `echo "scale=10; ($NBLOCKS - $OBLOCKS) * 512 / 1024^3" | bc` >> $LOG
}

for l in $1 ; do
  for t in $2 ; do
    for f in $3 ; do
      sudo fstrim /scratch-ssd

      mon -l$l -t$t -f$f

      run -l$l -t$t -f$f

      # wait for mon.sh to kill itself
      sleep 5
    done
  done
done
