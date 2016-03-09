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


#ifndef OOC_LOCK_H
#define OOC_LOCK_H


#ifdef _OPENMP
/* omp_lock_t */
#include <omp.h>

int lock_init(omp_lock_t * const lock);
int lock_free(omp_lock_t * const lock);
int lock_get(omp_lock_t * const lock);
int lock_let(omp_lock_t * const lock);
#else
typedef int omp_lock_t; /* unused, only necessary so other files do not have */
# define lock_init(...) /* to check for _OPENMP */
# define lock_free(...) 0
# define lock_get(...)  0
# define lock_let(...)  0
#endif


#endif /* OOC_LOCK_H */
