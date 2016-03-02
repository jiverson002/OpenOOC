#ifndef OPENOOC_H
#define OPENOOC_H


#define OPENOOC_MAJOR 0
#define OPENOOC_MINOR 0
#define OPENOOC_PATCH 0
#define OPENOOC_RCAND -pre


/* struct aiocb */
#include <aio.h>

/* EINPROGRESS, ECANCELED */
#include <errno.h>

/* abort */
#include <stdlib.h>

/* memcpy */
#include <string.h>

/* jmp_buf */
#include <setjmp.h>

/* ucontext_t */
#include <ucontext.h>


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
  struct aiocb aio;
  ucontext_t uc;
} ooc_fiber_t;


/* The stack environment to return to via longjmp. */
extern __thread jmp_buf ooc_ret_env;

/* The thread context at the point SIGSEGV was raised. */
extern __thread ucontext_t ooc_ret_uc;

/* The fibers. */
extern __thread ooc_fiber_t ooc_fiber[OOC_NUM_FIBERS];

/* Number of active fibers. */
extern __thread int ooc_cur_fibers;

/* My fiber id. */
extern __thread int ooc_me;


/* The OOC black magic. */
#define OOC_FOR(loops) \
  for (loops) {\
    int _ret, _i;\
\
    /* Initialize myself. */\
    ooc_fiber[ooc_me].state = OOC_RUNNING;\
\
    /* Set SIGSEGV return point. */\
    _ret = setjmp(ooc_ret_env);\
\
    /* If we arrived here via longjmp. */\
    if (_ret) {\
      /* If this fiber has async-io in progress upon arriving here via longjmp,
       * then we need to search the other existing fibers to see if any of their
       * in async-io has completed. If not and if resources are available, then
       * we can create a new fiber to continue execution. If no resources are
       * available, then we must just wait until one of the existing fibers'
       * async-io completes. */\
      if (OOC_AIO_INPROGRESS == ooc_fiber[ooc_me].state) {\
        /* Save my pre-sigsegv context. */\
        memcpy(&(ooc_fiber[ooc_me].uc), &ooc_ret_uc, sizeof(ooc_ret_uc));\
\
        /* Find a fiber that is runnable */\
        for (;;) {\
          /* Search existing fibers for one whose async-io is finished. */\
          for (_i=0; _i<ooc_cur_fibers; ++_i) {\
            if (OOC_AIO_INPROGRESS == ooc_fiber[_i].state) {\
              _ret = aio_error(&(ooc_fiber[_i].aio));\
\
              switch (_ret) {\
                case 0:\
                /* TODO Need to check aio_return(&(ooc_fiber[_i].aio)) to see if
                 * all bytes were successfully read. */\
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
\
      /* If we arrive here, it is because this fiber had no async-io in progress
       * upon arriving here via longjmp. In this case, we should switch to the
       * pre-sigsegv context of this fiber. */\
      setcontext(&ooc_ret_uc);\
    }\
    else {\
    }
#define OOC_DO \
    {
#define OOC_DONE \
    }\
  }


#endif /* OPENOOC_H */
