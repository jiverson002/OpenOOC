#!/usr/bin/awk -f

BEGIN { last=-1 }
      {
        if (-1 != last)
          gap1=$2-last;
        else
          gap1=0;
        gap2=$3-$2;
        last=$3;
        printf "%.5f %.5f\n",gap1,gap2
      }
END   { }
