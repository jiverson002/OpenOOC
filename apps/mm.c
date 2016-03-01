#include <stdlib.h>


#define restrict


static void
mm(
  size_t const m,
  size_t const n,
  size_t const p,
  double const * const restrict a,
  double const * const restrict b,
  double * const restrict c
);


static void
mm(
  size_t const m,
  size_t const n,
  size_t const p,
  double const * const restrict a,
  double const * const restrict b,
  double * const restrict c
)
{
  size_t i, j, k;

#define a(R,C) a[R*n+C]
#define b(R,C) b[R*p+C]
#define c(R,C) c[R*p+C]

//#pragma omp parallel for schedule(static) default(none) private(j,k)
  for (i=0; i<m; ++i) {
    for (j=0; j<n; ++j) {
      c(i,j) = a(i,0)*b(0,j);
      for (k=1; k<p; ++k) {
        c(i,j) += a(i,k)*b(k,j);
      }
    }
  }

#undef a
#undef b
#undef c
}


int
main(
  int argc,
  char * argv[]
)
{
  if (0 == argc || (void*)0 == argv) {}

  return EXIT_SUCCESS;
}
