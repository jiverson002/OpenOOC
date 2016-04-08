#define _DEFAULT_SOURCE 1

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define MB       (1lu<<20) /* 1MB */
#define MEM_SIZE (2600*MB)

int
main(void)
{
  int ret;
  void * mem;

  mem = mmap(NULL, MEM_SIZE, PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
  if (MAP_FAILED == mem) perror("error1***");
  assert(MAP_FAILED != mem);

  ret = mlockall(MCL_CURRENT);
  if (ret) perror("error2***");
  assert(!ret);

  sleep(UINT_MAX);

  return EXIT_SUCCESS;
}
