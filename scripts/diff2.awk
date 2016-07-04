#!/usr/bin/awk -f

BEGIN { }
      {
        print $3-$2
      }
END   { }
