bench_PROGRAMS := aio longjmp mprotect siglongjmp sigsegv swap swapcontext
bench_CFLAGS   := -fopenmp
bench_LDFLAGS  := -fopenmp
#bench_LDFLAGS := -fopenmp -ggdb

aio_SOURCES := aio.c
aio_LDADD   := libooc.a

longjmp_SOURCES := longjmp.c

mprotect_SOURCES := mprotect.c

siglongjmp_SOURCES := siglongjmp.c

sigsegv_SOURCES := sigsegv.c

swap_SOURCES := swap.c
swap_LDADD   := libooc.a

swapcontext_SOURCES := swapcontext.c
