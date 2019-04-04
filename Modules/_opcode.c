#include "Python.h"
#include "opcode.h"

/*[clinic input]
module _opcode
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=117442e66eb376e6]*/

#include "clinic/_opcode.c.h"

/*[clinic input]

_opcode.stack_effect -> int

  opcode: int
  oparg: object = None
  /

Compute the stack effect of the opcode.
[clinic start generated code]*/

/* moved from compile.c */

#include "opcode.h"
#define PY_INVALID_STACK_EFFECT INT_MAX

static int
stack_effect(int opcode, int oparg)
{
    switch (opcode) {
        case POP_TOP:
            return -1;
        case ROT_TWO:
        case ROT_THREE:
            return 0;
        case DUP_TOP:
            return 1;
        case DUP_TOP_TWO:
            return 2;

        case UNARY_POSITIVE:
        case UNARY_NEGATIVE:
        case UNARY_NOT:
        case UNARY_INVERT:
            return 0;

        case SET_ADD:
        case LIST_APPEND:
            return -1;
        case MAP_ADD:
            return -2;

        case BINARY_POWER:
        case BINARY_MULTIPLY:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MODULO:
        case BINARY_ADD:
        case BINARY_SUBTRACT:
        case BINARY_SUBSCR:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_TRUE_DIVIDE:
            return -1;
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            return -1;

        case INPLACE_ADD:
        case INPLACE_SUBTRACT:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_MODULO:
            return -1;
        case STORE_SUBSCR:
            return -3;
        case DELETE_SUBSCR:
            return -2;

        case BINARY_LSHIFT:
        case BINARY_RSHIFT:
        case BINARY_AND:
        case BINARY_XOR:
        case BINARY_OR:
            return -1;
        case INPLACE_POWER:
            return -1;
        case GET_ITER:
            return 0;

        case PRINT_EXPR:
            return -1;
        case LOAD_BUILD_CLASS:
            return 1;
        case INPLACE_LSHIFT:
        case INPLACE_RSHIFT:
        case INPLACE_AND:
        case INPLACE_XOR:
        case INPLACE_OR:
            return -1;
        case BREAK_LOOP:
            return 0;
        case SETUP_WITH:
            return 7;
        case WITH_CLEANUP_START:
            return 1;
        case WITH_CLEANUP_FINISH:
            return -1; /* XXX Sometimes more */
        case RETURN_VALUE:
            return -1;
        case IMPORT_STAR:
            return -1;
        case SETUP_ANNOTATIONS:
            return 0;
        case YIELD_VALUE:
            return 0;
        case YIELD_FROM:
            return -1;
        case POP_BLOCK:
            return 0;
        case POP_EXCEPT:
            return 0;  /* -3 except if bad bytecode */
        case END_FINALLY:
            return -1; /* or -2 or -3 if exception occurred */

        case STORE_NAME:
            return -1;
        case DELETE_NAME:
            return 0;
        case UNPACK_SEQUENCE:
            return oparg-1;
        case UNPACK_EX:
            return (oparg&0xFF) + (oparg>>8);
        case FOR_ITER:
            return 1; /* or -1, at end of iterator */

        case STORE_ATTR:
            return -2;
        case DELETE_ATTR:
            return -1;
        case STORE_GLOBAL:
            return -1;
        case DELETE_GLOBAL:
            return 0;
        case LOAD_CONST:
            return 1;
        case LOAD_NAME:
            return 1;
        case BUILD_TUPLE:
        case BUILD_LIST:
        case BUILD_SET:
        case BUILD_STRING:
            return 1-oparg;
        case BUILD_LIST_UNPACK:
        case BUILD_TUPLE_UNPACK:
        case BUILD_TUPLE_UNPACK_WITH_CALL:
        case BUILD_SET_UNPACK:
        case BUILD_MAP_UNPACK:
        case BUILD_MAP_UNPACK_WITH_CALL:
            return 1 - oparg;
        case BUILD_MAP:
            return 1 - 2*oparg;
        case BUILD_CONST_KEY_MAP:
            return -oparg;
        case LOAD_ATTR:
            return 0;
        case COMPARE_OP:
            return -1;
        case IMPORT_NAME:
            return -1;
        case IMPORT_FROM:
            return 1;

        case JUMP_FORWARD:
        case JUMP_IF_TRUE_OR_POP:  /* -1 if jump not taken */
        case JUMP_IF_FALSE_OR_POP:  /*  "" */
        case JUMP_ABSOLUTE:
            return 0;

        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE:
            return -1;

        case LOAD_GLOBAL:
            return 1;

        case CONTINUE_LOOP:
            return 0;
        case SETUP_LOOP:
            return 0;
        case SETUP_EXCEPT:
        case SETUP_FINALLY:
            return 6; /* can push 3 values for the new exception
                + 3 others for the previous exception state */

        case LOAD_FAST:
            return 1;
        case STORE_FAST:
            return -1;
        case DELETE_FAST:
            return 0;
        case STORE_ANNOTATION:
            return -1;

        case RAISE_VARARGS:
            return -oparg;
        case CALL_FUNCTION:
            return -oparg;
        case CALL_FUNCTION_KW:
            return -oparg-1;
        case CALL_FUNCTION_EX:
            return -1 - ((oparg & 0x01) != 0);
        case MAKE_FUNCTION:
            return -1 - ((oparg & 0x01) != 0) - ((oparg & 0x02) != 0) -
                ((oparg & 0x04) != 0) - ((oparg & 0x08) != 0);
        case BUILD_SLICE:
            if (oparg == 3)
                return -2;
            else
                return -1;

        case LOAD_CLOSURE:
            return 1;
        case LOAD_DEREF:
        case LOAD_CLASSDEREF:
            return 1;
        case STORE_DEREF:
            return -1;
        case DELETE_DEREF:
            return 0;
        case GET_AWAITABLE:
            return 0;
        case SETUP_ASYNC_WITH:
            return 6;
        case BEFORE_ASYNC_WITH:
            return 1;
        case GET_AITER:
            return 0;
        case GET_ANEXT:
            return 1;
        case GET_YIELD_FROM_ITER:
            return 0;
        case FORMAT_VALUE:
            /* If there's a fmt_spec on the stack, we go from 2->1,
               else 1->1. */
            return (oparg & FVS_MASK) == FVS_HAVE_SPEC ? -1 : 0;
        default:
            return PY_INVALID_STACK_EFFECT;
    }
    return PY_INVALID_STACK_EFFECT; /* not reachable */
}

static int
_opcode_stack_effect_impl(PyObject *module, int opcode, PyObject *oparg)
/*[clinic end generated code: output=ad39467fa3ad22ce input=2d0a9ee53c0418f5]*/
{
    int effect;
    int oparg_int = 0;
    if (HAS_ARG(opcode)) {
        if (oparg == Py_None) {
            PyErr_SetString(PyExc_ValueError,
                    "stack_effect: opcode requires oparg but oparg was not specified");
            return -1;
        }
        oparg_int = (int)PyLong_AsLong(oparg);
        if ((oparg_int == -1) && PyErr_Occurred())
            return -1;
    }
    else if (oparg != Py_None) {
        PyErr_SetString(PyExc_ValueError,
                "stack_effect: opcode does not permit oparg but oparg was specified");
        return -1;
    }
    effect = stack_effect(opcode, oparg_int);
    if (effect == PY_INVALID_STACK_EFFECT) {
            PyErr_SetString(PyExc_ValueError,
                    "invalid opcode or oparg");
            return -1;
    }
    return effect;
}




static PyMethodDef
opcode_functions[] =  {
    _OPCODE_STACK_EFFECT_METHODDEF
    {NULL, NULL, 0, NULL}
};


static struct PyModuleDef opcodemodule = {
    PyModuleDef_HEAD_INIT,
    "_opcode",
    "Opcode support module.",
    -1,
    opcode_functions,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__opcode(void)
{
    return PyModule_Create(&opcodemodule);
}
