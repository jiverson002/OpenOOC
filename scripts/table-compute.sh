#! /bin/bash

grep Compute $1 > tmp0.txt

let "nr=0"
while read line ; do
  let "mod=nr % 2"

  if [[ $mod -eq 0 ]] ; then
    let "num=num+1"
  fi

  echo $line | awk '{print $3}' >> tmp${num}.txt

  let "nr=nr+1"
done < tmp0.txt

echo -e "-y1    -x1      -z1\n-y1760 -x32768  -z1760" > tmp0.txt

echo -e "\t\t\t-f0\t\t-f1\t\t-f2\t\t-f4\t\t-f8\t\t-f16\t\t-f32\t\t-f64\t\t-f128\t\t-f256\t\t-f440" > table.txt
eval paste tmp{0..$num}.txt >> table.txt

eval rm -f tmp{0..$num}.txt
