#ifndef OPENOOC_H
#define OPENOOC_H


#define OPENOOC_MAJOR 0
#define OPENOOC_MINOR 0
#define OPENOOC_PATCH 0
#define OPENOOC_RCAND -pre


/* ucontext_t */
#include <ucontext.h>


/* The stack environment to return to via longjmp. */
extern __thread jmp_buf ooc_ret_env;

/* The thread context at the point SIGSEGV was raised. */
extern __thread ucontext_t ooc_ret_uc;


/* The OOC black magic. */
#define OOC_FOR(loops) \
  for (loops) {\
    int _ret/*, _run*/;\
\
    /* Set SIGSEGV return point. */\
    _ret = setjmp(ooc_ret_env);\
\
    /* If we arrived here via longjmp. */\
    if (_ret) {\
      /* Save my pre-sigsegv context. */\
      /*  memcpy(&(fiber[me].uc), &ooc_ret_uc, sizeof(ooc_ret_uc));*/\
\
      /* Find a fiber that is runnable */\
      /*  _run = -1;*/\
      /*  ???*/\
\
      /* If no fiber is runnable and there are more available fiber slots,\
       * then create a new fiber. */\
      /*  if (-1 == _run && ???) {*/\
      /*    continue;*/\
      /*  }*/\
      /* Otherwise, wait until one of the fibers becomes runnable. */\
      /*  else {*/\
      /*  }*/\
\
      /* Switch to the runnable fiber */\
      /*  setcontext(&(fiber[run].uc));*/\
    }
#define OOC_DO \
    {
#define OOC_DONE \
    }\
  }


#endif /* OPENOOC_H */
