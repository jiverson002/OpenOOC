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
    /* TODO How to programmatically fill in XXX, i.e., determine which kernel
     * should be called?? */\
    _ret = ooc_init(XXX);\
    assert(!_ret);\
  }\
\
  for (loops)

#define ooc1(iter) \
  {\
    int _ret;\
    _ret = ooc_sched(iter);\
    assert(!_ret);\
    /* TODO flush */\
  }

#define ooc(kern)   ooc1

/* TODO How do we pass arguments to the kernel, since kern only takes one int
 * argument, namely, the fiber number. Maybe, we have each fiber have some type
 * of args struct like pthread_create with is stored statically in sched.c */
/* TODO How do we insert ooc_finalize() after all iterations have completed? */


/*
ooc_for (i=0; i<10; ++i) {
  ooc(mykernel)(i);
}
*/


/* sched.c */
int ooc_init(void (*kern)(int const));
int ooc_finalize(void);
int ooc_sched(size_t const i);


#endif /* OPENOOC_H */
