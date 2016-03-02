apps_PROGRAMS := mm
apps_LDADD    := libooc.a

mm_SOURCES := mm.c
mm_LDLIBS  := -lrt
