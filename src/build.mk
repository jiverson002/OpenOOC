src_LIBRARIES := libooc.a
# If native aio were used instead of posix aio, then link against -laio
src_LDLIBS    := -lrt
src_CFLAGS    := -fopenmp

libooc.a_SOURCES := malloc.c sched.c sp_tree.c vma_alloc.c
