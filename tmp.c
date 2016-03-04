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
#define __ooc_void        void __ooc_kern (
#define __ooc_1__(rest)   __ooc_ ## rest )
#define __ooc__           __ooc_1__ (
__ooc__ void mm() )
{
  return;
}
#undef __ooc_kern
#undef __ooc_void
#undef __ooc_1__
#undef __ooc__


#define __ooc_kern(rest)  __ooc_kern_ ## rest
#define __ooc_void        void __ooc_kern (
#define __ooc__(rest)     __ooc_ ## rest )
__ooc__( void mm() )
{
  return;
}
#undef __ooc_kern
#undef __ooc_void
#undef __ooc__


/*#define ooc_for(loop) \
  __ooc_kern_ ## call;*/


/* Application code */

ooc_for (i=0; i<10; ++i) {
  mm(n, p, a, b, c);
}
