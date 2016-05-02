bench_PROGRAMS := longjmp mprotect siglongjmp sigsegv swap swapcontext
bench_CFLAGS   := -fopenmp
bench_LDFLAGS  := -fopenmp
#bench_LDFLAGS := -fopenmp -ggdb

longjmp_SOURCES := longjmp.c

mprotect_SOURCES := mprotect.c

siglongjmp_SOURCES := siglongjmp.c

sigsegv_SOURCES := sigsegv.c

swap_SOURCES := swap.c
swap_LDADD   := libooc.a

swapcontext_SOURCES := swapcontext.c
