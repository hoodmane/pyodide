#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>


struct _cc;

typedef int FuncType(struct _cc* contents, int arg);

typedef struct _cc {
  FuncType* func;
  int someData;
} CapsuleContents;

static int
do_the_thing1(CapsuleContents* contents, int arg) {
  return contents->someData + arg;
}


static int
do_the_thing2(CapsuleContents* contents, int arg) {
  return contents->someData * arg;
}

static void
capsule_destructor(PyObject* capsule) {
  CapsuleContents* contents = (CapsuleContents*)PyCapsule_GetPointer(capsule, NULL);
  free(capsule);
}


static PyObject*
make_capsule(PyObject* mod, PyObject* varargs) {
  int type = 0;
  int data = 0;
  if (!PyArg_ParseTuple(varargs, "ii:make_capsule", &type, &data)) {
    return NULL;
  }
  if (type != 1 && type != 2) {
    PyErr_SetString(PyExc_ValueError, "Expected type to be 1 or 2");
    return NULL;
  }
  CapsuleContents* contents = (CapsuleContents*)malloc(sizeof(CapsuleContents));
  contents->someData = data;
  if (type == 1) {
    contents->func = do_the_thing1;
  } else {
    contents->func = do_the_thing2;
  }
  return PyCapsule_New(contents, NULL, capsule_destructor);
}


static PyMethodDef Test_Methods[] = {
  { "make_capsule", (PyCFunction)make_capsule, METH_VARARGS },
  { NULL, NULL, 0, NULL }
};

static struct PyModuleDef module = {
  PyModuleDef_HEAD_INIT,
  "pycapsule_example",                   /* name of module */
  "Example of exporting a PyCapsule from Pyodide", /* module documentation, may be NULL */
  -1, /* size of per-interpreter state of the module,
         or -1 if the module keeps state in global variables. */
  Test_Methods
};

PyMODINIT_FUNC
PyInit_pycapsule_example(void)
{
  PyObject* module_object = PyModule_Create(&module);
  return module_object;
}
