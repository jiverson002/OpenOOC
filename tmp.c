#if 0
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
#endif


#if 0
#define __ooc_defn_kern(kern) \
  /*static inline void*/ __ooc_sched_ ## kern(size_t const, void * const);\
  static /*inline*/ void\
  __ooc_sched_ ## kern(size_t const i, void * const args) {\
    ooc_sched(&__ooc_kern_ ## kern, i, args);\
  }\
  void (*kern)(size_t const, void * const)= &__ooc_sched_ ## kern;\
  void __ooc_kern_ ## kern
#define __ooc_defn __ooc_defn_kern

#define __ooc_decl_kern(kern) \
  /*void*/ (*kern)(size_t const, void * const);\
  void __ooc_kern_ ## kern
#endif


#define __ooc_defn_make(scope,kern) \
  static void __ooc_kern_ ## kern(size_t const, void * const);\
  static /* inline */ void __ooc_sched_ ## kern(size_t const, void * const);\
  static /* inline */ void\
  __ooc_sched_ ## kern(size_t const i, void * const args) {\
    ooc_sched(&__ooc_kern_ ## kern, i, args);\
  }\
  scope void (*kern)(size_t const, void * const)= &__ooc_sched_ ## kern;\
  static void __ooc_kern_ ## kern
#define __ooc_defn_args(scope,kern) __ooc_defn_make(scope,kern)
#define __ooc_defn_kern(rest)       rest
#define __ooc_defn_void             __ooc_defn_args( ,__ooc_defn_kern(
#define __ooc_defn_xvoid            __ooc_defn_kern(
#define __ooc_defn_type(rest)       __ooc_defn_x ## rest)
#define __ooc_defn_extern           __ooc_defn_args(extern, __ooc_defn_type(
#define __ooc_defn_static           __ooc_defn_args(static, __ooc_defn_type(
#define __ooc_defn_scope(rest)      __ooc_defn_ ## rest))
#define __ooc_defn                  __ooc_defn_scope

#define __ooc_decl_make(scope,kern) scope void (*kern)
#define __ooc_decl_args(scope,kern) __ooc_decl_make(scope,kern)
#define __ooc_decl_kern(rest)       rest
#define __ooc_decl_void             __ooc_decl_args( ,__ooc_decl_kern(
#define __ooc_decl_xvoid            __ooc_decl_kern(
#define __ooc_decl_type(rest)       __ooc_decl_x ## rest)
#define __ooc_decl_extern           __ooc_decl_args(extern, __ooc_decl_type(
#define __ooc_decl_static           __ooc_decl_args(static, __ooc_decl_type(
#define __ooc_decl_scope(rest)      __ooc_decl_ ## rest))
#define __ooc_decl                  __ooc_decl_scope

__ooc_decl ( void mm )(size_t const i, void * const state);

__ooc_defn ( void mm )(size_t const i, void * const state)
{
  return;
}
#undef __ooc_defn_kern
#undef __ooc_defn
#undef __ooc_decl_kern
#undef __ooc_decl


#if 0
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
\
    for (loops) {

#define ooc(kern) \
      _ret = ooc_sched(&mykernel, ooc1

#define ooc1(i, args) \
      i, args);\
      assert(!_ret);\
    }\
  }\
  {\
    int __ret;\
    __ret = ooc_finalize();\
    assert(!__ret);\
  }

ooc_for (i=0; i<10; ++i) {
  ooc(mykernel)(i, args);
}

ooc_for (i=0; i<10; ++i)
  ooc(mykernel)(i, args);


/*----------------------------------------------------------------------------*/


  args.n = n;
  args.p = p;
  args.a = a;
  args.b = b;
  args.c = c;

# pragma omp parallel
  {
    OOC_INIT

#   pragma omp for
    for (i=0; i<10; ++i) {
      OOC_CALL(mykernel)(i, args);
    }

    OOC_FINAL
  }
#endif
