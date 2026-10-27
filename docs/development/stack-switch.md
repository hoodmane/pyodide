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

## Overall Idea

We partition the `PyThreadState` into two groups:

1. Fields that should be shared between all tasks.
2. Fields that are per-task

The core of the API is a new struct called `PyUnstable_ExecStack` which holds
all the per-task fields. We define two APIs to do this:
```C
PyUnstable_ExecStack *PyUnstable_ExecStack_Suspend(PyThreadState *)

int PyUnstable_ExecStack_Resume(PyThreadState *, PyUnstable_ExecStack *target)
```


## Downstream Needs

I'll discuss the shape of the Pyodide and Greenlet consumers. I'll start with
Pyodide because it is simpler.

### Pyodide's Design

A call from JavaScript into Python is either switchable or not. There is a
single main Python task that handles all non-switchable calls.

When we make a switchable call from JavaScript into Python, we call
`create_task()` and when it is done, we call `destroy_task`




