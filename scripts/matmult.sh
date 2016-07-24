#! /bin/bash

function run() {
  echo "$@" >> $LOG
  $TIMEEXE $@ >> $LOG 2>&1
}

function build() {
  N=$((T*2**GLG))
  M=$((225580*GB/T/2**LLG))
  #M=$((450560*GB/T/2**LLG))
  #M=$((7208960*GB/T/2**LLG))
  #M=$((1802240*GB/T/2**LLG))
  P=$((T*3*2**GLG))
  run build/bin/matmult -n$N -m$M -p$P -t$T -f$1
  echo -e "$hline" >> $LOG
  for ((i=$LLG; i<=$GLG; ++i)) ; do
    Y=$((N/2**(GLG-i)))
    X=$((M/2**(i-LLG)))
    Z=$((P/2**(GLG-i)))
    if (($1*$T==$Y)) ; then
      run build/bin/matmult -n$N -m$M -p$P -y$Y -x$X -z$Z -t$T -f$1
      echo -e "$hline" >> $LOG
    fi
  done
  echo -e "$HLINE" >> $LOG
}

# Default params
TIMEEXE=/usr/bin/time
T=4
GB=1
POWS="1 2 4 8 16 32 64 128 256 512 1024 2048"
TEMPLATE="matmult"
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
