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


/*
 *  TODO
 *    Implement a unified API wrapper around POSIX AIO (<aio.h>), native kernel
 *    AIO (<libaio.h>), so that the desired library can be chosen at compile
 *    time.
 */

#if defined(WITH_NATIVE_AIO)
  /* syscall, __NR_* */
  #include <sys/syscall.h>
#elif defined(WITH_POSIX_AIO)
  /* aio_read, aio_write, aio_return, aio_error, aio_cancel */
  #include <aio.h>

  /* memset */
  #include <string.h>
#else
  /* assert */
  #include <assert.h>

  /* EINPROGRESS */
  #include <errno.h>

  /* pthread_create, pthread_join */
  #include <pthread.h>

  /* sem_t, sem_wait, sem_post, sem_init, sem_destroy */
  #include <semaphore.h>

  /* mprotect, PROT_READ, PROT_WRITE */
  #include <sys/mman.h>


  /*! Maximum number of outstanding async-i/o requests. */
  #define AIO_MAX_REQS 2048


  /* Forward declaration. */
  struct ooc_aioreq;


  /*! Asynchronous i/o request queue. */
  struct ooc_aioq {
    int head;
    int tail;
    sem_t full;                               /*!< Indicator of full queue. */
    sem_t empty;                              /*!< Indicator of empty queue. */
    struct ooc_aioreq * aioreq[AIO_MAX_REQS]; /*!< Array of requests. */
  };

  /*! Asynchronous i/o thread state. */
  struct ooc_aioargs {
    struct ooc_aioq * oq;
    struct ooc_aioq * cq;
  };

  /*! Dummy variable to force page in core. */
  static volatile char dummy;

  /*! Asynchronous i/o thread. */
  static __thread pthread_t S_aiothread;

  /*! Outstanding work queue. */
  static __thread struct ooc_aioq S_oq;

  /*! Completed work queue. */
  static __thread struct ooc_aioq S_cq;

  /*! Args to be passed to async i/o thread. */
  static __thread struct ooc_aioargs S_args;
#endif

/* */
#include "common.h"


#undef aioctx
#undef aioctx_t
#undef aioreq
#undef aioreq_t
#undef aio_setup
#undef aio_destroy
#undef aio_read
#undef aio_write
#undef aio_error
#undef aio_return
#undef aio_suspend


#if !defined(WITH_NATIVE_AIO) && !defined(WITH_POSIX_AIO)
static int
S_q_setup(struct ooc_aioq * const q)
{
  int ret;

  q->head = 0;
  q->tail = 0;
  ret = sem_init(&(q->empty), 0, 0);
  if (ret) {
    return ret;
  }
  ret = sem_init(&(q->full), 0, AIO_MAX_REQS);
  if (ret) {
    return ret;
  }

  return 0;
}


static int
S_q_destroy(struct ooc_aioq * const q)
{
  int ret;

  ret = sem_destroy(&(q->empty));
  if (ret) {
    return ret;
  }
  ret = sem_destroy(&(q->full));
  if (ret) {
    return ret;
  }

  return 0;
}


static void
S_q_enq(struct ooc_aioq * const q, ooc_aioreq_t const * const aioreq)
{
  int ret;

  /*
    - Wait on full semaphore
    - Enqueue
    - Post to empty semaphore
  */
  ret = sem_wait(&(q->full));
  assert(!ret);
  q->aioreq[q->tail++] = (ooc_aioreq_t*)aioreq;
  if (AIO_MAX_REQS == q->tail) {
    q->tail = 0;
  }
  ret = sem_post(&(q->empty));
  assert(!ret);
}


static void
S_q_deq(struct ooc_aioq * const q, ooc_aioreq_t ** const aioreq)
{
  int ret;

  /*
    - Wait on empty semaphore
    - Dequeue
    - Post to full semaphore
  */
  ret = sem_wait(&(q->empty));
  assert(!ret);
  *aioreq = q->aioreq[q->head++];
  if (AIO_MAX_REQS == q->head) {
    q->head = 0;
  }
  ret = sem_post(&(q->full));
  assert(!ret);
}


static void *
S_aiothread_func(void * const state)
{
  int ret;
  unsigned char incore;
  unsigned long ps;
  ooc_aioreq_t * aioreq;
  struct ooc_aioargs * args;
  struct ooc_aioq * oq, * cq;

  /* Get system page size */
  ps = (uintptr_t)OOC_PAGE_SIZE;

  dbg_printf("[%5d.aio]   Async I/O thread alive\n", (int)syscall(SYS_gettid));

  args = (struct ooc_aioargs*)state;
  oq = args->oq;
  cq = args->cq;

  for (;;) {
    /* Dequeue outstanding request. */
    S_q_deq(oq, &aioreq);

    /* Process request. */
    /* FIXME POC */
    switch (aioreq->aio_op) {
      case 0:
      /* Async I/O thread will give the buffer read/write access. */
      ret = mprotect(aioreq->aio_buf, aioreq->aio_count, PROT_READ|PROT_WRITE);
      assert(!ret);

      /* Brute force the kernel to page fault the buffer into core. */
      dummy = *(char volatile*)aioreq->aio_buf;

      /* Sanity check... */
      assert(!mincore(aioreq->aio_buf, ps, &incore) && incore);

      /* Update request status. */
      aioreq->aio_error = 0;
      break;

      case 1:
      break;
    }

    /* Enqueue completed request. */
    S_q_enq(cq, aioreq);
  }

  return NULL;
}
#endif


int
ooc_aio_setup(unsigned int const nr, ooc_aioctx_t * const ctx)
{
  int ret;

#if defined(WITH_NATIVE_AIO)
  ret = syscall(__NR_io_setup, nr, ctx);
#elif defined(WITH_POSIX_AIO)
  ret = 0;

  if (nr || ctx) {}
#else
  dbg_printf("[%5d.***] Setting up async i/o context\n",\
    (int)syscall(SYS_gettid));

  ret = S_q_setup(&S_oq);
  if (ret) {
    return ret;
  }
  ret = S_q_setup(&S_cq);
  if (ret) {
    return ret;
  }

  S_args.oq = &S_oq;
  S_args.cq = &S_cq;
  ret = pthread_create(&S_aiothread, NULL, &S_aiothread_func, &S_args);
  if (ret) {
    return ret;
  }

  if (nr || ctx) {}
#endif

  return ret;
}


int
ooc_aio_destroy(ooc_aioctx_t ctx)
{
  int ret;

#if defined(WITH_NATIVE_AIO)
  ret = syscall(__NR_io_destroy, ctx);
#elif defined(WITH_POSIX_AIO)
  ret = 0;

  if (ctx) {}
#else
  dbg_printf("[%5d.***] Destroying async i/o context\n",\
    (int)syscall(SYS_gettid));

  /* FIXME If the queues get destroyed before async i/o thread is killed, it
   * could have undefined behavior when interacting with the queues semaphores.
   * */
  ret = S_q_destroy(&S_oq);
  if (ret) {
    return ret;
  }
  ret = S_q_destroy(&S_cq);
  if (ret) {
    return ret;
  }

  /* FIXME Send S_aiothread some type of cancel message. */
  /*ret = pthread_join(S_aiothread, NULL);
  if (ret) {
    return ret;
  }*/

  if (ctx) {}
#endif

  return ret;
}


int
ooc_aio_read(void * const buf, size_t const count, ooc_aioreq_t * const aioreq)
{
  int ret;

#if defined(WITH_NATIVE_AIO)
  /* FIXME Enqueue page read. */
  ret = -1;

  if (buf || count || aioreq) {}
#elif defined(WITH_POSIX_AIO)
  memset(aioreq, 0, sizeof(*aioreq));

  /* TODO: Need to compute fd and off */
  aioreq->aio_fildes = fd;
  aioreq->aio_offset = off;
  aioreq->aio_buf = buf;
  aioreq->aio_nbytes = count;
  aioreq->aio_reqprio = 0; /* Could use lower priority to prevent io thread from
                              stealing CPU time from compute threads. */
  aioreq->aio_sigevent.sigev_notify = SIGEV_NONE;

  ret = aio_read(aioreq);
#else
  aioreq->aio_buf = buf;
  aioreq->aio_count = count;
  aioreq->aio_error = EINPROGRESS;
  aioreq->aio_op = 0;

  /* Enqueue page read. */
  S_q_enq(&S_oq, aioreq);

  dbg_printf("[%5d.***]   Read request enqueued\n", (int)syscall(SYS_gettid));

  ret = 0;
#endif

  return ret;
}


int
ooc_aio_write(void const * const buf, size_t const count,
              ooc_aioreq_t * const aioreq)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  /* FIXME Enqueue page write. */
  ret = -1;

  if (buf || count || aioreq) {}
#elif defined(WITH_POSIX_AIO)
  memset(aioreq, 0, sizeof(*aioreq));

  /* TODO: Need to compute fd and off */
  aioreq->aio_fildes = fd;
  aioreq->aio_offset = off;
  aioreq->aio_buf = (void*)buf;
  aioreq->aio_nbytes = count;
  aioreq->aio_reqprio = 0; /* Could use lower priority to prevent io thread from
                              stealing CPU time from compute threads. */
  aioreq->aio_sigevent.sigev_notify = SIGEV_NONE;

  ret = aio_write(aioreq);
#else
  aioreq->aio_buf = (void*)buf;
  aioreq->aio_count = count;
  aioreq->aio_error = EINPROGRESS;
  aioreq->aio_op = 1;

  /* Enqueue page write. */
  S_q_enq(&S_oq, aioreq);

  /* FIXME Not fully implemented. */
  ret = -1;
#endif

  return ret;
}


int
ooc_aio_error(ooc_aioreq_t * const aioreq)
{
  int ret;

#if defined(WITH_NATIVE_AIO)
  /* FIXME Check completed status of request. */
  ret = -1;

  if (aioreq) {}
#elif defined(WITH_POSIX_AIO)
  ret = aio_error(aioreq);
#else
  if (0 == aioreq->aio_count) {
    ret = -1;
    errno = EINVAL;
  }
  else {
    ret = aioreq->aio_error;
  }
#endif

  return ret;
}


ssize_t
ooc_aio_return(ooc_aioreq_t * const aioreq)
{
  ssize_t ret;

#if defined(WITH_NATIVE_AIO)
  /* FIXME Do something. */
  ret = -1;

  if (aioreq) {}
#elif defined(WITH_POSIX_AIO)
  ret = aio_return(aioreq);
#else
  if (aioreq->aio_error) {
    ret = aioreq->aio_error;
  }
  else {
    ret = (ssize_t)aioreq->aio_count;
  }
  aioreq->aio_count = 0; /* Reset aioreq. */
#endif

  return ret;
}


ooc_aioreq_t *
ooc_aio_suspend(void)
{
  ooc_aioreq_t * retval;

#if defined(WITH_NATIVE_AIO)
  /* FIXME Do something. */
  ret = -1;
#elif defined(WITH_POSIX_AIO)
  /* FIXME aioreq_list should be the queue and nr its size. */
  ret = aio_suspend(aioreq_list, (int)nr, timeout);
#else
  /* Dequeue completed request. */
  S_q_deq(&S_cq, &retval);
#endif

  return retval;
}


#ifdef TEST
/* EXIT_SUCCESS */
#include <stdlib.h>

int
main(int argc, char * argv[])
{
  return EXIT_SUCCESS;

  if (argc || argv) {}
}
#endif
