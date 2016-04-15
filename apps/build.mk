apps_PROGRAMS := matmult mlock samplesort swap vecsort
apps_LDADD    := libooc.a
apps_CFLAGS   := -fopenmp
apps_LDFLAGS  := -fopenmp

matmult_SOURCES := matmult.c

mlock_SOURCES := mlock.c

swap_SOURCES := swap.c

samplesort_SOURCES := samplesort.c

vecsort_SOURCES := vecsort.c
