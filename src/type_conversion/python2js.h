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
python2js(PyObject* x);

int
python2js_init();

#endif /* PYTHON2JS_H */
