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


#define OOC_FINAL \
  {\
    int _ret;\
    _ret = ooc_finalize();\
    assert(!_ret);\
  }


/* Black magic. */
#define __ooc_defn_make(scope,kern) \
  static void __ooc_kern_ ## kern(size_t const, void * const);\
  static /* inline */ void __ooc_sched_ ## kern(size_t const, void * const);\
  static /* inline */ void\
  __ooc_sched_ ## kern(size_t const i, void * const args) {\
    ooc_sched(&__ooc_kern_ ## kern, i, args);\
  }\
  scope void (*kern)(size_t const, void * const)= &__ooc_sched_ ## kern;\
  static void __ooc_kern_ ## kern
#define __ooc_defn_args(scope,kern) __ooc_defn_make(scope,kern)
#define __ooc_defn_kern(rest)       rest
#define __ooc_defn_void             __ooc_defn_args( ,__ooc_defn_kern(
#define __ooc_defn_xvoid            __ooc_defn_kern(
#define __ooc_defn_type(rest)       __ooc_defn_x ## rest)
/*#define __ooc_defn_extern           __ooc_defn_args(extern, __ooc_defn_type(*/
#define __ooc_defn_static           __ooc_defn_args(static, __ooc_defn_type(
#define __ooc_defn_scope(rest)      __ooc_defn_ ## rest))
#define __ooc_defn                  __ooc_defn_scope

#define __ooc_decl_make(scope,kern) scope void (*kern)
#define __ooc_decl_args(scope,kern) __ooc_decl_make(scope,kern)
#define __ooc_decl_kern(rest)       rest
#define __ooc_decl_void             __ooc_decl_args( ,__ooc_decl_kern(
#define __ooc_decl_xvoid            __ooc_decl_kern(
#define __ooc_decl_type(rest)       __ooc_decl_x ## rest)
/*#define __ooc_decl_extern           __ooc_decl_args(extern, __ooc_decl_type(*/
#define __ooc_decl_static           __ooc_decl_args(static, __ooc_decl_type(
#define __ooc_decl_scope(rest)      __ooc_decl_ ## rest))
#define __ooc_decl                  __ooc_decl_scope


/* malloc.c */
void * ooc_malloc(size_t const size);
void ooc_free(void * ptr);


/* sched.c */
int ooc_init(void);
int ooc_finalize(void);
void ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
               void * const args);


#endif /* OPENOOC_H */
