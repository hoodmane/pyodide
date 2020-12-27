PyObject* ConversionError;
PyObject* Js2PyConversionError;
PyObject* Py2JsConversionError;

int
errors_init()
{
  PyObject* module = PyImport_ImportModule("pyodide");
  if (module == NULL) {
    goto fail;
  }
  ConversionError =
    PyErr_NewException("pyodide.ConversionError", PyExc_Exception, NULL);
  Js2PyConversionError =
    PyErr_NewException("pyodide.Js2PyConversionError", ConversionError, NULL);
  Py2JsConversionError =
    PyErr_NewException("pyodide.Py2JsConversionError", ConversionError, NULL);

  if (PyObject_SetAttrString(module, "ConversionError", ConversionError)) {
    goto fail;
  }
  if (PyObject_SetAttrString(
        module, "Js2PyConversionError", Js2PyConversionError)) {
    goto fail;
  }
  if (PyObject_SetAttrString(
        module, "Py2JsConversionError", Py2JsConversionError)) {
    goto fail;
  }

  Py_CLEAR(module);
  return 0;
fail:
  Py_CLEAR(module);
  return -1;
}
