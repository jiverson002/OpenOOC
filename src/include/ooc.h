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


#ifdef WITH_NATIVE_AIO
/* aio_context_t, struct iocb, struct io_event */
#include <linux/aio_abi.h>

/* struct timespec */
#include <linux/time.h>

typedef aio_context_t ooc_aioctx_t;
typedef struct iocb ooc_aioreq_t;
typedef struct io_event ooc_aioevnt_t;
#else
/* struct aiocb, struct timespec */
#include <aio.h>

typedef void * ooc_aioctx_t; /* unused */
typedef struct aiocb ooc_aioreq_t;
typedef void * ooc_aioevnt_t; /* unused */
#endif

/* EINPROGRESS, ECANCELED */
#include <errno.h>

/* abort */
#include <stdlib.h>

/* memcpy */
#include <string.h>

/* jmp_buf */
#include <setjmp.h>

/* siginfo_t */
#include <signal.h>

/* off_t, ssize_t */
#include <sys/types.h>

/* ucontext_t, setcontext */
#include <ucontext.h>

/* TEMPORARY */
#include <stdio.h>


/* Maximum number of fibers per thread. */
#define OOC_NUM_FIBERS 10


/* State flags for an OOC fiber. */
enum ooc_state_flags
{
  OOC_RUNNING,
  OOC_AIO_INPROGRESS
};


/* An execution context, henceforth known as a fiber. */
typedef struct ooc_fiber
{
  unsigned int state;
  ooc_aioreq_t aioreq;
  ucontext_t uc;
} ooc_fiber_t;


/* The stack environment to return to via longjmp. */
extern __thread jmp_buf ooc_ret_env;

/* The thread context at the point SIGSEGV was raised. */
extern __thread volatile ucontext_t ooc_ret_uc;
extern __thread ucontext_t ooc_tmp_uc;

/* The fibers. */
extern __thread ooc_fiber_t ooc_fiber[OOC_NUM_FIBERS];

/* Number of active fibers. */
extern __thread int ooc_cur_fibers;

/* My fiber id. */
extern __thread int ooc_me;


/* aio function prototypes. */
int ooc_aio_setup(unsigned int const nr, ooc_aioctx_t * const ctx);
int ooc_aio_destroy(ooc_aioctx_t ctx);
int ooc_aio_read(int const fd, void * const buf, size_t const count,
                  off_t const off, ooc_aioreq_t * const aioreq);
int ooc_aio_write(int const fd, void const * const buf, size_t const count,
                  off_t const off, ooc_aioreq_t * const aioreq);
int ooc_aio_error(ooc_aioreq_t * const aioreq);
ssize_t ooc_aio_return(ooc_aioreq_t * const aioreq);
int ooc_aio_cancel(ooc_aioreq_t * const aioreq);
int ooc_aio_suspend(ooc_aioreq_t const ** const aioreq_list,
                    unsigned int const nr,
                    struct timespec const * const timeout);

/*  sigsegv function prototypes. */
void ooc_sigsegv(int const _sig, siginfo_t * const _si, void * const _uc);


/* The OOC black magic. */
#define OOC_FOR(loops) \
  for (loops) {\
    int _ret, _i;\
\
    /* Initialize myself. */\
    ooc_fiber[ooc_me].state = OOC_RUNNING;\
    printf("[%2d] initialized myself\n", ooc_me);\
\
    /* Set SIGSEGV return point. */\
    _ret = setjmp(ooc_ret_env);\
\
    /* If we arrived here via longjmp. */\
    if (_ret) {\
      printf("[%2d] returned via longjmp from SIGSEGV\n", ooc_me);\
      /* If this fiber has async-io in progress upon arriving here via longjmp,
       * then we need to search the other existing fibers to see if any of their
       * in async-io has completed. If not and if resources are available, then
       * we can create a new fiber to continue execution. If no resources are
       * available, then we must just wait until one of the existing fibers'
       * async-io completes. */\
      if (OOC_AIO_INPROGRESS == ooc_fiber[ooc_me].state) {\
        /* Save my pre-sigsegv context. */\
        memcpy(&(ooc_fiber[ooc_me].uc), (void*)&ooc_ret_uc, sizeof(ooc_ret_uc));\
\
        /* Find a fiber that is runnable */\
        for (;;) {\
          /* Search existing fibers for one whose async-io is finished. */\
          for (_i=0; _i<ooc_cur_fibers; ++_i) {\
            if (OOC_AIO_INPROGRESS == ooc_fiber[_i].state) {\
              _ret = ooc_aio_error(&(ooc_fiber[_i].aioreq));\
\
              switch (_ret) {\
                case 0:\
                /* TODO Need to check aio_return(&(ooc_fiber[_i].aioreq)) to see
                 * if all bytes were successfully read. */\
                goto OOC_SEARCH_DONE;\
\
                case EINPROGRESS:\
                break;\
\
                case ECANCELED:\
                default:\
                /* TODO Arriving here is erroneous, act accordingly. */\
                abort();\
                break;\
              }\
            }\
          }\
\
          /* If we arrive here, then no runnable fiber was found. If there are
           * resources available for more fibers, then jump to OOC_NEW_FIBER label
           * and create a new fiber. Otherwise, jump to the OOC_SEARCH_AGAIN label
           * to search again. */\
          if (ooc_cur_fibers < OOC_NUM_FIBERS) {\
            goto OOC_NEW_FIBER;\
          }\
          goto OOC_SEARCH_AGAIN;\
\
          /* If we arrive at this label it is because a runnable fiber was found
           * and we jumped out of the search to here, in which case, _i holds the
           * id of the runnable fiber. */\
          OOC_SEARCH_DONE:\
          ooc_fiber[_i].state = OOC_RUNNING;\
          setcontext(&(ooc_fiber[_i].uc));\
\
          /* If we arrive here, then no runnable fiber was found and there are
           * not enough resources to create more. So, just go re-execute the
           * search for a runnable fiber. It is possible that by now, one of the
           * outstanding async-io requests has completed. */\
          OOC_SEARCH_AGAIN:\
          continue;\
        }\
\
        /* If we arrive here, then no runnable fiber was found, but there are
         * enough resources to create a new fiber. To do this, we will simply
         * continue to the next iteration of the OOC_FOR loop. This has the effect
         * of creating a new fiber. */\
        OOC_NEW_FIBER:\
\
        /* Change ooc_me value -- this is what actually makes me a new fiber. */\
        ooc_me = ooc_cur_fibers++;\
        continue;\
      }\
      else {\
        printf("[%2d] no outstanding async-io\n", ooc_me);\
      }\
\
      /* If we arrive here, it is because this fiber had no async-io in progress
       * upon arriving here via longjmp. In this case, we should switch to the
       * pre-sigsegv context of this fiber. */\
      printf("[%2d] setting context to pre-sigsegv context (%zu,%d)\n", ooc_me,\
        sizeof(ooc_ret_uc), memcmp(&ooc_tmp_uc, (void*)&ooc_ret_uc, sizeof(ooc_ret_uc)));\
      int j;\
      char * b=(char*)&ooc_ret_uc;\
      for (j=0; j<sizeof(ooc_ret_uc); ++j) {\
        printf("%2x ", b[j]);\
      }\
      printf("\n------------\n");\
      b=(char*)&ooc_tmp_uc;\
      for (j=0; j<sizeof(ooc_ret_uc); ++j) {\
        printf("%2x ", b[j]);\
      }\
      setcontext((ucontext_t*)&ooc_ret_uc);\
    }\
    else {\
      printf("[%2d] set SIGSEGV return point\n", ooc_me);\
    }
#define OOC_DO \
    {
#define OOC_DONE \
    }\
  }


#endif /* OPENOOC_H */
