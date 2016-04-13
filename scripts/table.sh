#! /bin/bash

grep Compute run.log > tmp0.txt

let "nr=0"
while read line ; do
  let "mod=nr % 6"

  if [[ $mod -eq 0 ]] ; then
    let "num=num+1"
  fi

  echo $line | awk '{print $3}' >> tmp${num}.txt

  let "nr=nr+1"
done < tmp0.txt

echo -e "-y1    -x1      -z1\n-y64   -x450560 -z192\n-y128  -x225280 -z384\n-y256  -x112640 -z768\n-y512  -x56320  -z1536\n-y1024 -x28160  -z3072" > tmp0.txt

echo -e "                   \t-f0\t\t-f1\t\t-f16\t\t-f32\t\t-f64\t\t-f128\t\t-f256" > table.txt
eval paste tmp{0..$num}.txt >> table.txt

eval rm -f tmp{0..$num}.txt
