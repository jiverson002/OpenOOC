#!/bin/bash

function run() {
  if [[ $DEBUG -ne 0 ]] ; then
    eval "echo \"$TIMEEXE $@ >> $LOG 2>&1\""
  else
    eval "echo "$@" >> $LOG"
    eval "$TIMEEXE $@ >> $LOG 2>&1"
  fi
}

function build() {
  F=$1
  Y=$((T*F))    #!< number of fibers = number of rows in block
  X=$((WS/2/Y)) #!< partial dot product size (see computation of M)
  Z=$((Y*3))    #!< same as Y

  run build/bin/matmult -l$L -n$N -m$M -p$P -y$Y -x$X -z$Z -t$T -f$F
  if [[ $((2**MAXF)) == $F ]] ; then
    run build/bin/matmult -l$L -n$N -m$M -p$P -y$Y -x$X -z$Z -t$T -f0
  fi
}

# Constants
DT=8                        #!< Datatype size multiplier
KB=$((1024))                #!< Kebibyte multiplier
MB=$((1024*KB))             #!< Mebibyte multiplier
GB=$((1024*MB))             #!< Gebibyte multiplier
TIMEEXE=/usr/bin/time       #!< Executable to measure time
TEMPLATE="mm-\${T}-\${BM}-\${WS}" #!< Output file template (will be appended with a
                            #   unique random id)
PO2="1 2 4 8 16 32 64 128 256 512 1024 2048 4096" #!< powers of 2
PO2R="4096 2048 1024 512 256 128 64 32 16 8 4 2 1" #!< powers of 2 reverse
hline="\\n--------------------------------------------------------------------------------\\n"

# Default params
T=1      #!< Number of threads
BM=0     #!< Bitmask of fiber counts to execute
ML=0     #!< Amount of memory to be locked
WS=1*$GB #!< Working set (desired resident memory requirement for each
         #   computation block)
DEBUG=0  #!< Debug indicator variable

# Parse command line
while [[ $# > 0 ]] ; do
  key=$1
  case $key in
  -b)
    shift # past argument
    key="$1"
    ;&
  --bitmask=*)
    BM="${key#*=}"
    ;;
  -m)
    shift # past argument
    key="$1"
    ;&
  --memory=*)
    ML="${key#*=}"
    ;;
  -t)
    shift # past argument
    key="$1"
    ;&
  --threads=*)
    T="${key#*=}"
    ;;
  -w)
    shift # past argument
    key="$1"
    ;&
  --workset=*)
    WS="${key#*=}"
    ;;
  -d|--debug)
    DEBUG=1
    ;;
  -h|--help)
    echo "usage: matmult.sh"
    echo "  -b | --bitmask="
    echo "  -m | --memory="
    echo "  -t | --threads="
    echo "  -w | --workset="
    echo "  -d | --debug"
    echo "  -h | --help"
    exit
    ;;
  *)
    # unknown option
    echo "unknown option '$key'"
  ;;
  esac
  shift # past value or argument=value or argument with no value
done

# Adjust working set size for datatype
WS=$((WS/DT))

# Increment the identifier in the filename until the filename is unique
LOG="$TEMPLATE-0"
while [ -e `eval echo $LOG` ] ; do
  ID=${LOG##*-}
  ID=$((ID+1))
  LOG="$TEMPLATE-$ID"
done

# Find lowest power of 2 (log of min number of fibers per thread)
MINF=0
for i in $PO2 ; do
  if (($i==($BM&$i))) ; then
    break
  fi
  MINF=$((MINF+1))
done

# Find greatest power of 2 (log of max number of fibers per thread)
MAXF=12
for i in $PO2R ; do
  if (($i==($BM&$i))) ; then
    break
  fi
  MAXF=$((MAXF-1))
done

# Compute input matrices' sizes
# N = threads * max fibers (must be at least one row for each possible fiber)
# M = working size / (threads * min fibers) (dictates memory requirement of
#     single dot product iteration. a single dot product iteration means each
#     fiber for each thread computes a complete dot product. for this reason we
#     compute it as a function of min fibers so that even with the smallest
#     number of fibers, the memory requirement is met. for greater than the
#     minimum number of fibers, the memory requirement will be ensured by
#     blocking using X, Y, and Z variables)
# P = two times larger than N to ensure eviction of previous memory
N=$((T*2**MAXF))
M=$((WS/2/(T*2**MINF))) # divide by 2 for 2x memory due to matrix A and matrix B
P=$((N*3))

# Execute the tests
for L in $ML ; do
  for i in $PO2 ; do
    if (($i==($BM&$i))) ; then
      build $i
    fi
  done
done
