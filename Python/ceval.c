
/* Execute compiled code */

/* XXX TO DO:
   XXX speed up searching for keywords by using a dictionary
   XXX document it!
   */

/* enable more aggressive intra-module optimizations, where available */
#define PY_LOCAL_AGGRESSIVE

#include "Python.h"

#include "code.h"
#include "dictobject.h"
#include "frameobject.h"
#include "opcode.h"
#include "setobject.h"
#include "structmember.h"

#include <ctype.h>

#define GETLOCAL(i) fastlocals[i]
#define SETLOCAL(i, value)      do { PyObject *tmp = GETLOCAL(i); \
                                     GETLOCAL(i) = value; \
                                     Py_XDECREF(tmp); } while (0)

/* Dynamic execution profile */
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
static long dxpairs[257][256];
#define dxp dxpairs[256]
#else
static long dxp[256];
#endif
#endif

/* Function call profile */
PyObject *
PyEval_GetCallStats(PyObject *self)
{
    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WITH_THREAD
#define GIL_REQUEST _Py_atomic_load_relaxed(&gil_drop_request)
#else
#define GIL_REQUEST 0
#endif

/* This can set eval_breaker to 0 even though gil_drop_request became
   1.  We believe this is all right because the eval loop will release
   the GIL eventually anyway. */
#define COMPUTE_EVAL_BREAKER() \
    _Py_atomic_store_relaxed( \
        &eval_breaker, \
        GIL_REQUEST | \
        _Py_atomic_load_relaxed(&pendingcalls_to_do) | \
        pending_async_exc)

#ifdef WITH_THREAD

#define SET_GIL_DROP_REQUEST() \
    do { \
        _Py_atomic_store_relaxed(&gil_drop_request, 1); \
        _Py_atomic_store_relaxed(&eval_breaker, 1); \
    } while (0)

#define RESET_GIL_DROP_REQUEST() \
    do { \
        _Py_atomic_store_relaxed(&gil_drop_request, 0); \
        COMPUTE_EVAL_BREAKER(); \
    } while (0)

#endif

/* Pending calls are only modified under pending_lock */
#define SIGNAL_PENDING_CALLS() \
    do { \
        _Py_atomic_store_relaxed(&pendingcalls_to_do, 1); \
        _Py_atomic_store_relaxed(&eval_breaker, 1); \
    } while (0)

#define UNSIGNAL_PENDING_CALLS() \
    do { \
        _Py_atomic_store_relaxed(&pendingcalls_to_do, 0); \
        COMPUTE_EVAL_BREAKER(); \
    } while (0)

#define SIGNAL_ASYNC_EXC() \
    do { \
        pending_async_exc = 1; \
        _Py_atomic_store_relaxed(&eval_breaker, 1); \
    } while (0)

#define UNSIGNAL_ASYNC_EXC() \
    do { pending_async_exc = 0; COMPUTE_EVAL_BREAKER(); } while (0)


/* This single variable consolidates all requests to break out of the fast path
   in the eval loop. */
static _Py_atomic_int eval_breaker = {0};
/* Request for running pending calls. */
static _Py_atomic_int pendingcalls_to_do = {0};
/* Request for looking at the `async_exc` field of the current thread state.
   Guarded by the GIL. */
static int pending_async_exc = 0;

#ifdef WITH_THREAD

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "pythread.h"

static PyThread_type_lock pending_lock = 0; /* for pending calls */
static long main_thread = 0;
/* Request for dropping the GIL */
static _Py_atomic_int gil_drop_request = {0};

#include "ceval_gil.h"

int
PyEval_ThreadsInitialized(void)
{
    return gil_created();
}

void
PyEval_InitThreads(void)
{
    if (gil_created())
        return;
    create_gil();
    take_gil(PyThreadState_GET());
    main_thread = PyThread_get_thread_ident();
    if (!pending_lock)
        pending_lock = PyThread_allocate_lock();
}

void
_PyEval_FiniThreads(void)
{
    if (!gil_created())
        return;
    destroy_gil();
    assert(!gil_created());
}

void
PyEval_AcquireLock(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    if (tstate == NULL)
        Py_FatalError("PyEval_AcquireLock: current thread state is NULL");
    take_gil(tstate);
}

void
PyEval_ReleaseLock(void)
{
    /* This function must succeed when the current thread state is NULL.
       We therefore avoid PyThreadState_GET() which dumps a fatal error
       in debug mode.
    */
    drop_gil((PyThreadState*)_Py_atomic_load_relaxed(
        &_PyThreadState_Current));
}

void
PyEval_AcquireThread(PyThreadState *tstate)
{
    if (tstate == NULL)
        Py_FatalError("PyEval_AcquireThread: NULL new thread state");
    /* Check someone has called PyEval_InitThreads() to create the lock */
    assert(gil_created());
    take_gil(tstate);
    if (PyThreadState_Swap(tstate) != NULL)
        Py_FatalError(
            "PyEval_AcquireThread: non-NULL old thread state");
}

void
PyEval_ReleaseThread(PyThreadState *tstate)
{
    if (tstate == NULL)
        Py_FatalError("PyEval_ReleaseThread: NULL thread state");
    if (PyThreadState_Swap(NULL) != tstate)
        Py_FatalError("PyEval_ReleaseThread: wrong thread state");
    drop_gil(tstate);
}

/* This function is called from PyOS_AfterFork to destroy all threads which are
 * not running in the child process, and clear internal locks which might be
 * held by those threads. (This could also be done using pthread_atfork
 * mechanism, at least for the pthreads implementation.) */

void
PyEval_ReInitThreads(void)
{
    _Py_IDENTIFIER(_after_fork);
    PyObject *threading, *result;
    PyThreadState *current_tstate = PyThreadState_GET();

    if (!gil_created())
        return;
    recreate_gil();
    pending_lock = PyThread_allocate_lock();
    take_gil(current_tstate);
    main_thread = PyThread_get_thread_ident();

    /* Update the threading module with the new state.
     */
    threading = PyMapping_GetItemString(current_tstate->interp->modules,
                                        "threading");
    if (threading == NULL) {
        /* threading not imported */
        PyErr_Clear();
        return;
    }
    result = _PyObject_CallMethodId(threading, &PyId__after_fork, NULL);
    if (result == NULL)
        PyErr_WriteUnraisable(threading);
    else
        Py_DECREF(result);
    Py_DECREF(threading);

    /* Destroy all threads except the current one */
    _PyThreadState_DeleteExcept(current_tstate);
}

#endif /* WITH_THREAD */

/* This function is used to signal that async exceptions are waiting to be
   raised, therefore it is also useful in non-threaded builds. */

void
_PyEval_SignalAsyncExc(void)
{
    SIGNAL_ASYNC_EXC();
}

/* Functions save_thread and restore_thread are always defined so
   dynamically loaded modules needn't be compiled separately for use
   with and without threads: */

PyThreadState *
PyEval_SaveThread(void)
{
    PyThreadState *tstate = PyThreadState_Swap(NULL);
    if (tstate == NULL)
        Py_FatalError("PyEval_SaveThread: NULL tstate");
#ifdef WITH_THREAD
    if (gil_created())
        drop_gil(tstate);
#endif
    return tstate;
}

void
PyEval_RestoreThread(PyThreadState *tstate)
{
    if (tstate == NULL)
        Py_FatalError("PyEval_RestoreThread: NULL tstate");
#ifdef WITH_THREAD
    if (gil_created()) {
        int err = errno;
        take_gil(tstate);
        /* _Py_Finalizing is protected by the GIL */
        if (_Py_Finalizing && tstate != _Py_Finalizing) {
            drop_gil(tstate);
            PyThread_exit_thread();
            assert(0);  /* unreachable */
        }
        errno = err;
    }
#endif
    PyThreadState_Swap(tstate);
}


/* Mechanism whereby asynchronously executing callbacks (e.g. UNIX
   signal handlers or Mac I/O completion routines) can schedule calls
   to a function to be called synchronously.
   The synchronous function is called with one void* argument.
   It should return 0 for success or -1 for failure -- failure should
   be accompanied by an exception.

   If registry succeeds, the registry function returns 0; if it fails
   (e.g. due to too many pending calls) it returns -1 (without setting
   an exception condition).

   Note that because registry may occur from within signal handlers,
   or other asynchronous events, calling malloc() is unsafe!

#ifdef WITH_THREAD
   Any thread can schedule pending calls, but only the main thread
   will execute them.
   There is no facility to schedule calls to a particular thread, but
   that should be easy to change, should that ever be required.  In
   that case, the static variables here should go into the python
   threadstate.
#endif
*/

void
_PyEval_SignalReceived(void)
{
    /* bpo-30703: Function called when the C signal handler of Python gets a
       signal. We cannot queue a callback using Py_AddPendingCall() since
       that function is not async-signal-safe. */
    SIGNAL_PENDING_CALLS();
}

#ifdef WITH_THREAD

/* The WITH_THREAD implementation is thread-safe.  It allows
   scheduling to be made from any thread, and even from an executing
   callback.
 */

#define NPENDINGCALLS 32
static struct {
    int (*func)(void *);
    void *arg;
} pendingcalls[NPENDINGCALLS];
static int pendingfirst = 0;
static int pendinglast = 0;

int
Py_AddPendingCall(int (*func)(void *), void *arg)
{
    int i, j, result=0;
    PyThread_type_lock lock = pending_lock;

    /* try a few times for the lock.  Since this mechanism is used
     * for signal handling (on the main thread), there is a (slim)
     * chance that a signal is delivered on the same thread while we
     * hold the lock during the Py_MakePendingCalls() function.
     * This avoids a deadlock in that case.
     * Note that signals can be delivered on any thread.  In particular,
     * on Windows, a SIGINT is delivered on a system-created worker
     * thread.
     * We also check for lock being NULL, in the unlikely case that
     * this function is called before any bytecode evaluation takes place.
     */
    if (lock != NULL) {
        for (i = 0; i<100; i++) {
            if (PyThread_acquire_lock(lock, NOWAIT_LOCK))
                break;
        }
        if (i == 100)
            return -1;
    }

    i = pendinglast;
    j = (i + 1) % NPENDINGCALLS;
    if (j == pendingfirst) {
        result = -1; /* Queue full */
    } else {
        pendingcalls[i].func = func;
        pendingcalls[i].arg = arg;
        pendinglast = j;
    }
    /* signal main loop */
    SIGNAL_PENDING_CALLS();
    if (lock != NULL)
        PyThread_release_lock(lock);
    return result;
}

int
Py_MakePendingCalls(void)
{
    static int busy = 0;
    int i;
    int r = 0;

    assert(PyGILState_Check());

    if (!pending_lock) {
        /* initial allocation of the lock */
        pending_lock = PyThread_allocate_lock();
        if (pending_lock == NULL)
            return -1;
    }

    /* only service pending calls on main thread */
    if (main_thread && PyThread_get_thread_ident() != main_thread)
        return 0;
    /* don't perform recursive pending calls */
    if (busy)
        return 0;
    busy = 1;
    /* unsignal before starting to call callbacks, so that any callback
       added in-between re-signals */
    UNSIGNAL_PENDING_CALLS();

    /* Python signal handler doesn't really queue a callback: it only signals
       that a signal was received, see _PyEval_SignalReceived(). */
    if (PyErr_CheckSignals() < 0) {
        goto error;
    }

    /* perform a bounded number of calls, in case of recursion */
    for (i=0; i<NPENDINGCALLS; i++) {
        int j;
        int (*func)(void *);
        void *arg = NULL;

        /* pop one item off the queue while holding the lock */
        PyThread_acquire_lock(pending_lock, WAIT_LOCK);
        j = pendingfirst;
        if (j == pendinglast) {
            func = NULL; /* Queue empty */
        } else {
            func = pendingcalls[j].func;
            arg = pendingcalls[j].arg;
            pendingfirst = (j + 1) % NPENDINGCALLS;
        }
        PyThread_release_lock(pending_lock);
        /* having released the lock, perform the callback */
        if (func == NULL)
            break;
        r = func(arg);
        if (r) {
            goto error;
        }
    }

    busy = 0;
    return r;

error:
    busy = 0;
    SIGNAL_PENDING_CALLS(); /* We're not done yet */
    return -1;
}

#else /* if ! defined WITH_THREAD */

/*
   WARNING!  ASYNCHRONOUSLY EXECUTING CODE!
   This code is used for signal handling in python that isn't built
   with WITH_THREAD.
   Don't use this implementation when Py_AddPendingCalls() can happen
   on a different thread!

   There are two possible race conditions:
   (1) nested asynchronous calls to Py_AddPendingCall()
   (2) AddPendingCall() calls made while pending calls are being processed.

   (1) is very unlikely because typically signal delivery
   is blocked during signal handling.  So it should be impossible.
   (2) is a real possibility.
   The current code is safe against (2), but not against (1).
   The safety against (2) is derived from the fact that only one
   thread is present, interrupted by signals, and that the critical
   section is protected with the "busy" variable.  On Windows, which
   delivers SIGINT on a system thread, this does not hold and therefore
   Windows really shouldn't use this version.
   The two threads could theoretically wiggle around the "busy" variable.
*/

#define NPENDINGCALLS 32
static struct {
    int (*func)(void *);
    void *arg;
} pendingcalls[NPENDINGCALLS];
static volatile int pendingfirst = 0;
static volatile int pendinglast = 0;

int
Py_AddPendingCall(int (*func)(void *), void *arg)
{
    static volatile int busy = 0;
    int i, j;
    /* XXX Begin critical section */
    if (busy)
        return -1;
    busy = 1;
    i = pendinglast;
    j = (i + 1) % NPENDINGCALLS;
    if (j == pendingfirst) {
        busy = 0;
        return -1; /* Queue full */
    }
    pendingcalls[i].func = func;
    pendingcalls[i].arg = arg;
    pendinglast = j;

    SIGNAL_PENDING_CALLS();
    busy = 0;
    /* XXX End critical section */
    return 0;
}

int
Py_MakePendingCalls(void)
{
    static int busy = 0;
    if (busy)
        return 0;
    busy = 1;

    /* unsignal before starting to call callbacks, so that any callback
       added in-between re-signals */
    UNSIGNAL_PENDING_CALLS();
    /* Python signal handler doesn't really queue a callback: it only signals
       that a signal was received, see _PyEval_SignalReceived(). */
    if (PyErr_CheckSignals() < 0) {
        goto error;
    }

    for (;;) {
        int i;
        int (*func)(void *);
        void *arg;
        i = pendingfirst;
        if (i == pendinglast)
            break; /* Queue empty */
        func = pendingcalls[i].func;
        arg = pendingcalls[i].arg;
        pendingfirst = (i + 1) % NPENDINGCALLS;
        if (func(arg) < 0) {
            goto error;
        }
    }
    busy = 0;
    return 0;

error:
    busy = 0;
    SIGNAL_PENDING_CALLS(); /* We're not done yet */
    return -1;
}

#endif /* WITH_THREAD */


/* The interpreter's recursion limit */

#ifndef Py_DEFAULT_RECURSION_LIMIT
#define Py_DEFAULT_RECURSION_LIMIT 1000
#endif
static int recursion_limit = Py_DEFAULT_RECURSION_LIMIT;
int _Py_CheckRecursionLimit = Py_DEFAULT_RECURSION_LIMIT;

int
Py_GetRecursionLimit(void)
{
    return recursion_limit;
}

void
Py_SetRecursionLimit(int new_limit)
{
    recursion_limit = new_limit;
    _Py_CheckRecursionLimit = recursion_limit;
}

/* the macro Py_EnterRecursiveCall() only calls _Py_CheckRecursiveCall()
   if the recursion_depth reaches _Py_CheckRecursionLimit.
   If USE_STACKCHECK, the macro decrements _Py_CheckRecursionLimit
   to guarantee that _Py_CheckRecursiveCall() is regularly called.
   Without USE_STACKCHECK, there is no need for this. */
int
_Py_CheckRecursiveCall(const char *where)
{
    PyThreadState *tstate = PyThreadState_GET();

#ifdef USE_STACKCHECK
    if (PyOS_CheckStack()) {
        --tstate->recursion_depth;
        PyErr_SetString(PyExc_MemoryError, "Stack overflow");
        return -1;
    }
#endif
    _Py_CheckRecursionLimit = recursion_limit;
    if (tstate->recursion_critical)
        /* Somebody asked that we don't check for recursion. */
        return 0;
    if (tstate->overflowed) {
        if (tstate->recursion_depth > recursion_limit + 50) {
            /* Overflowing while handling an overflow. Give up. */
            Py_FatalError("Cannot recover from stack overflow.");
        }
        return 0;
    }
    if (tstate->recursion_depth > recursion_limit) {
        --tstate->recursion_depth;
        tstate->overflowed = 1;
        PyErr_Format(PyExc_RecursionError,
                     "maximum recursion depth exceeded%s",
                     where);
        return -1;
    }
    return 0;
}

static void save_exc_state(PyThreadState *, PyFrameObject *);
static void swap_exc_state(PyThreadState *, PyFrameObject *);
static void restore_and_clear_exc_state(PyThreadState *, PyFrameObject *);

/* Records whether tracing is on for any thread.  Counts the number of
   threads for which tstate->c_tracefunc is non-NULL, so if the value
   is 0, we know we don't have to check this thread's c_tracefunc.
   This speeds up the if statement in PyEval_EvalFrameEx() after
   fast_next_opcode*/
static int _Py_TracingPossible = 0;



PyObject *
PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals)
{
    return PyEval_EvalCodeEx(co,
                      globals, locals,
                      (PyObject **)NULL, 0,
                      (PyObject **)NULL, 0,
                      (PyObject **)NULL, 0,
                      NULL, NULL);
}


/* Interpreter main loop */

PyObject *
PyEval_EvalFrame(PyFrameObject *f) {
    /* This is for backward compatibility with extension modules that
       used this API; core interpreter code should call
       PyEval_EvalFrameEx() */
    return PyEval_EvalFrameEx(f, 0);
}

PyObject *
PyEval_EvalFrameEx(PyFrameObject *f, int throwflag)
{
    PyThreadState *tstate = PyThreadState_GET();
    return tstate->interp->eval_frame(f, throwflag);
}

PyObject *
_PyEval_EvalFrameDefault(PyFrameObject *f, int throwflag)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyObject *retval = NULL;            /* Return value */

    /* push frame */
    if (Py_EnterRecursiveCall(""))
        return NULL;

    tstate->frame = f;
    f->f_executing = 1;

    if (f->f_code->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)) {
        if (!throwflag && f->f_exc_type != NULL && f->f_exc_type != Py_None) {
            /* We were in an except handler when we left,
               restore the exception state which was put aside
               (see YIELD_VALUE). */
            swap_exc_state(tstate, f);
        }
        else
            save_exc_state(tstate, f);
    }

    retval = f->f_code->co_meth_ptr(f);

    Py_LeaveRecursiveCall();
    f->f_executing = 0;
    tstate->frame = f->f_back;

    return _Py_CheckFunctionResult(NULL, retval, "PyEval_EvalFrameEx");
}

static void
format_missing(const char *kind, PyCodeObject *co, PyObject *names)
{
    int err;
    Py_ssize_t len = PyList_GET_SIZE(names);
    PyObject *name_str, *comma, *tail, *tmp;

    assert(PyList_CheckExact(names));
    assert(len >= 1);
    /* Deal with the joys of natural language. */
    switch (len) {
    case 1:
        name_str = PyList_GET_ITEM(names, 0);
        Py_INCREF(name_str);
        break;
    case 2:
        name_str = PyUnicode_FromFormat("%U and %U",
                                        PyList_GET_ITEM(names, len - 2),
                                        PyList_GET_ITEM(names, len - 1));
        break;
    default:
        tail = PyUnicode_FromFormat(", %U, and %U",
                                    PyList_GET_ITEM(names, len - 2),
                                    PyList_GET_ITEM(names, len - 1));
        if (tail == NULL)
            return;
        /* Chop off the last two objects in the list. This shouldn't actually
           fail, but we can't be too careful. */
        err = PyList_SetSlice(names, len - 2, len, NULL);
        if (err == -1) {
            Py_DECREF(tail);
            return;
        }
        /* Stitch everything up into a nice comma-separated list. */
        comma = PyUnicode_FromString(", ");
        if (comma == NULL) {
            Py_DECREF(tail);
            return;
        }
        tmp = PyUnicode_Join(comma, names);
        Py_DECREF(comma);
        if (tmp == NULL) {
            Py_DECREF(tail);
            return;
        }
        name_str = PyUnicode_Concat(tmp, tail);
        Py_DECREF(tmp);
        Py_DECREF(tail);
        break;
    }
    if (name_str == NULL)
        return;
    PyErr_Format(PyExc_TypeError,
                 "%U() missing %i required %s argument%s: %U",
                 co->co_name,
                 len,
                 kind,
                 len == 1 ? "" : "s",
                 name_str);
    Py_DECREF(name_str);
}

static void
missing_arguments(PyCodeObject *co, Py_ssize_t missing, Py_ssize_t defcount,
                  PyObject **fastlocals)
{
    Py_ssize_t i, j = 0;
    Py_ssize_t start, end;
    int positional = (defcount != -1);
    const char *kind = positional ? "positional" : "keyword-only";
    PyObject *missing_names;

    /* Compute the names of the arguments that are missing. */
    missing_names = PyList_New(missing);
    if (missing_names == NULL)
        return;
    if (positional) {
        start = 0;
        end = co->co_argcount - defcount;
    }
    else {
        start = co->co_argcount;
        end = start + co->co_kwonlyargcount;
    }
    for (i = start; i < end; i++) {
        if (GETLOCAL(i) == NULL) {
            PyObject *raw = PyTuple_GET_ITEM(co->co_varnames, i);
            PyObject *name = PyObject_Repr(raw);
            if (name == NULL) {
                Py_DECREF(missing_names);
                return;
            }
            PyList_SET_ITEM(missing_names, j++, name);
        }
    }
    assert(j == missing);
    format_missing(kind, co, missing_names);
    Py_DECREF(missing_names);
}

static void
too_many_positional(PyCodeObject *co, Py_ssize_t given, Py_ssize_t defcount,
                    PyObject **fastlocals)
{
    int plural;
    Py_ssize_t kwonly_given = 0;
    Py_ssize_t i;
    PyObject *sig, *kwonly_sig;
    Py_ssize_t co_argcount = co->co_argcount;

    assert((co->co_flags & CO_VARARGS) == 0);
    /* Count missing keyword-only args. */
    for (i = co_argcount; i < co_argcount + co->co_kwonlyargcount; i++) {
        if (GETLOCAL(i) != NULL) {
            kwonly_given++;
        }
    }
    if (defcount) {
        Py_ssize_t atleast = co_argcount - defcount;
        plural = 1;
        sig = PyUnicode_FromFormat("from %zd to %zd", atleast, co_argcount);
    }
    else {
        plural = (co_argcount != 1);
        sig = PyUnicode_FromFormat("%zd", co_argcount);
    }
    if (sig == NULL)
        return;
    if (kwonly_given) {
        const char *format = " positional argument%s (and %zd keyword-only argument%s)";
        kwonly_sig = PyUnicode_FromFormat(format,
                                          given != 1 ? "s" : "",
                                          kwonly_given,
                                          kwonly_given != 1 ? "s" : "");
        if (kwonly_sig == NULL) {
            Py_DECREF(sig);
            return;
        }
    }
    else {
        /* This will not fail. */
        kwonly_sig = PyUnicode_FromString("");
        assert(kwonly_sig != NULL);
    }
    PyErr_Format(PyExc_TypeError,
                 "%U() takes %U positional argument%s but %zd%U %s given",
                 co->co_name,
                 sig,
                 plural ? "s" : "",
                 given,
                 kwonly_sig,
                 given == 1 && !kwonly_given ? "was" : "were");
    Py_DECREF(sig);
    Py_DECREF(kwonly_sig);
}

/* This is gonna seem *real weird*, but if you put some other code between
   PyEval_EvalFrame() and _PyEval_EvalFrameDefault() you will need to adjust
   the test in the if statements in Misc/gdbinit (pystack and pystackv). */

static PyObject *
_PyEval_EvalCodeWithName(PyObject *_co, PyObject *globals, PyObject *locals,
           PyObject **args, Py_ssize_t argcount,
           PyObject **kwnames, PyObject **kwargs,
           Py_ssize_t kwcount, int kwstep,
           PyObject **defs, Py_ssize_t defcount,
           PyObject *kwdefs, PyObject *closure,
           PyObject *name, PyObject *qualname)
{
    PyCodeObject* co = (PyCodeObject*)_co;
    PyFrameObject *f;
    PyObject *retval = NULL;
    PyObject **fastlocals, **freevars;
    PyThreadState *tstate;
    PyObject *x, *u;
    const Py_ssize_t total_args = co->co_argcount + co->co_kwonlyargcount;
    Py_ssize_t i, n;
    PyObject *kwdict;

    if (globals == NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "PyEval_EvalCodeEx: NULL globals");
        return NULL;
    }

    /* Create the frame */
    tstate = PyThreadState_GET();
    assert(tstate != NULL);
    f = PyFrame_New(tstate, co, globals, locals);
    if (f == NULL) {
        return NULL;
    }
    fastlocals = f->f_localsplus;
    freevars = f->f_localsplus + co->co_nlocals;

    /* Create a dictionary for keyword parameters (**kwags) */
    if (co->co_flags & CO_VARKEYWORDS) {
        kwdict = PyDict_New();
        if (kwdict == NULL)
            goto fail;
        i = total_args;
        if (co->co_flags & CO_VARARGS) {
            i++;
        }
        SETLOCAL(i, kwdict);
    }
    else {
        kwdict = NULL;
    }

    /* Copy positional arguments into local variables */
    if (argcount > co->co_argcount) {
        n = co->co_argcount;
    }
    else {
        n = argcount;
    }
    for (i = 0; i < n; i++) {
        x = args[i];
        Py_INCREF(x);
        SETLOCAL(i, x);
    }

    /* Pack other positional arguments into the *args argument */
    if (co->co_flags & CO_VARARGS) {
        u = PyTuple_New(argcount - n);
        if (u == NULL) {
            goto fail;
        }
        SETLOCAL(total_args, u);
        for (i = n; i < argcount; i++) {
            x = args[i];
            Py_INCREF(x);
            PyTuple_SET_ITEM(u, i-n, x);
        }
    }

    /* Handle keyword arguments passed as two strided arrays */
    kwcount *= kwstep;
    for (i = 0; i < kwcount; i += kwstep) {
        PyObject **co_varnames;
        PyObject *keyword = kwnames[i];
        PyObject *value = kwargs[i];
        Py_ssize_t j;

        if (keyword == NULL || !PyUnicode_Check(keyword)) {
            PyErr_Format(PyExc_TypeError,
                         "%U() keywords must be strings",
                         co->co_name);
            goto fail;
        }

        /* Speed hack: do raw pointer compares. As names are
           normally interned this should almost always hit. */
        co_varnames = ((PyTupleObject *)(co->co_varnames))->ob_item;
        for (j = 0; j < total_args; j++) {
            PyObject *name = co_varnames[j];
            if (name == keyword) {
                goto kw_found;
            }
        }

        /* Slow fallback, just in case */
        for (j = 0; j < total_args; j++) {
            PyObject *name = co_varnames[j];
            int cmp = PyObject_RichCompareBool( keyword, name, Py_EQ);
            if (cmp > 0) {
                goto kw_found;
            }
            else if (cmp < 0) {
                goto fail;
            }
        }

        if (j >= total_args && kwdict == NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%U() got an unexpected keyword argument '%S'",
                         co->co_name, keyword);
            goto fail;
        }

        if (PyDict_SetItem(kwdict, keyword, value) == -1) {
            goto fail;
        }
        continue;

      kw_found:
        if (GETLOCAL(j) != NULL) {
            PyErr_Format(PyExc_TypeError,
                         "%U() got multiple values for argument '%S'",
                         co->co_name, keyword);
            goto fail;
        }
        Py_INCREF(value);
        SETLOCAL(j, value);
    }

    /* Check the number of positional arguments */
    if (argcount > co->co_argcount && !(co->co_flags & CO_VARARGS)) {
        too_many_positional(co, argcount, defcount, fastlocals);
        goto fail;
    }

    /* Add missing positional arguments (copy default values from defs) */
    if (argcount < co->co_argcount) {
        Py_ssize_t m = co->co_argcount - defcount;
        Py_ssize_t missing = 0;
        for (i = argcount; i < m; i++) {
            if (GETLOCAL(i) == NULL) {
                missing++;
            }
        }
        if (missing) {
            missing_arguments(co, missing, defcount, fastlocals);
            goto fail;
        }
        if (n > m)
            i = n - m;
        else
            i = 0;
        for (; i < defcount; i++) {
            if (GETLOCAL(m+i) == NULL) {
                PyObject *def = defs[i];
                Py_INCREF(def);
                SETLOCAL(m+i, def);
            }
        }
    }

    /* Add missing keyword arguments (copy default values from kwdefs) */
    if (co->co_kwonlyargcount > 0) {
        Py_ssize_t missing = 0;
        for (i = co->co_argcount; i < total_args; i++) {
            PyObject *name;
            if (GETLOCAL(i) != NULL)
                continue;
            name = PyTuple_GET_ITEM(co->co_varnames, i);
            if (kwdefs != NULL) {
                PyObject *def = PyDict_GetItem(kwdefs, name);
                if (def) {
                    Py_INCREF(def);
                    SETLOCAL(i, def);
                    continue;
                }
            }
            missing++;
        }
        if (missing) {
            missing_arguments(co, missing, -1, fastlocals);
            goto fail;
        }
    }

    /* Allocate and initialize storage for cell vars, and copy free
       vars into frame. */
    for (i = 0; i < PyTuple_GET_SIZE(co->co_cellvars); ++i) {
        PyObject *c;
        int arg;
        /* Possibly account for the cell variable being an argument. */
        if (co->co_cell2arg != NULL &&
            (arg = co->co_cell2arg[i]) != CO_CELL_NOT_AN_ARG) {
            c = PyCell_New(GETLOCAL(arg));
            /* Clear the local copy. */
            SETLOCAL(arg, NULL);
        }
        else {
            c = PyCell_New(NULL);
        }
        if (c == NULL)
            goto fail;
        SETLOCAL(co->co_nlocals + i, c);
    }

    /* Copy closure variables to free variables */
    for (i = 0; i < PyTuple_GET_SIZE(co->co_freevars); ++i) {
        PyObject *o = PyTuple_GET_ITEM(closure, i);
        Py_INCREF(o);
        freevars[PyTuple_GET_SIZE(co->co_cellvars) + i] = o;
    }

    /* Handle generator/coroutine/asynchronous generator */
    if (co->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)) {
        PyObject *gen;
        PyObject *coro_wrapper = tstate->coroutine_wrapper;
        int is_coro = co->co_flags & CO_COROUTINE;

        if (is_coro && tstate->in_coroutine_wrapper) {
            assert(coro_wrapper != NULL);
            PyErr_Format(PyExc_RuntimeError,
                         "coroutine wrapper %.200R attempted "
                         "to recursively wrap %.200R",
                         coro_wrapper,
                         co);
            goto fail;
        }

        /* Don't need to keep the reference to f_back, it will be set
         * when the generator is resumed. */
        Py_CLEAR(f->f_back);

        /* Create a new generator that owns the ready to run frame
         * and return that as the value. */
        if (is_coro) {
            gen = PyCoro_New(f, name, qualname);
        } else if (co->co_flags & CO_ASYNC_GENERATOR) {
            gen = PyAsyncGen_New(f, name, qualname);
        } else {
            gen = PyGen_NewWithQualName(f, name, qualname);
        }
        if (gen == NULL)
            return NULL;

        if (is_coro && coro_wrapper != NULL) {
            PyObject *wrapped;
            tstate->in_coroutine_wrapper = 1;
            wrapped = PyObject_CallFunction(coro_wrapper, "N", gen);
            tstate->in_coroutine_wrapper = 0;
            return wrapped;
        }

        return gen;
    }

    retval = PyEval_EvalFrameEx(f,0);

fail: /* Jump here from prelude on failure */

    /* decref'ing the frame can cause __del__ methods to get invoked,
       which can call back into Python.  While we're done with the
       current Python frame (f), the associated C stack is still in use,
       so recursion_depth must be boosted for the duration.
    */
    assert(tstate != NULL);
    ++tstate->recursion_depth;
    Py_DECREF(f);
    --tstate->recursion_depth;
    return retval;
}

PyObject *
PyEval_EvalCodeEx(PyObject *_co, PyObject *globals, PyObject *locals,
           PyObject **args, int argcount, PyObject **kws, int kwcount,
           PyObject **defs, int defcount, PyObject *kwdefs, PyObject *closure)
{
    return _PyEval_EvalCodeWithName(_co, globals, locals,
                                    args, argcount,
                                    kws, kws != NULL ? kws + 1 : NULL,
                                    kwcount, 2,
                                    defs, defcount,
                                    kwdefs, closure,
                                    NULL, NULL);
}

/* These 3 functions deal with the exception state of generators. */

static void
save_exc_state(PyThreadState *tstate, PyFrameObject *f)
{
    PyObject *type, *value, *traceback;
    Py_XINCREF(tstate->exc_type);
    Py_XINCREF(tstate->exc_value);
    Py_XINCREF(tstate->exc_traceback);
    type = f->f_exc_type;
    value = f->f_exc_value;
    traceback = f->f_exc_traceback;
    f->f_exc_type = tstate->exc_type;
    f->f_exc_value = tstate->exc_value;
    f->f_exc_traceback = tstate->exc_traceback;
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
}

static void
swap_exc_state(PyThreadState *tstate, PyFrameObject *f)
{
    PyObject *tmp;
    tmp = tstate->exc_type;
    tstate->exc_type = f->f_exc_type;
    f->f_exc_type = tmp;
    tmp = tstate->exc_value;
    tstate->exc_value = f->f_exc_value;
    f->f_exc_value = tmp;
    tmp = tstate->exc_traceback;
    tstate->exc_traceback = f->f_exc_traceback;
    f->f_exc_traceback = tmp;
}

static void
restore_and_clear_exc_state(PyThreadState *tstate, PyFrameObject *f)
{
    PyObject *type, *value, *tb;
    type = tstate->exc_type;
    value = tstate->exc_value;
    tb = tstate->exc_traceback;
    tstate->exc_type = f->f_exc_type;
    tstate->exc_value = f->f_exc_value;
    tstate->exc_traceback = f->f_exc_traceback;
    f->f_exc_type = NULL;
    f->f_exc_value = NULL;
    f->f_exc_traceback = NULL;
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(tb);
}

PyObject *
_PyEval_CallTracing(PyObject *func, PyObject *args)
{
    PyThreadState *tstate = PyThreadState_GET();
    int save_tracing = tstate->tracing;
    int save_use_tracing = tstate->use_tracing;
    PyObject *result;

    tstate->tracing = 0;
    tstate->use_tracing = ((tstate->c_tracefunc != NULL)
                           || (tstate->c_profilefunc != NULL));
    result = PyObject_Call(func, args, NULL);
    tstate->tracing = save_tracing;
    tstate->use_tracing = save_use_tracing;
    return result;
}

void
PyEval_SetProfile(Py_tracefunc func, PyObject *arg)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyObject *temp = tstate->c_profileobj;
    Py_XINCREF(arg);
    tstate->c_profilefunc = NULL;
    tstate->c_profileobj = NULL;
    /* Must make sure that tracing is not ignored if 'temp' is freed */
    tstate->use_tracing = tstate->c_tracefunc != NULL;
    Py_XDECREF(temp);
    tstate->c_profilefunc = func;
    tstate->c_profileobj = arg;
    /* Flag that tracing or profiling is turned on */
    tstate->use_tracing = (func != NULL) || (tstate->c_tracefunc != NULL);
}

void
PyEval_SetTrace(Py_tracefunc func, PyObject *arg)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyObject *temp = tstate->c_traceobj;
    _Py_TracingPossible += (func != NULL) - (tstate->c_tracefunc != NULL);
    Py_XINCREF(arg);
    tstate->c_tracefunc = NULL;
    tstate->c_traceobj = NULL;
    /* Must make sure that profiling is not ignored if 'temp' is freed */
    tstate->use_tracing = tstate->c_profilefunc != NULL;
    Py_XDECREF(temp);
    tstate->c_tracefunc = func;
    tstate->c_traceobj = arg;
    /* Flag that tracing or profiling is turned on */
    tstate->use_tracing = ((func != NULL)
                           || (tstate->c_profilefunc != NULL));
}

void
_PyEval_SetCoroutineWrapper(PyObject *wrapper)
{
    PyThreadState *tstate = PyThreadState_GET();

    Py_XINCREF(wrapper);
    Py_XSETREF(tstate->coroutine_wrapper, wrapper);
}

PyObject *
_PyEval_GetCoroutineWrapper(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    return tstate->coroutine_wrapper;
}

void
_PyEval_SetAsyncGenFirstiter(PyObject *firstiter)
{
    PyThreadState *tstate = PyThreadState_GET();

    Py_XINCREF(firstiter);
    Py_XSETREF(tstate->async_gen_firstiter, firstiter);
}

PyObject *
_PyEval_GetAsyncGenFirstiter(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    return tstate->async_gen_firstiter;
}

void
_PyEval_SetAsyncGenFinalizer(PyObject *finalizer)
{
    PyThreadState *tstate = PyThreadState_GET();

    Py_XINCREF(finalizer);
    Py_XSETREF(tstate->async_gen_finalizer, finalizer);
}

PyObject *
_PyEval_GetAsyncGenFinalizer(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    return tstate->async_gen_finalizer;
}

PyObject *
PyEval_GetBuiltins(void)
{
    PyFrameObject *current_frame = PyEval_GetFrame();
    if (current_frame == NULL)
        return PyThreadState_GET()->interp->builtins;
    else
        return current_frame->f_builtins;
}

/* Convenience function to get a builtin from its name */
PyObject *
_PyEval_GetBuiltinId(_Py_Identifier *name)
{
    PyObject *attr = _PyDict_GetItemIdWithError(PyEval_GetBuiltins(), name);
    if (attr) {
        Py_INCREF(attr);
    }
    else if (!PyErr_Occurred()) {
        PyErr_SetObject(PyExc_AttributeError, _PyUnicode_FromId(name));
    }
    return attr;
}

PyObject *
PyEval_GetLocals(void)
{
    PyFrameObject *current_frame = PyEval_GetFrame();
    if (current_frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "frame does not exist");
        return NULL;
    }

    if (PyFrame_FastToLocalsWithError(current_frame) < 0)
        return NULL;

    assert(current_frame->f_locals != NULL);
    return current_frame->f_locals;
}

PyObject *
PyEval_GetGlobals(void)
{
    PyFrameObject *current_frame = PyEval_GetFrame();
    if (current_frame == NULL)
        return NULL;

    assert(current_frame->f_globals != NULL);
    return current_frame->f_globals;
}

PyFrameObject *
PyEval_GetFrame(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    return _PyThreadState_GetFrame(tstate);
}


/* External interface to call any callable object.
   The arg must be a tuple or NULL.  The kw must be a dict or NULL. */

PyObject *
PyEval_CallObjectWithKeywords(PyObject *func, PyObject *args, PyObject *kwargs)
{
#ifdef Py_DEBUG
    /* PyEval_CallObjectWithKeywords() must not be called with an exception
       set. It raises a new exception if parameters are invalid or if
       PyTuple_New() fails, and so the original exception is lost. */
    assert(!PyErr_Occurred());
#endif

    if (args != NULL && !PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError,
                        "argument list must be a tuple");
        return NULL;
    }

    if (kwargs != NULL && !PyDict_Check(kwargs)) {
        PyErr_SetString(PyExc_TypeError,
                        "keyword list must be a dictionary");
        return NULL;
    }

    if (args == NULL) {
        return _PyObject_FastCallDict(func, NULL, 0, kwargs);
    }
    else {
        return PyObject_Call(func, args, kwargs);
    }
}

const char *
PyEval_GetFuncName(PyObject *func)
{
    if (PyMethod_Check(func))
        return PyEval_GetFuncName(PyMethod_GET_FUNCTION(func));
    else if (PyFunction_Check(func))
        return PyUnicode_AsUTF8(((PyFunctionObject*)func)->func_name);
    else if (PyCFunction_Check(func))
        return ((PyCFunctionObject*)func)->m_ml->ml_name;
    else
        return func->ob_type->tp_name;
}

const char *
PyEval_GetFuncDesc(PyObject *func)
{
    if (PyMethod_Check(func))
        return "()";
    else if (PyFunction_Check(func))
        return "()";
    else if (PyCFunction_Check(func))
        return "()";
    else
        return " object";
}

/* The fast_function() function optimize calls for which no argument
   tuple is necessary; the objects are passed directly from the stack.
   For the simplest case -- a function that takes only positional
   arguments and is called with only positional arguments -- it
   inlines the most primitive frame setup code from
   PyEval_EvalCodeEx(), which vastly reduces the checks that must be
   done before evaluating the frame.
*/

static PyObject*
_PyFunction_FastCall(PyCodeObject *co, PyObject **args, Py_ssize_t nargs,
                     PyObject *globals)
{
    PyFrameObject *f;
    PyThreadState *tstate = PyThreadState_GET();
    PyObject **fastlocals;
    Py_ssize_t i;
    PyObject *result;

    assert(globals != NULL);
    /* XXX Perhaps we should create a specialized
       PyFrame_New() that doesn't take locals, but does
       take builtins without sanity checking them.
       */
    assert(tstate != NULL);
    f = PyFrame_New(tstate, co, globals, NULL);
    if (f == NULL) {
        return NULL;
    }

    fastlocals = f->f_localsplus;

    for (i = 0; i < nargs; i++) {
        Py_INCREF(*args);
        fastlocals[i] = *args++;
    }
    result = PyEval_EvalFrameEx(f,0);

    ++tstate->recursion_depth;
    Py_DECREF(f);
    --tstate->recursion_depth;

    return result;
}

static PyObject *
fast_function(PyObject *func, PyObject **stack,
              Py_ssize_t nargs, PyObject *kwnames)
{
    PyCodeObject *co = (PyCodeObject *)PyFunction_GET_CODE(func);
    PyObject *globals = PyFunction_GET_GLOBALS(func);
    PyObject *argdefs = PyFunction_GET_DEFAULTS(func);
    PyObject *kwdefs, *closure, *name, *qualname;
    PyObject **d;
    Py_ssize_t nkwargs = (kwnames == NULL) ? 0 : PyTuple_GET_SIZE(kwnames);
    Py_ssize_t nd;

    assert(PyFunction_Check(func));
    assert(nargs >= 0);
    assert(kwnames == NULL || PyTuple_CheckExact(kwnames));
    assert((nargs == 0 && nkwargs == 0) || stack != NULL);
    /* kwnames must only contains str strings, no subclass, and all keys must
       be unique */

    if (co->co_kwonlyargcount == 0 && nkwargs == 0 &&
        co->co_flags == (CO_OPTIMIZED | CO_NEWLOCALS | CO_NOFREE))
    {
        if (argdefs == NULL && co->co_argcount == nargs) {
            return _PyFunction_FastCall(co, stack, nargs, globals);
        }
        else if (nargs == 0 && argdefs != NULL
                 && co->co_argcount == Py_SIZE(argdefs)) {
            /* function called with no arguments, but all parameters have
               a default value: use default values as arguments .*/
            stack = &PyTuple_GET_ITEM(argdefs, 0);
            return _PyFunction_FastCall(co, stack, Py_SIZE(argdefs), globals);
        }
    }

    kwdefs = PyFunction_GET_KW_DEFAULTS(func);
    closure = PyFunction_GET_CLOSURE(func);
    name = ((PyFunctionObject *)func) -> func_name;
    qualname = ((PyFunctionObject *)func) -> func_qualname;

    if (argdefs != NULL) {
        d = &PyTuple_GET_ITEM(argdefs, 0);
        nd = Py_SIZE(argdefs);
    }
    else {
        d = NULL;
        nd = 0;
    }
    return _PyEval_EvalCodeWithName((PyObject*)co, globals, (PyObject *)NULL,
                                    stack, nargs,
                                    nkwargs ? &PyTuple_GET_ITEM(kwnames, 0) : NULL,
                                    stack + nargs,
                                    nkwargs, 1,
                                    d, (int)nd, kwdefs,
                                    closure, name, qualname);
}

PyObject *
_PyFunction_FastCallKeywords(PyObject *func, PyObject **stack,
                             Py_ssize_t nargs, PyObject *kwnames)
{
    return fast_function(func, stack, nargs, kwnames);
}

PyObject *
_PyFunction_FastCallDict(PyObject *func, PyObject **args, Py_ssize_t nargs,
                         PyObject *kwargs)
{
    PyCodeObject *co = (PyCodeObject *)PyFunction_GET_CODE(func);
    PyObject *globals = PyFunction_GET_GLOBALS(func);
    PyObject *argdefs = PyFunction_GET_DEFAULTS(func);
    PyObject *kwdefs, *closure, *name, *qualname;
    PyObject *kwtuple, **k;
    PyObject **d;
    Py_ssize_t nd, nk;
    PyObject *result;

    assert(func != NULL);
    assert(nargs >= 0);
    assert(nargs == 0 || args != NULL);
    assert(kwargs == NULL || PyDict_Check(kwargs));

    if (co->co_kwonlyargcount == 0 &&
        (kwargs == NULL || PyDict_Size(kwargs) == 0) &&
        co->co_flags == (CO_OPTIMIZED | CO_NEWLOCALS | CO_NOFREE))
    {
        /* Fast paths */
        if (argdefs == NULL && co->co_argcount == nargs) {
            return _PyFunction_FastCall(co, args, nargs, globals);
        }
        else if (nargs == 0 && argdefs != NULL
                 && co->co_argcount == Py_SIZE(argdefs)) {
            /* function called with no arguments, but all parameters have
               a default value: use default values as arguments .*/
            args = &PyTuple_GET_ITEM(argdefs, 0);
            return _PyFunction_FastCall(co, args, Py_SIZE(argdefs), globals);
        }
    }

    if (kwargs != NULL) {
        Py_ssize_t pos, i;
        nk = PyDict_Size(kwargs);

        kwtuple = PyTuple_New(2 * nk);
        if (kwtuple == NULL) {
            return NULL;
        }

        k = &PyTuple_GET_ITEM(kwtuple, 0);
        pos = i = 0;
        while (PyDict_Next(kwargs, &pos, &k[i], &k[i+1])) {
            Py_INCREF(k[i]);
            Py_INCREF(k[i+1]);
            i += 2;
        }
        nk = i / 2;
    }
    else {
        kwtuple = NULL;
        k = NULL;
        nk = 0;
    }

    kwdefs = PyFunction_GET_KW_DEFAULTS(func);
    closure = PyFunction_GET_CLOSURE(func);
    name = ((PyFunctionObject *)func) -> func_name;
    qualname = ((PyFunctionObject *)func) -> func_qualname;

    if (argdefs != NULL) {
        d = &PyTuple_GET_ITEM(argdefs, 0);
        nd = Py_SIZE(argdefs);
    }
    else {
        d = NULL;
        nd = 0;
    }

    result = _PyEval_EvalCodeWithName((PyObject*)co, globals, (PyObject *)NULL,
                                      args, nargs,
                                      k, k != NULL ? k + 1 : NULL, nk, 2,
                                      d, nd, kwdefs,
                                      closure, name, qualname);
    Py_XDECREF(kwtuple);
    return result;
}

/* Extract a slice index from a PyLong or an object with the
   nb_index slot defined, and store in *pi.
   Silently reduce values larger than PY_SSIZE_T_MAX to PY_SSIZE_T_MAX,
   and silently boost values less than PY_SSIZE_T_MIN to PY_SSIZE_T_MIN.
   Return 0 on error, 1 on success.
*/
int
_PyEval_SliceIndex(PyObject *v, Py_ssize_t *pi)
{
    if (v != Py_None) {
        Py_ssize_t x;
        if (PyIndex_Check(v)) {
            x = PyNumber_AsSsize_t(v, NULL);
            if (x == -1 && PyErr_Occurred())
                return 0;
        }
        else {
            PyErr_SetString(PyExc_TypeError,
                            "slice indices must be integers or "
                            "None or have an __index__ method");
            return 0;
        }
        *pi = x;
    }
    return 1;
}

int
_PyEval_SliceIndexNotNone(PyObject *v, Py_ssize_t *pi)
{
    Py_ssize_t x;
    if (PyIndex_Check(v)) {
        x = PyNumber_AsSsize_t(v, NULL);
        if (x == -1 && PyErr_Occurred())
            return 0;
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "slice indices must be integers or "
                        "have an __index__ method");
        return 0;
    }
    *pi = x;
    return 1;
}

#ifdef DYNAMIC_EXECUTION_PROFILE

static PyObject *
getarray(long a[256])
{
    int i;
    PyObject *l = PyList_New(256);
    if (l == NULL) return NULL;
    for (i = 0; i < 256; i++) {
        PyObject *x = PyLong_FromLong(a[i]);
        if (x == NULL) {
            Py_DECREF(l);
            return NULL;
        }
        PyList_SET_ITEM(l, i, x);
    }
    for (i = 0; i < 256; i++)
        a[i] = 0;
    return l;
}

PyObject *
_Py_GetDXProfile(PyObject *self, PyObject *args)
{
#ifndef DXPAIRS
    return getarray(dxp);
#else
    int i;
    PyObject *l = PyList_New(257);
    if (l == NULL) return NULL;
    for (i = 0; i < 257; i++) {
        PyObject *x = getarray(dxpairs[i]);
        if (x == NULL) {
            Py_DECREF(l);
            return NULL;
        }
        PyList_SET_ITEM(l, i, x);
    }
    return l;
#endif
}

#endif

Py_ssize_t
_PyEval_RequestCodeExtraIndex(freefunc free)
{
    __PyCodeExtraState *state = __PyCodeExtraState_Get();
    Py_ssize_t new_index;

    if (state->co_extra_user_count == MAX_CO_EXTRA_USERS - 1) {
        return -1;
    }
    new_index = state->co_extra_user_count++;
    state->co_extra_freefuncs[new_index] = free;
    return new_index;
}
