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


#ifndef OPENOOC_H
#define OPENOOC_H


#define OPENOOC_MAJOR 0
#define OPENOOC_MINOR 0
#define OPENOOC_PATCH 0
#define OPENOOC_RCAND -pre


/* assert */
#include <assert.h>

/* SIGSEGV */
#include <errno.h>

/* siginfo_t, sigaction */
#include <signal.h>

/* memset */
#include <string.h>


/*  sigsegv function prototypes. */
void ooc_sigsegv_trampoline(int const, siginfo_t * const, void * const);


/* The OOC black magic. */
#define OOC_FOR(loops) \
  {\
    int _ret;\
    struct sigaction _new, _old;\
\
    memset(&_new, 0, sizeof(_new));\
    _new.sa_sigaction = &ooc_sigsegv_trampoline;\
    _new.sa_flags = SA_SIGINFO;\
    _ret = sigaction(SIGSEGV, &_new, &_old);\
    assert(!_ret);\
    for (loops) {
#define OOC_DO \
      {
#define OOC_DONE \
      }\
    }\
    _ret = sigaction(SIGSEGV, &_old, NULL);\
    assert(!_ret);\
  }


#endif /* OPENOOC_H */
