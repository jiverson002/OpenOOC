apps_PROGRAMS := floyd matmult mlock samplesort vecsort
apps_LDADD    := libooc.a
apps_CFLAGS   := -fopenmp
apps_LDFLAGS  := -fopenmp

floyd_SOURCES := floyd.c

matmult_SOURCES := matmult.c

mlock_SOURCES := mlock.c

samplesort_SOURCES := samplesort.c

vecsort_SOURCES := vecsort.c
