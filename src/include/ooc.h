/*
Copyright (c) 2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef OPENOOC_H
#define OPENOOC_H


#define OPENOOC_MAJOR 0
#define OPENOOC_MINOR 0
#define OPENOOC_PATCH 0
#define OPENOOC_RCAND -pre


/* size_t */
#include <stddef.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>


/* Maximum number of fibers per thread. */
#define OOC_NUM_FIBERS 10

/* OOC page size. */
#define OOC_PAGE_SIZE sysconf(_SC_PAGESIZE)


#define OOC_INIT \
  {\
    int _ret;\
    _ret = ooc_init();\
    assert(!_ret);\
  }

#define OOC_FINAL \
  {\
    int _ret;\
    _ret = ooc_finalize();\
    assert(!_ret);\
  }

#define OOC_CALL(kern) \
  {\
    int _ret;\
    _ret = ooc_sched(&kern, OOC_CALL1

#define OOC_CALL1(i, args) \
    i, args);\
    assert(!_ret);\
  }


/* malloc.c */
void * ooc_malloc(size_t const size);
void ooc_free(void * ptr);


/* sched.c */
int ooc_init(void);
int ooc_finalize(void);
int ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
              void * const args);


#endif /* OPENOOC_H */
