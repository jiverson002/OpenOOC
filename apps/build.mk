apps_PROGRAMS := mm mm-hc
apps_CFLAGS   := -Wno-unused-function
apps_LDADD    := libooc.a

mm_SOURCES := mm.c

mm-hc_SOURCES := mm-hc.c
mm-hc_LDLIBS  := -lrt
