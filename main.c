#include <Python.h>

int main(int argc, char** argv)
{
    Py_FrozenFlag++;
    Py_NoSiteFlag++;
    return Py_Main(argc, argv);
}
