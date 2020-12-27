#ifndef JS2PYTHON_H
#define JS2PYTHON_H

/**
 * Utilities to convert Javascript objects to Python objects.
 */

#include <Python.h>

/** Convert a Javascript object to a Python object.
 *  \param x The Javascript object.
 *  \return The Python object. A new reference, must use Py_DECREF when done
 * with it. On error returns NULL and sets a Python error.
 */
PyObject*
js2python(int x);

int
js2python_init();

#endif /* JS2PYTHON_H */
