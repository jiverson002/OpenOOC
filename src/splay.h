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


#ifndef OOC_SPLAY_H
#define OOC_SPLAY_H


/* uintptr_t, uint8_t */
#include <inttypes.h>

/* size_t */
#include <stddef.h>


/*------------------------------------------------------------------------------
  Splay tree node
------------------------------------------------------------------------------*/
typedef struct sp_nd {
  struct sp_nd * p;
  struct sp_nd * l;
  struct sp_nd * r;
  uintptr_t b;
  size_t s;
} sp_nd_t;

/*------------------------------------------------------------------------------
  Splay tree
------------------------------------------------------------------------------*/
typedef struct sp {
  sp_nd_t * root;
  sp_nd_t * next;
} sp_t;

/*------------------------------------------------------------------------------
  Virtual memory allocation
------------------------------------------------------------------------------*/
struct vma {
  sp_nd_t nd;
  uint8_t * pflags;
};


/* System page table. */
/* TODO Since this is not thread local, it must be access protected to prevent
 * race conditions between threads. */
extern sp_t _sp;


int ooc_sp_init(sp_t * const sp);
int ooc_sp_free(sp_t * const sp);
int ooc_sp_insert(sp_t * const sp, sp_nd_t * const z, uintptr_t const b,
                  size_t const s);
int ooc_sp_find(sp_t * const sp, uintptr_t const d, sp_nd_t ** const sp_nd_p);
int ooc_sp_remove(sp_t * const sp, uintptr_t const d);
int ooc_sp_next(sp_t * const sp, sp_nd_t ** const sp_nd_p);
int ooc_sp_empty(sp_t * const sp);


#endif /* OOC_SPLAY_H */
