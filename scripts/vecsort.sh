#! /bin/bash

function run() {
  echo "$@" >> $LOG
  $TIMEEXE $@ >> $LOG 2>&1
}

function build() {
  N=$((GB*1024**3/8))
  run build/bin/vecsort -n$N -t$T -f$1
  echo -e "$hline" >> $LOG
  for ((i=$LLG; i<=$GLG; ++i)) ; do
    Y=$((N/T/2**i))
    if ((Y*T*$1<=N)) ; then
      run build/bin/vecsort -n$N -y$Y -t$T -f$1
      echo -e "$hline" >> $LOG
    fi
  done
  echo -e "$HLINE" >> $LOG
}

# Default params
TIMEEXE=/usr/bin/time
T=4
GB=16
POWS="1 2 4 8 16 32 64 128 256 512 1024 2048"
TEMPLATE="vecsort"
HLINE="\n================================================================================\n"
hline="\n--------------------------------------------------------------------------------\n"

# Handle input
if [ $# -eq 1 ] ; then
  OPTS=$1
else
  OPTS=0
fi

# Increment the identifier in the filename until the filename is unique
LOG="$TEMPLATE-0"
while [ -e $LOG ] ; do
  ID=${LOG##*-}
  ID=$((ID+1))
  LOG="$TEMPLATE-$ID"
done

# Find lowest power of 2
i=$OPTS
LLG=0
for i in $POWS ; do
  if (($i==($OPTS&$i))) ; then
    break
  fi
  LLG=$((LLG+1))
done

# Find greatest power of 2
i=$OPTS
GLG=-1
while (($i>0)) ; do
  GLG=$((GLG+1))
  i=$((i/2))
done

# Execute the tests
for i in 0 $POWS ; do
  if (($i==($OPTS&$i))) ; then
    build $i
  fi
done
