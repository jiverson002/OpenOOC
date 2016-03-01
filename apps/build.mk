apps_LDADD    := libooc.a
apps_PROGRAMS := mm mm-hc
apps_LDFLAGS  := -lrt

mm_SOURCES    := mm.c mm-hc.c
