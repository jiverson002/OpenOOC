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


#ifndef OOC_COMMON_H
#define OOC_COMMON_H


/* uintptr_t */
#include <inttypes.h>

/* struct sigaction, SIGSTKSZ */
#include <signal.h>

/* size_t */
#include <stddef.h>

/* FILE */
#include <stdio.h>

/* ucontext_t */
#include <ucontext.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>


/*----------------------------------------------------------------------------*/
/* Implementation constants */
/*----------------------------------------------------------------------------*/
/*! OOC page size. */
#define OOC_PAGE_SIZE sysconf(_SC_PAGESIZE)

/*! Maximum number of fibers per thread. */
#define OOC_MAX_FIBERS 2048

/*! Protection flags. */
#define VM_PROT_NONE  0x0LU
#define VM_PROT_READ  0x1LU
#define VM_PROT_WRITE 0x3LU


/*----------------------------------------------------------------------------*/
/* Simple lock implementation */
/*----------------------------------------------------------------------------*/
#define lock_init ooc_lock_init
#define lock_free ooc_lock_free
#define lock_get  ooc_lock_get
#define lock_let  ooc_lock_let
#define lock_try  ooc_lock_try
#define lock_t    ooc_lock_t
#ifdef _OPENMP
/* omp_lock_t */
#include <omp.h>

typedef omp_lock_t lock_t;

#define ooc_lock_init(lock) (omp_init_lock(lock), 0)
#define ooc_lock_free(lock) (omp_destroy_lock(lock), 0)
#define ooc_lock_get(lock)  (omp_set_lock(lock), 0)
#define ooc_lock_let(lock)  (omp_unset_lock(lock), 0)
#define ooc_lock_try(lock)  (0 == omp_test_lock(lock))
#else
typedef int lock_t;

#define ooc_lock_init(lock) 0
#define ooc_lock_free(lock) 0
#define ooc_lock_get(lock)  0
#define ooc_lock_let(lock)  0
#define ooc_lock_try(lock)  0
#endif


/*----------------------------------------------------------------------------*/
/* */
/*----------------------------------------------------------------------------*/
#define aioctx   ooc_aioctx
#define aioctx_t ooc_aioctx_t
/*! Asynchronous i/o context. */
struct aioctx
{
  int fixme;
};
typedef struct aioctx * aioctx_t;

#define aioreq   ooc_aioreq
#define aioreq_t ooc_aioreq_t
/*! Asynchronous i/o request. */
struct aioreq
{
  /* The order of these fields is implementation-dependent */

  int             aio_id;    /*! Fiber ID */
  void *          aio_buf;   /*! Location of buffer */
  size_t          aio_count; /*! Length of transfer */
  volatile int    aio_error; /*! Request error */
  int             aio_op;    /*! Indicator of read(0) / write(1) / exit(-1) operation */
};
typedef struct aioreq aioreq_t;

#define vm_area ooc_vm_area
#define sp_node ooc_vm_area
/*! Virtual memory area. */
struct vm_area
{
  struct vm_area * vm_next;   /* list of VMAs (for vma_alloc() and sp_tree_*) */
  struct vm_area * vm_prev;   /* doubly-linked list of VMAs (for sp_tree_*) */

  struct sp_node * sp_p;      /* parent node (used only by sp_tree_*() */
  struct sp_node * sp_l;      /* left child ... */
  struct sp_node * sp_r;      /* right child ... */

  unsigned long  vm_flags;    /* flags */
  void *         vm_start;    /* VMA start, inclusive */
  void *         vm_end;      /* VMA end, exclusive */

  lock_t         vm_lock;     /* struct lock */
};

#define sp_tree ooc_sp_tree
/*! Splay tree. */
struct sp_tree
{
  struct sp_node * root;      /* root of tree */
  lock_t         lock;        /* struct lock */
};


/*----------------------------------------------------------------------------*/
/* */
/*----------------------------------------------------------------------------*/
/*! Out-of-core execution context, henceforth known simply as a fiber. */
#define fiber ooc_fiber
struct fiber
{
  size_t     iter;
  aioreq_t   aioreq;
  ucontext_t handler;
  ucontext_t trampoline;
  ucontext_t kern;
  ucontext_t tmp_uc;
  void *     args;
  void *     addr;
  char       stack[SIGSTKSZ];
  char       tmp_stack[SIGSTKSZ];
  void       (*kernel)(size_t const, void * const);

  #define F_iter(id)       (T_fiber[id].iter)
  #define F_aioreq(id)     (T_fiber[id].aioreq)
  #define F_handler(id)    (T_fiber[id].handler)
  #define F_trampoline(id) (T_fiber[id].trampoline)
  #define F_kern(id)       (T_fiber[id].kern)
  #define F_tmp_uc(id)     (T_fiber[id].tmp_uc)
  #define F_args(id)       (T_fiber[id].args)
  #define F_addr(id)       (T_fiber[id].addr)
  #define F_stack(id)      (T_fiber[id].stack)
  #define F_tmp_stack(id)  (T_fiber[id].tmp_stack)
  #define F_kernel(id)     (T_fiber[id].kernel)
};


/*! Thread context. */
#define thread ooc_thread
struct thread
{
  int              is_init;     /*!< Indicator of library init. */
  int              me;          /*!< Current fiber id. */
  int              n_wait;      /*!< Waiting fiber counter. */
  int              n_idle;      /*!< Idle fiber counter. */
  unsigned int     num_fibers;  /*!< Number of fibers. */
  uintptr_t        ps;          /*!< System page size. */
  aioctx_t         aioctx;      /*!< Async I/O context. */
  ucontext_t       main;        /*!< The main context. */
  struct sigaction old_act;     /*!< Old sigaction. */
  int              * idle_list; /*!< List of idle fibers. */
  struct fiber     * fiber;     /*!< Fibers. */

  #define T_is_init    (thread.is_init)
  #define T_me         (thread.me)
  #define T_n_wait     (thread.n_wait)
  #define T_n_idle     (thread.n_idle)
  #define T_num_fibers (thread.num_fibers)
  #define T_ps         (thread.ps)
  #define T_aioctx     (thread.aioctx)
  #define T_main       (thread.main)
  #define T_old_act    (thread.old_act)
  #define T_idle_list  (thread.idle_list)
  #define T_fiber      (thread.fiber)
};


#define process ooc_process
/*! Process context. */
struct process
{
  /*! OOC Virtual Memory Area (VMA) tree - shared by all threads in a process,
   * since said threads all share the same address space. */
  struct sp_tree vma_tree; /*!< Per-process system page table. */

  #define P_vma_tree process.vma_tree
};


/*----------------------------------------------------------------------------*/
/* Function prototypes */
/*----------------------------------------------------------------------------*/
#define aio_setup ooc_aio_setup
/*! Setup an asynchronous i/o context. */
int aio_setup(unsigned int const nr, aioctx_t * const ctx);

#define aio_destroy ooc_aio_destroy
/*! Destroy an asynchronous i/o context. */
int aio_destroy(aioctx_t ctx);

#define aio_read ooc_aio_read
/*! Post an asynchronous read request. */
int aio_read(void * const buf, size_t const count, aioreq_t * const aioreq);

#define aio_write ooc_aio_write
/*! Post an asynchronous write request. */
int aio_write(void const * const buf, size_t const count,
              aioreq_t * const aioreq);

#define aio_error ooc_aio_error
/*! Check for completion of a request. */
int aio_error(aioreq_t * const aioreq);

#define aio_return ooc_aio_return
/*! Get the return value of a completed request. */
ssize_t aio_return(aioreq_t * const aioreq);

#define aio_suspend ooc_aio_suspend
/*! Suspend execution until some aio request completes. */
aioreq_t * aio_suspend(void);


/* sp_tree.c */
#define sp_tree_init ooc_sp_tree_init
/*! Initialize the linked list to an empty list. */
int sp_tree_init(struct sp_tree * const sp);

#define sp_tree_free ooc_sp_tree_free
/*! Start from the root and recursively free each subtree of a splay tree. */
int sp_tree_free(struct sp_tree * const sp);

#define sp_tree_insert ooc_sp_tree_insert
/*! Insert node with specified data into the tree, it MUST NOT exist. */
int sp_tree_insert(struct sp_tree * const sp, struct sp_node * const z);

#define sp_tree_find_and_lock ooc_sp_tree_find_and_lock
/*! Find and lock node containing vm_addr in the tree, it MUST exist. */
int sp_tree_find_and_lock(struct sp_tree * const sp, void * const vm_addr,\
                          struct sp_node ** const zp);

#define sp_tree_find_mod_and_lock ooc_sp_tree_find_mod_and_lock
/*! Find and lock node containing vm_addr in the tree, it MUST exist. */
int sp_tree_find_mod_and_lock(struct sp_tree * const sp, void * const vm_addr,\
                              struct sp_node ** const zp);

#define sp_tree_remove ooc_sp_tree_remove
/*! Remove node with specified datum in the tree, if MUST exist. */
int sp_tree_remove(struct sp_tree * const sp_tree, void * const vm_addr);


/* vma_alloc.c */
#define vma_alloc ooc_vma_alloc
/*! Get next available vm_area struct. */
struct vm_area * vma_alloc(void);

#define vma_free ooc_vma_free
/*! Return a vm_area struct to the system. */
void vma_free(struct vm_area * const vma);

#define vma_gpool_init ooc_vma_gpool_init
/*! Initialize the vm_area struct memory pool. */
void vma_gpool_init(void);

#define vma_gpool_free ooc_vma_gpool_free
/*! Free the vm_area struct memory pool. */
void vma_gpool_free(void);

#define vma_gpool_gather ooc_vma_gpool_gather
/*! Gather statistics runtime statistics. */
void vma_gpool_gather(void);

#define vma_gpool_show ooc_vma_gpool_show
/*! Show runtime statistics. */
void vma_gpool_show(void);


/*----------------------------------------------------------------------------*/
/* Extern variables */
/*----------------------------------------------------------------------------*/
#define thread ooc_thread
/*! Thread context. */
extern __thread struct thread thread;

#define process ooc_process
/*! Process context. */
extern struct process process;


/*----------------------------------------------------------------------------*/
/* Debug print statements. */
/*----------------------------------------------------------------------------*/
#if 0
#include <stdio.h>
#include <sys/syscall.h>
#define dbg_printf(...) (printf(__VA_ARGS__), fflush(stdout))
#else
#define dbg_printf(...) (void)(0)
#endif


/*----------------------------------------------------------------------------*/
/* Log print statements. */
/*----------------------------------------------------------------------------*/
#if 0
#include <stdio.h>
#include <sys/syscall.h>
#define log_init(log,name) \
{\
  char * _lname = malloc(FILENAME_MAX);\
  sprintf(_lname, name "%d", (int)syscall(SYS_gettid));\
  log = fopen(_lname, "w");\
  assert(log);\
  free(_lname);\
}
#define log_finalize(log) \
{\
  int _ret = fclose(log);\
  assert(!_ret);\
}
#define log_fprintf(log,...) (fprintf(log,__VA_ARGS__), fflush(log))
#else
#define log_init(...)        (void)(0)
#define log_finalize(...)    (void)(0)
#define log_fprintf(log,...) (void)(0 && log)
#endif


#endif /* OOC_COMMON_H */
