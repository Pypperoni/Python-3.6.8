
/* Python interpreter top-level routines, including init/exit */

#include "Python.h"

#undef Yield /* undefine macro conflicting with winbase.h */
#include "errcode.h"
#include "code.h"
#include "marshal.h"
#include "osdefs.h"
#include <locale.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef MS_WINDOWS
#include "malloc.h" /* for alloca */
#endif

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef MS_WINDOWS
#undef BYTE
#include "windows.h"
#endif

_Py_IDENTIFIER(builtins);
_Py_IDENTIFIER(excepthook);
_Py_IDENTIFIER(flush);
_Py_IDENTIFIER(last_traceback);
_Py_IDENTIFIER(last_type);
_Py_IDENTIFIER(last_value);
_Py_IDENTIFIER(ps1);
_Py_IDENTIFIER(ps2);
_Py_IDENTIFIER(stdin);
_Py_IDENTIFIER(stdout);
_Py_IDENTIFIER(stderr);
_Py_static_string(PyId_string, "<string>");

#ifdef __cplusplus
extern "C" {
#endif

static int
parse_syntax_error(PyObject *err, PyObject **message, PyObject **filename,
                   int *lineno, int *offset, PyObject **text)
{
    int hold;
    PyObject *v;
    _Py_IDENTIFIER(msg);
    _Py_IDENTIFIER(filename);
    _Py_IDENTIFIER(lineno);
    _Py_IDENTIFIER(offset);
    _Py_IDENTIFIER(text);

    *message = NULL;
    *filename = NULL;

    /* new style errors.  `err' is an instance */
    *message = _PyObject_GetAttrId(err, &PyId_msg);
    if (!*message)
        goto finally;

    v = _PyObject_GetAttrId(err, &PyId_filename);
    if (!v)
        goto finally;
    if (v == Py_None) {
        Py_DECREF(v);
        *filename = _PyUnicode_FromId(&PyId_string);
        if (*filename == NULL)
            goto finally;
        Py_INCREF(*filename);
    }
    else {
        *filename = v;
    }

    v = _PyObject_GetAttrId(err, &PyId_lineno);
    if (!v)
        goto finally;
    hold = _PyLong_AsInt(v);
    Py_DECREF(v);
    if (hold < 0 && PyErr_Occurred())
        goto finally;
    *lineno = hold;

    v = _PyObject_GetAttrId(err, &PyId_offset);
    if (!v)
        goto finally;
    if (v == Py_None) {
        *offset = -1;
        Py_DECREF(v);
    } else {
        hold = _PyLong_AsInt(v);
        Py_DECREF(v);
        if (hold < 0 && PyErr_Occurred())
            goto finally;
        *offset = hold;
    }

    v = _PyObject_GetAttrId(err, &PyId_text);
    if (!v)
        goto finally;
    if (v == Py_None) {
        Py_DECREF(v);
        *text = NULL;
    }
    else {
        *text = v;
    }
    return 1;

finally:
    Py_XDECREF(*message);
    Py_XDECREF(*filename);
    return 0;
}

void
PyErr_Print(void)
{
    PyErr_PrintEx(1);
}

static void
print_error_text(PyObject *f, int offset, PyObject *text_obj)
{
    char *text;
    char *nl;

    text = PyUnicode_AsUTF8(text_obj);
    if (text == NULL)
        return;

    if (offset >= 0) {
        if (offset > 0 && (size_t)offset == strlen(text) && text[offset - 1] == '\n')
            offset--;
        for (;;) {
            nl = strchr(text, '\n');
            if (nl == NULL || nl-text >= offset)
                break;
            offset -= (int)(nl+1-text);
            text = nl+1;
        }
        while (*text == ' ' || *text == '\t' || *text == '\f') {
            text++;
            offset--;
        }
    }
    PyFile_WriteString("    ", f);
    PyFile_WriteString(text, f);
    if (*text == '\0' || text[strlen(text)-1] != '\n')
        PyFile_WriteString("\n", f);
    if (offset == -1)
        return;
    PyFile_WriteString("    ", f);
    while (--offset > 0)
        PyFile_WriteString(" ", f);
    PyFile_WriteString("^\n", f);
}

static void
handle_system_exit(void)
{
    PyObject *exception, *value, *tb;
    int exitcode = 0;

    if (Py_InspectFlag)
        /* Don't exit if -i flag was given. This flag is set to 0
         * when entering interactive mode for inspecting. */
        return;

    PyErr_Fetch(&exception, &value, &tb);
    fflush(stdout);
    if (value == NULL || value == Py_None)
        goto done;
    if (PyExceptionInstance_Check(value)) {
        /* The error code should be in the `code' attribute. */
        _Py_IDENTIFIER(code);
        PyObject *code = _PyObject_GetAttrId(value, &PyId_code);
        if (code) {
            Py_DECREF(value);
            value = code;
            if (value == Py_None)
                goto done;
        }
        /* If we failed to dig out the 'code' attribute,
           just let the else clause below print the error. */
    }
    if (PyLong_Check(value))
        exitcode = (int)PyLong_AsLong(value);
    else {
        PyObject *sys_stderr = _PySys_GetObjectId(&PyId_stderr);
        /* We clear the exception here to avoid triggering the assertion
         * in PyObject_Str that ensures it won't silently lose exception
         * details.
         */
        PyErr_Clear();
        if (sys_stderr != NULL && sys_stderr != Py_None) {
            PyFile_WriteObject(value, sys_stderr, Py_PRINT_RAW);
        } else {
            PyObject_Print(value, stderr, Py_PRINT_RAW);
            fflush(stderr);
        }
        PySys_WriteStderr("\n");
        exitcode = 1;
    }
 done:
    /* Restore and clear the exception info, in order to properly decref
     * the exception, value, and traceback.      If we just exit instead,
     * these leak, which confuses PYTHONDUMPREFS output, and may prevent
     * some finalizers from running.
     */
    PyErr_Restore(exception, value, tb);
    PyErr_Clear();
    Py_Exit(exitcode);
    /* NOTREACHED */
}

void
PyErr_PrintEx(int set_sys_last_vars)
{
    PyObject *exception, *v, *tb, *hook;

    if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
        handle_system_exit();
    }
    PyErr_Fetch(&exception, &v, &tb);
    if (exception == NULL)
        return;
    PyErr_NormalizeException(&exception, &v, &tb);
    if (tb == NULL) {
        tb = Py_None;
        Py_INCREF(tb);
    }
    PyException_SetTraceback(v, tb);
    if (exception == NULL)
        return;
    /* Now we know v != NULL too */
    if (set_sys_last_vars) {
        if (_PySys_SetObjectId(&PyId_last_type, exception) < 0) {
            PyErr_Clear();
        }
        if (_PySys_SetObjectId(&PyId_last_value, v) < 0) {
            PyErr_Clear();
        }
        if (_PySys_SetObjectId(&PyId_last_traceback, tb) < 0) {
            PyErr_Clear();
        }
    }
    hook = _PySys_GetObjectId(&PyId_excepthook);
    if (hook) {
        PyObject* stack[3];
        PyObject *result;

        stack[0] = exception;
        stack[1] = v;
        stack[2] = tb;
        result = _PyObject_FastCall(hook, stack, 3);
        if (result == NULL) {
            PyObject *exception2, *v2, *tb2;
            if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
                handle_system_exit();
            }
            PyErr_Fetch(&exception2, &v2, &tb2);
            PyErr_NormalizeException(&exception2, &v2, &tb2);
            /* It should not be possible for exception2 or v2
               to be NULL. However PyErr_Display() can't
               tolerate NULLs, so just be safe. */
            if (exception2 == NULL) {
                exception2 = Py_None;
                Py_INCREF(exception2);
            }
            if (v2 == NULL) {
                v2 = Py_None;
                Py_INCREF(v2);
            }
            fflush(stdout);
            PySys_WriteStderr("Error in sys.excepthook:\n");
            PyErr_Display(exception2, v2, tb2);
            PySys_WriteStderr("\nOriginal exception was:\n");
            PyErr_Display(exception, v, tb);
            Py_DECREF(exception2);
            Py_DECREF(v2);
            Py_XDECREF(tb2);
        }
        Py_XDECREF(result);
    } else {
        PySys_WriteStderr("sys.excepthook is missing\n");
        PyErr_Display(exception, v, tb);
    }
    Py_XDECREF(exception);
    Py_XDECREF(v);
    Py_XDECREF(tb);
}

static void
print_exception(PyObject *f, PyObject *value)
{
    int err = 0;
    PyObject *type, *tb;
    _Py_IDENTIFIER(print_file_and_line);

    if (!PyExceptionInstance_Check(value)) {
        err = PyFile_WriteString("TypeError: print_exception(): Exception expected for value, ", f);
        err += PyFile_WriteString(Py_TYPE(value)->tp_name, f);
        err += PyFile_WriteString(" found\n", f);
        if (err)
            PyErr_Clear();
        return;
    }

    Py_INCREF(value);
    fflush(stdout);
    type = (PyObject *) Py_TYPE(value);
    tb = PyException_GetTraceback(value);
    if (tb && tb != Py_None)
        err = PyTraceBack_Print(tb, f);
    if (err == 0 &&
        _PyObject_HasAttrId(value, &PyId_print_file_and_line))
    {
        PyObject *message, *filename, *text;
        int lineno, offset;
        if (!parse_syntax_error(value, &message, &filename,
                                &lineno, &offset, &text))
            PyErr_Clear();
        else {
            PyObject *line;

            Py_DECREF(value);
            value = message;

            line = PyUnicode_FromFormat("  File \"%U\", line %d\n",
                                          filename, lineno);
            Py_DECREF(filename);
            if (line != NULL) {
                PyFile_WriteObject(line, f, Py_PRINT_RAW);
                Py_DECREF(line);
            }

            if (text != NULL) {
                print_error_text(f, offset, text);
                Py_DECREF(text);
            }

            /* Can't be bothered to check all those
               PyFile_WriteString() calls */
            if (PyErr_Occurred())
                err = -1;
        }
    }
    if (err) {
        /* Don't do anything else */
    }
    else {
        PyObject* moduleName;
        char* className;
        _Py_IDENTIFIER(__module__);
        assert(PyExceptionClass_Check(type));
        className = PyExceptionClass_Name(type);
        if (className != NULL) {
            char *dot = strrchr(className, '.');
            if (dot != NULL)
                className = dot+1;
        }

        moduleName = _PyObject_GetAttrId(type, &PyId___module__);
        if (moduleName == NULL || !PyUnicode_Check(moduleName))
        {
            Py_XDECREF(moduleName);
            err = PyFile_WriteString("<unknown>", f);
        }
        else {
            if (!_PyUnicode_EqualToASCIIId(moduleName, &PyId_builtins))
            {
                err = PyFile_WriteObject(moduleName, f, Py_PRINT_RAW);
                err += PyFile_WriteString(".", f);
            }
            Py_DECREF(moduleName);
        }
        if (err == 0) {
            if (className == NULL)
                      err = PyFile_WriteString("<unknown>", f);
            else
                      err = PyFile_WriteString(className, f);
        }
    }
    if (err == 0 && (value != Py_None)) {
        PyObject *s = PyObject_Str(value);
        /* only print colon if the str() of the
           object is not the empty string
        */
        if (s == NULL) {
            PyErr_Clear();
            err = -1;
            PyFile_WriteString(": <exception str() failed>", f);
        }
        else if (!PyUnicode_Check(s) ||
            PyUnicode_GetLength(s) != 0)
            err = PyFile_WriteString(": ", f);
        if (err == 0)
          err = PyFile_WriteObject(s, f, Py_PRINT_RAW);
        Py_XDECREF(s);
    }
    /* try to write a newline in any case */
    if (err < 0) {
        PyErr_Clear();
    }
    err += PyFile_WriteString("\n", f);
    Py_XDECREF(tb);
    Py_DECREF(value);
    /* If an error happened here, don't show it.
       XXX This is wrong, but too many callers rely on this behavior. */
    if (err != 0)
        PyErr_Clear();
}

static const char cause_message[] =
    "\nThe above exception was the direct cause "
    "of the following exception:\n\n";

static const char context_message[] =
    "\nDuring handling of the above exception, "
    "another exception occurred:\n\n";

static void
print_exception_recursive(PyObject *f, PyObject *value, PyObject *seen)
{
    int err = 0, res;
    PyObject *cause, *context;

    if (seen != NULL) {
        /* Exception chaining */
        PyObject *value_id = PyLong_FromVoidPtr(value);
        if (value_id == NULL || PySet_Add(seen, value_id) == -1)
            PyErr_Clear();
        else if (PyExceptionInstance_Check(value)) {
            PyObject *check_id = NULL;
            cause = PyException_GetCause(value);
            context = PyException_GetContext(value);
            if (cause) {
                check_id = PyLong_FromVoidPtr(cause);
                if (check_id == NULL) {
                    res = -1;
                } else {
                    res = PySet_Contains(seen, check_id);
                    Py_DECREF(check_id);
                }
                if (res == -1)
                    PyErr_Clear();
                if (res == 0) {
                    print_exception_recursive(
                        f, cause, seen);
                    err |= PyFile_WriteString(
                        cause_message, f);
                }
            }
            else if (context &&
                !((PyBaseExceptionObject *)value)->suppress_context) {
                check_id = PyLong_FromVoidPtr(context);
                if (check_id == NULL) {
                    res = -1;
                } else {
                    res = PySet_Contains(seen, check_id);
                    Py_DECREF(check_id);
                }
                if (res == -1)
                    PyErr_Clear();
                if (res == 0) {
                    print_exception_recursive(
                        f, context, seen);
                    err |= PyFile_WriteString(
                        context_message, f);
                }
            }
            Py_XDECREF(context);
            Py_XDECREF(cause);
        }
        Py_XDECREF(value_id);
    }
    print_exception(f, value);
    if (err != 0)
        PyErr_Clear();
}

void
PyErr_Display(PyObject *exception, PyObject *value, PyObject *tb)
{
    PyObject *seen;
    PyObject *f = _PySys_GetObjectId(&PyId_stderr);
    if (PyExceptionInstance_Check(value)
        && tb != NULL && PyTraceBack_Check(tb)) {
        /* Put the traceback on the exception, otherwise it won't get
           displayed.  See issue #18776. */
        PyObject *cur_tb = PyException_GetTraceback(value);
        if (cur_tb == NULL)
            PyException_SetTraceback(value, tb);
        else
            Py_DECREF(cur_tb);
    }
    if (f == Py_None) {
        /* pass */
    }
    else if (f == NULL) {
        _PyObject_Dump(value);
        fprintf(stderr, "lost sys.stderr\n");
    }
    else {
        /* We choose to ignore seen being possibly NULL, and report
           at least the main exception (it could be a MemoryError).
        */
        seen = PySet_New(NULL);
        if (seen == NULL)
            PyErr_Clear();
        print_exception_recursive(f, value, seen);
        Py_XDECREF(seen);
    }
}

#if defined(USE_STACKCHECK)
#if defined(WIN32) && defined(_MSC_VER)

/* Stack checking for Microsoft C */

#include <malloc.h>
#include <excpt.h>

/*
 * Return non-zero when we run out of memory on the stack; zero otherwise.
 */
int
PyOS_CheckStack(void)
{
    __try {
        /* alloca throws a stack overflow exception if there's
           not enough space left on the stack */
        alloca(PYOS_STACK_MARGIN * sizeof(void*));
        return 0;
    } __except (GetExceptionCode() == STATUS_STACK_OVERFLOW ?
                    EXCEPTION_EXECUTE_HANDLER :
            EXCEPTION_CONTINUE_SEARCH) {
        int errcode = _resetstkoflw();
        if (errcode == 0)
        {
            Py_FatalError("Could not reset the stack!");
        }
    }
    return 1;
}

#endif /* WIN32 && _MSC_VER */

/* Alternate implementations can be added here... */

#endif /* USE_STACKCHECK */

#ifdef __cplusplus
}
#endif
