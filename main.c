#include <Python.h>

int main(int argc, char** argv)
{
    Py_FileSystemDefaultEncoding = "UTF-8";

    wchar_t* program = Py_DecodeLocale(argv[0], NULL);
    if (program == NULL) {
        fprintf(stderr, "Fatal error: cannot decode argv[0]\n");
        exit(1);
    }

    Py_FrozenFlag++;
    Py_NoSiteFlag++;
    Py_SetProgramName(program);  /* optional but recommended */
    Py_Initialize();
    PyImport_ImportModule("__hello__");
    if (Py_FinalizeEx() < 0) {
        exit(120);
    }
    PyMem_RawFree(program);
    return 0;
}
