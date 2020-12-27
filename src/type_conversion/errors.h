#ifndef ERRORS_H
#define ERRORS_H

extern PyObject* ConversionError;
extern PyObject* Js2PyConversionError;
extern PyObject* Py2JsConversionError;

int
errors_init();

#endif // ERRORS_H
