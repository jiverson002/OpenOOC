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


/* assert */
#include <assert.h>

/* SIGSEGV */
#include <errno.h>

/* __WORDSIZE */
#include <limits.h>

/* longjmp */
#include <setjmp.h>

/* ucontext_t */
#include <ucontext.h>


/* The stack environment to return to via longjmp. */
static __thread jmp_buf ret_env;

/* Memory location which caused fault */
static __thread void * segv_addr;


/*******************************************************************************
 * Function to post asynchronous I/O requests.
 ******************************************************************************/
static void
ooc_async(void)
{
  /* Jump back to the stack environment which resulted in this function being
   * called. */
  longjmp(ret_env, 1);
}


/*******************************************************************************
 *  SIGSEGV handler.
 ******************************************************************************/
static void
ooc_sigsegv(int const _sig, siginfo_t * const _si, void * const _uc)
{
  assert(SIGSEGV == _sig);

  /* Save the memory location which caused the fault. */
  segv_addr = _si->si_addr;

  /* Hijack instruction pointer and point it towards a new address that the
   * kernel signal handling mechanism should return to instead of the
   * instruction which originally generated the signal. */
#if 64 == __WORDSIZE
  ((ucontext_t*)_uc)->uc_mcontext.gregs[REG_RIP] = (greg_t)&ooc_async;
#else
  ((ucontext_t*)_uc)->uc_mcontext.gregs[REG_EIP] = (greg_t)&ooc_async;
#endif
}


#ifdef TEST
/* assert */
#include <assert.h>

/* NULL, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdlib.h>

/* setjmp */
#include <setjmp.h>

/* sigaction */
#include <signal.h>

#define SEGV_ADDR (void*)(0x1)

int
main(int argc, char * argv[])
{
  int ret;
  struct sigaction act;

  act.sa_sigaction = &ooc_sigsegv;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  ret = setjmp(ret_env);
  if (!ret) {
    *(int*)SEGV_ADDR = 1; /* Raise a SIGSEGV. */
    return EXIT_FAILURE;
  }

  assert(1 == ret);
  assert(SEGV_ADDR == segv_addr);

  return EXIT_SUCCESS;

  if (argc || argv) {}
}
#endif
