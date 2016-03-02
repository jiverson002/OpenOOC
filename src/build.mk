src_LIBRARIES := libooc.a
# If native aio were used instead of posix aio, then link against -laio
src_LDLIBS    := -lrt

libooc.a_SOURCES := sched.c
