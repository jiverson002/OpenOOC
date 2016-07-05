#!/usr/bin/awk -f

BEGIN { min=8726878129 }
      {
        if (2 == NF) {
          for (i=1; i<=NF; ++i)
            printf(" %lu", $i-min);
          printf("\n");
        }
        else if (8 == NF) {
          printf("%lu %lu ", $1-min, $2);
          for (i=3; i<=NF; ++i)
            printf(" %lu", $i-min);
          printf("\n");
        }
        else {
          printf("%lu", $1);
          for (i=2; i<=NF; ++i)
            printf(" %lu", $i-min);
          printf("\n");
        }
      }
END   { }
