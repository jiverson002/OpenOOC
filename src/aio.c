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


/* TODO Implement a unified API wrapper around POSIX AIO (<aio.h>) and native
 *      kernel AIO (<libaio.h>) implementations, so that the desired library can
 *      be chosen at compile time. */


#ifdef WITH_NATIVE_AIO
/* syscall, __NR_* */
#include <sys/syscall.h>
#else
/* aio_read, aio_write, aio_return, aio_error, aio_cancel */
#include <aio.h>
#endif

/* memset */
#include <string.h>

/* ooc_aioctx_t, ooc_aioreq_t */
#include "src/ooc.h"


int
ooc_aio_setup(unsigned int const nr, ooc_aioctx_t * const ctx)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = syscall(__NR_io_setup, nr, ctx);
#else
  ret = 0;

  if (nr || ctx) {}
#endif

  return ret;
}


int
ooc_aio_destroy(ooc_aioctx_t ctx)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = syscall(__NR_io_destroy, ctx);
#else
  ret = 0;

  if (ctx) {}
#endif

  return ret;
}


int
ooc_aio_read(int const fd, void * const buf, size_t const count,
             off_t const off, ooc_aioreq_t * const aioreq)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (fd || buf || count || aioreq) {}
#else
  memset(aioreq, 0, sizeof(*aioreq));

  aioreq->aio_fildes = fd;
  aioreq->aio_offset = off;
  aioreq->aio_buf = buf;
  aioreq->aio_nbytes = count;
  aioreq->aio_reqprio = 0; /* Could use lower priority to prevent io thread from
                              stealing CPU time from compute threads. */
  aioreq->aio_sigevent.sigev_notify = SIGEV_NONE;

  ret = aio_read(aioreq);
#endif

  return ret;
}


int
ooc_aio_write(int const fd, void const * const buf, size_t const count,
              off_t const off, ooc_aioreq_t * const aioreq)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (fd || buf || count || aioreq) {}
#else
  memset(aioreq, 0, sizeof(*aioreq));

  aioreq->aio_fildes = fd;
  aioreq->aio_offset = off;
  aioreq->aio_buf = (void*)buf;
  aioreq->aio_nbytes = count;
  aioreq->aio_reqprio = 0; /* Could use lower priority to prevent io thread from
                              stealing CPU time from compute threads. */
  aioreq->aio_sigevent.sigev_notify = SIGEV_NONE;

  ret = aio_write(aioreq);
#endif

  return ret;
}


int
ooc_aio_error(ooc_aioreq_t * const aioreq)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (aioreq) {}
#else
  ret = aio_error(aioreq);
#endif

  return ret;
}


ssize_t
ooc_aio_return(ooc_aioreq_t * const aioreq)
{
  ssize_t ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (aioreq) {}
#else
  ret = aio_return(aioreq);
#endif

  return ret;
}


int
ooc_aio_cancel(ooc_aioreq_t * const aioreq)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (fd || aioreq) {}
#else
  ret = aio_cancel(aioreq->aio_fildes, aioreq);
#endif

  return ret;
}


int
ooc_aio_suspend(ooc_aioreq_t const ** const aioreq_list, unsigned int const nr,
                struct timespec const * const timeout)
{
  int ret;

#ifdef WITH_NATIVE_AIO
  ret = -1;

  if (aioreq_list || nr || timeout) {}
#else
  ret = aio_suspend(aioreq_list, (int)nr, timeout);
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
