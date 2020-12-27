#include "runpython.h"
#include "pyproxy.h"
#include "python2js.h"

#include <Python.h>
#include <emscripten.h>
<<<<<<< HEAD

int
runpython_init()
=======
#include <node.h> // from Python

#include "hiwire.h"
#include "python2js.h"

PyObject* globals;
PyObject* pyodide;

_Py_IDENTIFIER(eval_code);
_Py_IDENTIFIER(find_imports);

int
_runPython(char* code)
{
  PyObject* py_code;
  py_code = PyUnicode_FromString(code);
  if (py_code == NULL) {
    return pythonexc2js();
  }

  PyObject* ret = _PyObject_CallMethodIdObjArgs(
    pyodide, &PyId_eval_code, py_code, globals, NULL);

  if (ret == NULL) {
    return pythonexc2js();
  }

  int id = python2js_deep(ret);
  Py_DECREF(ret);
  return id;
}

int
_findImports(char* code)
{
  PyObject* py_code;
  py_code = PyUnicode_FromString(code);
  if (py_code == NULL) {
    return pythonexc2js();
  }

  PyObject* ret =
    _PyObject_CallMethodIdObjArgs(pyodide, &PyId_find_imports, py_code, NULL);

  if (ret == NULL) {
    return pythonexc2js();
  }

  int id = python2js_deep(ret);
  Py_DECREF(ret);
  return id;
}

EM_JS(int, runpython_init_js, (), {
  Module._runPythonInternal = function(pycode)
  {
    var idresult = Module.__runPython(pycode);
    var jsresult = Module.hiwire.get_value(idresult);
    Module.hiwire.decref(idresult);
    _free(pycode);
    return jsresult;
  };

  Module.runPython = function(code)
  {
    var pycode = allocate(intArrayFromString(code), 'i8', ALLOC_NORMAL);
    return Module._runPythonInternal(pycode);
  };

  Module.runPythonAsync = function(code, messageCallback, errorCallback)
  {
    var pycode = allocate(intArrayFromString(code), 'i8', ALLOC_NORMAL);

    var idimports = Module.__findImports(pycode);
    var jsimports = Module.hiwire.get_value(idimports);
    Module.hiwire.decref(idimports);

    var internal = function(resolve, reject)
    {
      try {
        resolve(Module._runPythonInternal(pycode));
      } catch (e) {
        reject(e);
      }
    };

    if (jsimports.length) {
      var packageNames =
        self.pyodide._module.packages.import_name_to_package_name;
      var packages = {};
      for (var i = 0; i < jsimports.length; ++i) {
        var name = jsimports[i];
        // clang-format off
        if (packageNames[name] !== undefined) {
          // clang-format on
          packages[packageNames[name]] = undefined;
        }
      }
      if (Object.keys(packages).length) {
        var runInternal = function() { return new Promise(internal); };
        return Module
          .loadPackage(Object.keys(packages), messageCallback, errorCallback)
          .then(runInternal);
      }
    }
    return new Promise(internal);
  };
});

int
runpython_init_py()
>>>>>>> py2js-variants
{
  PyObject* builtins = PyImport_AddModule("builtins");
  if (builtins == NULL) {
    return 1;
  }

  PyObject* builtins_dict = PyModule_GetDict(builtins);
  if (builtins_dict == NULL) {
    return 1;
  }

  PyObject* __main__ = PyImport_AddModule("__main__");
  if (__main__ == NULL) {
    return 1;
  }

  PyObject* globals = PyModule_GetDict(__main__);
  if (globals == NULL) {
    return 1;
  }

  if (PyDict_Update(globals, builtins_dict)) {
    return 1;
  }

  PyObject* py_pyodide = PyImport_ImportModule("pyodide");
  if (py_pyodide == NULL) {
    return 1;
  }

  int py_pyodide_id = python2js(py_pyodide);
  Py_CLEAR(py_pyodide);
  // Currently by default, python2js copies dicts into objects.
  // We want to feed Module.globals back to `eval_code` in `pyodide.runPython`
  // (see definition in pyodide.js) but because the round trip conversion
  // py => js => py for a dict object is a JsProxy, that causes trouble.
  // Instead we explicitly call pyproxy_new.
  // We also had to add ad-hoc modifications to _pyproxy_get, etc to support
  // this. I (HC) will fix this with the rest of the type conversions
  // modifications.
  int py_globals_id = pyproxy_new(globals);
  EM_ASM(
    {
      Module.py_pyodide = Module.hiwire.get_value($0);
      Module.globals = Module.hiwire.get_value($1);
    },
    py_pyodide_id,
    py_globals_id);
  return 0;
}
