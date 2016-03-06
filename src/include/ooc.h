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


/* Maximum number of fibers per thread. */
#define OOC_NUM_FIBERS 10


#define ooc_for(loops) \
  {\
    int _ret;\
    _ret = ooc_init();\
    assert(!_ret);\
\
    for (loops) {

#define ooc(kern) \
      _ret = ooc_sched(&mykernel, ooc1

#define ooc1(i, args) \
      i, args);\
      assert(!_ret);\
    }\
  }\
  {\
    int __ret;\
    __ret = ooc_finalize();\
    assert(!__ret);\
  }

/* Example invocations --
ooc_for (i=0; i<10; ++i) {
  ooc(mykernel)(i, args);
}

ooc_for (i=0; i<10; ++i)
  ooc(mykernel)(i, args);
*/


/* sched.c */
int ooc_init(void);
int ooc_finalize(void);
int ooc_sched(void (*kern)(void * const), size_t const i, void * const args);


#endif /* OPENOOC_H */
