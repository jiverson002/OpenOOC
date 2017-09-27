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

/* omp_get_wtime */
#include <omp.h>

/* fopen, fclose, fscanf, printf */
#include <stdio.h>

/* mmap, MAP_FAILED, MAP_PRIVATE, MAP_ANONYMOUS, MAP_LOCKED */
#include <sys/mman.h>

/* getrlimit */
#include <sys/resource.h>

/* getrlimit */
#include <sys/time.h>

/* ctime, time_t */
#include <time.h>


#define XSTR(X) #X
#define STR(X)  XSTR(X)

#define KB (1lu<<10) /* 1KB */
#define MB (1lu<<20) /* 1MB */
#define GB (1lu<<30) /* 1GB */

#define UNUSED(var) (void)(var)


static void
S_gettime(double * const t)
{
  *t = omp_get_wtime();
}


static double
S_getelapsed(double const * const ts, double * const te)
{
  return *te-*ts;
}


static int
S_getpagecluster(void)
{
  int ret, c_size;
  FILE * fp;

  fp = fopen("/proc/sys/vm/page-cluster", "r");
  assert(fp);

  ret = fscanf(fp, "%d", &c_size);
  assert(1 == ret);

  ret = fclose(fp);
  assert(!ret);

  return c_size;
}


static void __attribute__((unused))
S_fill_dram(void)
{
  size_t n_bytes=0;

  /* Allocate GBs. */
  for (;;) {
    if (MAP_FAILED == mmap(NULL, GB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0))
    {
      break;
    }
    n_bytes += GB;
  }

  /* Allocate MBs. */
  for (;;) {
    if (MAP_FAILED == mmap(NULL, MB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0))
    {
      break;
    }
    n_bytes += MB;
  }

  /* Allocate 8*KBs. */
  /*for (;;) {
    if (MAP_FAILED == mmap(NULL, 8*KB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0))
    {
      break;
    }
  }*/

  printf("  Filled with %.2f MiB...\n", (double)n_bytes/1048576.0);
}


static void
S_printbuildinfo(time_t const * const now, size_t const p_size,
                 size_t const c_size)
{
  int ret;
  struct rlimit rlim;

  ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
  assert(!ret);

  /* Output build info. */
  printf("===============================\n");
  printf(" General ======================\n");
  printf("===============================\n");
  printf("  Machine info    = %s\n", STR(UNAME));
  printf("  GCC version     = %s\n", STR(GCCV));
  printf("  Build date      = %s\n", STR(DATE));
  printf("  Run date        = %s", ctime(now));
  printf("  Git commit      = %11s\n", STR(COMMIT));
  printf("  PageCluster (B) = %11zu\n", c_size);
  printf("  SysPage (B)     = %11zu\n", p_size);
  printf("  Memlock Max (B) = %11lu\n", (long unsigned)rlim.rlim_max);
  printf("\n");
}
