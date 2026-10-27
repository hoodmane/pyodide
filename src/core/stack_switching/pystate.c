// Note: This file manages the Python thread state when stack switching.
//
// captureTaskState / restoreTaskState are called from JS, in saveState and
// restoreState in suspenders.c. enter_promising_task / exit_promising_task are
// called from C, in _pyproxy_apply_promising and run_main_promising.
//
// There is exactly one PyThreadState and it is always the current thread state.
// Every in-flight "promising task" owns a PyUnstable_ExecStack: the per-task
// part of the thread state (frame chain and the memory it lives in, exception
// stack, contextvars context, recursion depth, ...). The interpreter decides
// which fields those are and moves them in and out of the thread state for us;
// all we do here is keep track of which stack should be running.
//
// Keeping one thread state is important because some extensions store the
// thread state pointer in a TLS variable and assume it will remain valid since
// they reason there is only one thread state per thread. In our single threaded
// build, TLS is turned into normal global variables so to make these extensions
// work there can only be one thread state. See
// https://github.com/pyodide/pyodide/issues/6479.
//
// Why we can use PyUnstable_ExecStack_Switch() (an atomic suspend+resume)
// rather than the split Suspend()/Resume() that greenlet needs: the
// interpreter reads the entry frames it keeps on the C stack of the stack
// being suspended. Our C ("argument") stack region for the outer, event-loop
// stack lies above Module.stackStop and is never evicted while a task is
// running, and a task's own region is restored by StackState.restore() before
// restoreTaskState() runs. So at both switch points both C stacks are intact.

#include <stdbool.h>
#include <stdlib.h>

#include "Python.h"
#include "emscripten.h"
#include "error_handling.h"

int pystate_keepalive;

/**
 * What saveState keeps for a suspended task.
 *
 * The error indicator is deliberately not part of a PyUnstable_ExecStack (it
 * travels with control so that libraries can throw into a stack). We never
 * expect one to be set when a task suspends, but if it is, keep it with the
 * task rather than letting it leak into the event loop.
 */
typedef struct
{
  PyUnstable_ExecStack* stack;
  PyObject* exception; // Strong reference or NULL.
} TaskState;

/**
 * `outer` is the execution stack of the event loop, i.e. of everything that is
 * not a promising task. The interpreter does not track execution stacks
 * itself, so we create this one ourselves; it adopts the thread's live state
 * the first time a task is entered.
 *
 * `current_task` is the stack of the task that is executing, or NULL when the
 * event loop is. A task can only ever take control from the event loop and
 * always returns control to the event loop. It is a bug to start or resume a
 * task when current_task is set.
 */
static PyUnstable_ExecStack* outer = NULL;
static PyUnstable_ExecStack* current_task = NULL;

static PyUnstable_ExecStack*
get_outer(void)
{
  if (outer == NULL) {
    outer = PyUnstable_ExecStack_New(NULL);
  }
  return outer;
}

/**
 * Start a new task. The task runs on the same thread state as its caller but
 * with a frame chain, exception stack, and contextvars context of its own. The
 * caller's stack is remembered in `outer` and resumed when the task suspends
 * or exits.
 *
 * The task's context is a copy of the caller's context, so contextvar changes
 * made by the task stay local to it.
 */
int
enter_promising_task(void)
{
  if (current_task != NULL) {
    // Entering a promising call yields to the event loop first so this should
    // not be reachable.
    PyErr_SetString(PyExc_SystemError,
                    "Cannot enter a promising task from inside another running "
                    "promising task. This is a bug in Pyodide.");
    return -1;
  }
  PyObject* context = PyContext_CopyCurrent();
  if (context == NULL) {
    return -1;
  }
  PyUnstable_ExecStack* task = PyUnstable_ExecStack_New(context);
  Py_DECREF(context);
  if (task == NULL) {
    return -1;
  }
  PyThreadState* tstate = PyThreadState_Get();
  if (get_outer() == NULL ||
      PyUnstable_ExecStack_Switch(tstate, outer, task) < 0) {
    PyUnstable_ExecStack_Free(task);
    return -1;
  }
  current_task = task;
  return 0;
}

/**
 * Finish the current task and put back the caller's state.
 *
 * Any pending exception must have been moved out of the thread state before
 * calling this, see _pyproxy_apply_promising. (The error indicator is not part
 * of an execution stack: it travels with control across a switch.)
 */
void
exit_promising_task(void)
{
  assert(current_task != NULL);
  PyThreadState* tstate = PyThreadState_Get();
  PyUnstable_ExecStack* task = current_task;
  if (PyUnstable_ExecStack_Switch(tstate, task, outer) < 0) {
    Py_FatalError("exit_promising_task: failed to switch back to the event "
                  "loop's stack. This is a bug in Pyodide.");
  }
  current_task = NULL;
  // The task's Python code has returned, so its frame chain is empty; this
  // releases its context and the datastack memory.
  PyUnstable_ExecStack_Free(task);
}

/**
 * Suspend the current task: move its state off the thread state and resume the
 * event loop's stack. saveState stores the returned pointer in the JS state
 * object and passes it back to restoreTaskState when the task resumes.
 *
 * With the task's frames off the thread state, a suspended task looks like
 * Python code that isn't running, which is what the GC and other things that
 * walk current_frame expect. The task's objects are kept alive by their
 * reference counts. (The interpreter also makes the suspended frames safe to
 * walk via f_back, by unlinking the entry frames that live on our argument
 * stack, which may be evicted while the task is suspended.)
 */
EMSCRIPTEN_KEEPALIVE TaskState*
captureTaskState(void)
{
  if (current_task == NULL) {
    // validSuspender is supposed to prevent this. If it happens anyway, refuse
    // rather than corrupting the thread state.
    PyErr_SetString(PyExc_SystemError,
                    "Cannot stack switch: not inside a promising task. This is "
                    "a bug in Pyodide.");
    return NULL;
  }
  TaskState* saved = malloc(sizeof(TaskState));
  if (saved == NULL) {
    PyErr_NoMemory();
    return NULL;
  }
  // Take any stray exception along with the task, see TaskState.
  saved->exception = PyErr_GetRaisedException();
  saved->stack = current_task;
  PyThreadState* tstate = PyThreadState_Get();
  if (PyUnstable_ExecStack_Switch(tstate, current_task, outer) < 0) {
    // The switch failed and the thread state is untouched. Put things back
    // the way they were, but let the switch's error win.
    Py_XDECREF(saved->exception);
    free(saved);
    return NULL;
  }
  current_task = NULL;
  return saved;
}

/**
 * Resume a suspended task: remember the event loop's stack in `outer` and
 * install the task's. Frees `saved`.
 */
EMSCRIPTEN_KEEPALIVE void
restoreTaskState(TaskState* saved)
{
  assert(current_task == NULL);
  PyThreadState* tstate = PyThreadState_Get();
  if (PyUnstable_ExecStack_Switch(tstate, outer, saved->stack) < 0) {
    Py_FatalError("restoreTaskState: failed to switch to the task's stack. "
                  "This is a bug in Pyodide.");
  }
  current_task = saved->stack;
  if (saved->exception != NULL) {
    PyErr_SetRaisedException(saved->exception);
  }
  free(saved);
}
