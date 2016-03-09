apps_PROGRAMS := mm
apps_LDADD    := libooc.a
apps_CFLAGS   := -fopenmp
apps_LDFLAGS  := -fopenmp

mm_SOURCES := mm.c
mm_LDLIBS  := -lrt
