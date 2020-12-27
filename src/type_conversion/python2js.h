#ifndef PYTHON2JS_H
#define PYTHON2JS_H

/** Utilities to convert Python objects to Javascript.
 */

#include <Python.h>

/** Convert the active Python exception into a Javascript Error object.
 *  \return A Javascript Error object
 */
int
format_exc();

/** Convert a Python object to a Javascript object.
 *  \param The Python object
 *  \return On success returns a hiwire id for the javascript object.
 *      On fail returns zero and sets a Python error.
 */
int
python2js_deep(PyObject* x);

/** Convert a Python object to a Javascript object.
 *  \param The Python object
 *  \return The Javascript object -- might be an Error object in the case of an
 *     exception.
 */
int
python2js_shallow(PyObject* x);

int
python2js_minimal(PyObject* x);

int
python2js_init();

#endif /* PYTHON2JS_H */
