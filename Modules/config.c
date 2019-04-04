/* This file contains the table of built-in modules.
   See create_builtin() in import.c. */

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MS_WINDOWS
extern void PyInit_msvcrt(void);
extern void PyInit_nt(void);
extern void PyInit_winreg(void);
extern void PyInit__winapi(void);
#ifndef MS_WINI64
extern PyObject* PyInit_audioop(void);
#endif
#else
extern PyObject* PyInit_posix(void);
extern PyObject* PyInit_pwd(void);
#endif

extern PyObject* PyInit_array(void);
extern PyObject* PyInit_binascii(void);
extern PyObject* PyInit_cmath(void);
extern PyObject* PyInit_math(void);
extern PyObject* PyInit_array(void);
extern PyObject* PyInit__thread(void);
extern PyObject* PyInit_errno(void);
extern PyObject* PyInit__sre(void);
extern PyObject* PyInit__codecs(void);
extern PyObject* PyInit__weakref(void);
extern PyObject* PyInit__functools(void);
extern PyObject* PyInit__operator(void);
extern PyObject* PyInit__collections(void);
extern PyObject* PyInit_itertools(void);
extern PyObject* PyInit_atexit(void);
extern PyObject* PyInit__signal(void);
extern PyObject* PyInit__stat(void);
extern PyObject* PyInit_time(void);
extern PyObject* PyInit__locale(void);
extern PyObject* PyInit__io(void);
extern PyObject* PyInit_faulthandler(void);
extern PyObject* PyInit__tracemalloc(void);
extern PyObject* PyInit_xxsubtype(void);

extern PyObject* PyInit_mmap(void);
extern PyObject* PyInit__csv(void);
extern PyObject* PyInit__struct(void);
extern PyObject* PyInit__datetime(void);
extern PyObject* PyInit__json(void);
extern PyObject* PyInit__random(void);
extern PyObject* PyInit__bisect(void);
extern PyObject* PyInit__heapq(void);
extern PyObject* PyInit__lsprof(void);
extern PyObject* PyInit_zlib(void);

extern PyObject* PyInit__md5(void);
extern PyObject* PyInit__sha1(void);
extern PyObject* PyInit__sha256(void);
extern PyObject* PyInit__sha512(void);
extern PyObject* PyInit__sha3(void);
extern PyObject* PyInit__blake2(void);

extern PyObject* PyInit__multibytecodec(void);
extern PyObject* PyInit__codecs_cn(void);
extern PyObject* PyInit__codecs_hk(void);
extern PyObject* PyInit__codecs_iso2022(void);
extern PyObject* PyInit__codecs_jp(void);
extern PyObject* PyInit__codecs_kr(void);
extern PyObject* PyInit__codecs_tw(void);

extern PyObject* PyMarshal_Init(void);
extern PyObject* PyInit_imp(void);
extern PyObject* PyInit_gc(void);
extern PyObject* _PyWarnings_Init(void);
extern PyObject* PyInit__string(void);

struct _inittab _PyImport_Inittab[] = {

#ifdef MS_WINDOWS
    {"msvcrt", PyInit_msvcrt},
    {"nt", PyInit_nt},
    {"_winapi", PyInit__winapi},
    {"winreg", PyInit_winreg},
#ifndef MS_WINI64
    {"audioop", PyInit_audioop},
#endif
#else
    {"posix", PyInit_posix},
    {"pwd", PyInit_pwd},
#endif

    {"errno", PyInit_errno},
    {"array", PyInit_array},
    {"binascii", PyInit_binascii},
    {"cmath", PyInit_cmath},
    {"math", PyInit_math},
    {"_md5", PyInit__md5},
    {"_sha1", PyInit__sha1},
    {"_sha256", PyInit__sha256},
    {"_sha512", PyInit__sha512},
    {"_sha3", PyInit__sha3},
    {"_blake2", PyInit__blake2},

#ifdef WITH_THREAD
    {"_thread", PyInit__thread},
#endif

    {"_sre", PyInit__sre},
    {"_codecs", PyInit__codecs},
    {"_weakref", PyInit__weakref},
    {"_functools", PyInit__functools},
    {"_operator", PyInit__operator},
    {"_collections", PyInit__collections},
    {"itertools", PyInit_itertools},
    {"atexit", PyInit_atexit},
    {"_signal", PyInit__signal},
    {"_stat", PyInit__stat},
    {"time", PyInit_time},
    {"_locale", PyInit__locale},
    {"_io", PyInit__io},
    {"faulthandler", PyInit_faulthandler},
    {"_tracemalloc", PyInit__tracemalloc},
    {"xxsubtype", PyInit_xxsubtype},


    /* This module lives in marshal.c */
    {"marshal", PyMarshal_Init},

    /* This lives in import.c */
    {"_imp", PyInit_imp},

    /* These entries are here for sys.builtin_module_names */
    {"builtins", NULL},
    {"sys", NULL},

    /* This lives in gcmodule.c */
    {"gc", PyInit_gc},

    /* This lives in _warnings.c */
    {"_warnings", _PyWarnings_Init},

    /* This lives in Objects/unicodeobject.c */
    {"_string", PyInit__string},

    {"zlib", PyInit_zlib},

    {"_random", PyInit__random},
    {"_bisect", PyInit__bisect},
    {"_heapq", PyInit__heapq},
    {"_lsprof", PyInit__lsprof},
    {"mmap", PyInit_mmap},
    {"_csv", PyInit__csv},
    {"_struct", PyInit__struct},
    {"_datetime", PyInit__datetime},
    {"_json", PyInit__json},

    /* CJK codecs */
    {"_multibytecodec", PyInit__multibytecodec},
    {"_codecs_cn", PyInit__codecs_cn},
    {"_codecs_hk", PyInit__codecs_hk},
    {"_codecs_iso2022", PyInit__codecs_iso2022},
    {"_codecs_jp", PyInit__codecs_jp},
    {"_codecs_kr", PyInit__codecs_kr},
    {"_codecs_tw", PyInit__codecs_tw},

    /* Sentinel */
    {0, 0}
};


#ifdef __cplusplus
}
#endif
