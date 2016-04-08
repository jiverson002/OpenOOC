apps_PROGRAMS := mm mlock
apps_LDADD    := libooc.a
apps_CFLAGS   := -fopenmp
apps_LDFLAGS  := -fopenmp

mm_SOURCES := mm.c
mm_LDLIBS  := -lrt

mlock_SOURCES := mlock.c
