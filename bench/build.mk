bench_PROGRAMS := micro micro-latency swap
bench_CFLAGS   := -fopenmp

micro_SOURCES  := impl/io.c impl/libc.c impl/sbma.c micro.c
micro_HEADERS  := impl/impl.h
micro_LDLIBS   := -lrt

micro-latency_SOURCES := micro-latency.c
micro-latency_LDLIBS  := -lrt

swap_SOURCES := swap.c
swap_LDADD   := libooc.a
swap_LDFLAGS := -fopenmp
#swap_LDFLAGS := -fopenmp -ggdb
