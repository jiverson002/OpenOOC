#! /bin/bash

grep "Compute" $1 > tmp0.txt
grep "Compute" $2 > tmp1.txt
grep "Compute" $3 > tmp2.txt
grep "Compute" $4 > tmp3.txt

paste -d" " tmp0.txt tmp1.txt tmp2.txt tmp3.txt > tmp4.txt

let "num=4"
while read line ; do
  let "num=num+1"
  echo $line | awk '{printf "%12s\n",  $3}' >  tmp${num}.txt
  echo $line | awk '{printf "%12s\n",  $6}' >> tmp${num}.txt
  echo $line | awk '{printf "%12s\n",  $9}' >> tmp${num}.txt
  echo $line | awk '{printf "%12s\n", $12}' >> tmp${num}.txt
done < tmp4.txt

printf "%-9s%-9s%-9s\n" "-y1"    "-x1"     "-z1"    >  tmp4.txt
printf "%-9s%-9s%-9s\n" "-y1760" "-x32768" "-z1760" >> tmp4.txt
printf "%-9s%-9s%-9s\n" "-y1760" "-x32768" "-z1760" >> tmp4.txt
printf "%-9s%-9s%-9s\n" "-y1760" "-x32768" "-z1760" >> tmp4.txt

printf "%9s%9s%9s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s  %-11s\n" " " " " " " "-f0" "-f1" "-f2" "-f4" "-f8" "-f16" "-f32" "-f64" "-f128" "-f256" "-f440" > table-compute.txt
eval paste -d\" \" tmp{4..$num}.txt >> table-compute.txt

eval rm -f tmp{0..$num}.txt
