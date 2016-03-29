bench_PROGRAMS := micro micro-latency
bench_LDLIBS = -lrt

micro_SOURCES  := impl/io.c impl/libc.c impl/sbma.c micro.c
micro_HEADERS  := impl/impl.h

micro-latency_SOURCES := micro-latency.c
