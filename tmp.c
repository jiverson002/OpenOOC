

#define __ooc_kern(rest)  __ooc_kern_ ## rest
#define __ooc_void        void __ooc_kern (
#define __ooc_1__(rest)   __ooc_ ## rest
#define __ooc_2__(rest)   __ooc_1__ rest )
#define __ooc__(rest)     __ooc_2__ rest
__ooc__((( void mm() )))
{
  return;
}
#undef __ooc_kern
#undef __ooc_void
#undef __ooc_1__
#undef __ooc_2__
#undef __ooc__


#define __ooc_kern(rest)  __ooc_kern_ ## rest
#define __ooc_void        void __ooc_kern(
#define __ooc_1__(rest)   __ooc_ ## rest)
#define __ooc__           __ooc_1__(
__ooc__ void mm(void) )
{
  return;
}
#undef __ooc_kern
#undef __ooc_void
#undef __ooc_1__
#undef __ooc__


#define __ooc_kern(rest)  __ooc_kern_ ## rest
#define __ooc_void        void __ooc_kern(
#define __ooc_1__(rest)   __ooc_ ## rest)
#define __ooc__           __ooc_1__
__ooc__ ( void mm(void) )
{
  return;
}
#undef __ooc_kern
#undef __ooc_void
#undef __ooc_1__
#undef __ooc__


#define __ooc__
#define ooc_for(loop) \
  for (loop) {
#define ooc_rof \
  }

/* Application code */

__ooc__ void mm(void)
{
  return;
}

ooc_for (i=0; i<m; ++i)
{
  mm(n, p, a, b, c);
}
ooc_rof


#define ooc _Pragma("omp parallel for num_threads(1)")

ooc

#undef ooc
#undef ooc_for


/*----------------------------------------------------------------------------*/


#define ooc_for(loops) \
  {\
    int _ret;\
    _ret = ooc_init();\
    assert(!_ret);\
  }\
\
  for (loops)

#define ooc(kern) \
  {\
    int _ret;\
    _ret = ooc_sched(&mykernel, ooc1

#define ooc1(i, args) \
    i, args);\
    assert(!_ret);\
  }

ooc_for (i=0; i<10; ++i) {
  ooc(mykernel)(i, args);
}
