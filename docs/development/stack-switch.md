# PyUnstable_ExecStack API Proposal

Problem: Greenlet and Pyodide both implement stack switching downstream. It
entails a high maintenance burden for these downstream projects.

## Motivation

To stack switch, we need to take care of three different things:

1. Save/restore appropriate parts of the Python thread state
2. Save/restore appropriate parts of the C stack
3. Switch the native execution stack

(1) is platform-independent but tightly bound to Python interpreter internals.
(2) and (3) are completely independent of Python. (2) can be solved in a generic
way for all platforms, whereas (3) is tightly bound to the platform and requires
an assembly trampoline for most platforms. On the Emscripten target, we use
[WebAssembly/JavaScript Promise Integration](https://blog.pyodide.org/posts/jspi-with-c-runtime/)
to handle (3).

The goal here is to add to Python unstable APIs that take care of (3). 

Greenlet currently has `#ifdef` code for essentially every single Python
version. Pyodide only targets one Python version at a time so it does not need
to maintain a snarl of `#ifdef`s but figuring out how to adapt the code for each
version is a similarly high maintenance burden.s

The hope is that once this API is introduced, Greenlet and Pyodide can switch to
using these APIs and will not need to change much to support new Python APIs.

## API

We partition the `PyThreadState` into two groups:

1. Fields that should be shared between all tasks.
2. Fields that are per-task

All the per-task fields are held in a new struct called `PyUnstable_ExecStack`.
It also has a `state` field that records whether or not it is "active".

The key API is `PyUnstable_ExecStack_Switch`:
```C
int PyUnstable_ExecStack_Switch(
    PyThreadState *tstate,
    PyUnstable_ExecStack *current,
    PyUnstable_ExecStack *target);
```
`PyUnstable_ExecStack_Switch` switches the `ExecStack` by copying the fields in
the current `tstate` into `current` and copying the fields from `target` into
`tstate`. Before calling this function, the `state` of `current` must be
"active" and the state of `target` must be "suspended". After the call, this is
swapped: `current` will be in state "suspended" and `target` will be in state
"active".

There are two APIs to create an `ExecStack`:
```C
PyUnstable_ExecStack *PyUnstable_ExecStack_Adopt(PyThreadState *);
PyUnstable_ExecStack *PyUnstable_ExecStack_New();
```
`PyUnstable_ExecStack_Adopt` creates an "active" `ExecStack` from the given
threadstate. `PyUnstable_ExecStack_New()` creates a "suspended" `ExecStack`.

An `ExecStack` must be freed with `PyUnstable_ExecStack_Free()`. If the
`ExecStack` is owned by a a Python object the following three functions must be
called respectively in `tp_free`, `tp_clear`, and `tp_traverse`:
```C
void PyUnstable_ExecStack_Free(PyUnstable_ExecStack *);
int PyUnstable_ExecStack_Clear(PyUnstable_ExecStack *);
int PyUnstable_ExecStack_Traverse(PyUnstable_ExecStack *, visitproc, void *);
```

The following two APIs are needed by greenlet:

```C
int PyUnstable_ExecStack_IsRunning(PyUnstable_ExecStack *);
PyFrameObject *PyUnstable_ExecStack_GetFrame(PyUnstable_ExecStack *);
```

The following two APIs are used to get and set the asyncio context variables:
```C
PyObject *PyUnstable_ExecStack_GetContext(PyUnstable_ExecStack *);
int  PyUnstable_ExecStack_SetContext(PyUnstable_ExecStack *, PyObject *);
```


## Downstream Needs

I'll discuss the shape of the Pyodide and Greenlet consumers. I'll start with
Pyodide because it is simpler.

### Pyodide's Design

A call from JavaScript into Python is either switchable or not. There is a
single main Python task that handles all non-switchable calls.

When we make a switchable call from JavaScript into Python, we call
`create_task()` and when it is done, we call `destroy_task`




