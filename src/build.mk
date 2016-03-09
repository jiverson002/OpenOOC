src_LIBRARIES := libooc.a
# If native aio were used instead of posix aio, then link against -laio
src_LDLIBS    := -lrt
src_CFLAGS    := -fopenmp

libooc.a_SOURCES := lock/free.c \
                    lock/init.c \
                    lock/let.c \
                    lock/get.c \
                    splay/splay.c \
                    malloc.c sched.c
