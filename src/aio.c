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

  /* EAGAIN */
  #include <errno.h>

  /* pthread_create, pthread_join */
  #include <pthread.h>

  /* sem_t, sem_wait, sem_post, sem_init, sem_destroy */
  #include <semaphore.h>

  /* FIXME POC memcmp */
  #include <string.h>


  /*! Maximum number of outstanding async-i/o requests. */
  #define AIO_MAX_REQS 2048


  /* Forward declaration. */
  struct ooc_aioreq;


  /*! Asynchronous i/o thread. */
  static __thread pthread_t S_aiothread;

  /*! Asynchronous i/o request queue. */
  static __thread struct {
    sem_t num_aioos;                          /*!< Number of outstanding aio requests. */
    sem_t full;                               /*!< Indicator of full queue. */
    sem_t empty;                              /*!< Indicator of empty queue. */
    struct ooc_aioreq * aioreq[AIO_MAX_REQS]; /*!< Array of requests. */
  } S_q;
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
static void *
S_aiothread_func(void * const unused)
{
  int ret;
  aioreq_t * aioreq;

  for (;;) {
    /*
      - Wait on empty semaphore
      - Lock queue
      - Dequeue
      - Unlock queue
      - Post to full semaphore
    */
    ret = sem_wait(&(S_q.empty));
    assert(!ret);
    /* Lock queue */
    /* Dequeue */
    /* Unlock queue */
    ret = sem_post(&(S_q.full));
    assert(!ret);

    /*
      - Process request
    */
    /* FIXME POC */
    /*switch (aioreq->aio_op) {
      case 0:
      ret = memcmp(aioreq->buf, zpage, S_ps);
      aioreq->error = 0;
      break;
      case 1:
      break;
    }*/
  }

  return NULL;

  if (unused) { (void)0; }
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
  ret = pthread_create(&S_aiothread, NULL, &S_aiothread_func, NULL);

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
  ret = pthread_join(S_aiothread, NULL);

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
  aioreq->aio_error = EAGAIN;
  aioreq->aio_op = 0;

  /* TODO Enqueue page read. */
  /*
    - Wait on full semaphore
    - Lock queue
    - Enqueue
    - Unlock queue
    - Post to empty semaphore
  */

  /* FIXME Not fully implemented. */
  ret = -1;
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
  aioreq->aio_error = EAGAIN;
  aioreq->aio_op = 1;

  /* TODO Enqueue page write. */
  /*
    - Wait on full semaphore
    - Lock queue
    - Enqueue
    - Unlock queue
    - Post to empty semaphore
  */

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
  /* FIXME Check completed status of request. */
  ret = aioreq->aio_error;
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
#endif

  return ret;
}


int
ooc_aio_suspend(void)
{
  int ret;

#if defined(WITH_NATIVE_AIO)
  /* FIXME Do something. */
  ret = -1;
#elif defined(WITH_POSIX_AIO)
  /* FIXME aioreq_list should be the queue and nr its size. */
  ret = aio_suspend(aioreq_list, (int)nr, timeout);
#else
  /* Wait for a completed request. */
  ret = sem_wait(&(S_q.num_aioos));
  if (-1 == ret) {
    return ret;
  }

  /* TODO Iterate queue and find completed request. */
  /*
    - lock queue
    - iterate queue and get pointer to completed request
    - unlock queue
  */

  /* FIXME Not fully implemented. */
  ret = -1;
#endif

  return ret;
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
