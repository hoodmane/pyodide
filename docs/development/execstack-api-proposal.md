---
orphan: true
---

# Proposal: a CPython `PyUnstable_ExecStack` API for stack-switching libraries

Status: prototyped and tested. Branches:

- CPython: `execstack-3.14` (worktree `~/cpython-execstack`, off `v3.14.2`).
  `Python/execstack.c`, `Include/cpython/pystate.h`, tests in
  `Lib/test/test_capi/test_execstack.py` driven by a ucontext fiber in
  `Modules/_testcapi/execstack.c`. 27 tests, refleak-clean (`-R 3:3`).
- greenlet: `execstack-api`. Full suite against the patched debug 3.14
  matches the baseline built with the API disabled (`-DGREENLET_NO_EXECSTACK`);
  one known leak (`fails_leakcheck_on_py314_or_less`) is fixed.
- Pyodide: `execstack-api`. Carries the CPython change as
  `cpython/patches/0010-Add-PyUnstable_ExecStack-API.patch`;
  `pystate_pycore.c` is deleted. `test_stack_switching.py` passes on node
  and chrome; core suites unchanged.

The section "What the implementation changed" at the end records the
differences between this document's first draft and what actually works.

## Motivation

Both greenlet (`src/greenlet/TPythonState.cpp`, `TExceptionState.cpp`) and
Pyodide (`src/core/stack_switching/pystate.c`, `pystate_pycore.c`) implement
stackful coroutines by treating `PyThreadState` as two halves:

- a *thread-wide* half (GIL/attach state, `interp`, `dict`, trace/profile
  functions, `async_gen_*`, thread ids, `threading_local_*`, the asyncio
  running loop, ...), and
- a *per-execution-stack* half that must travel with a coroutine.

Each library swaps the second half by poking `PyThreadState` fields directly,
several of which live in `_PyThreadStateImpl` or require internal headers.
Both files carry an "audit this on every Python release" banner for that
reason, and greenlet's version is a ladder of `#if GREENLET_PY3xx`.

Mark Shannon's abandoned [gh-32303](https://github.com/python/cpython/pull/32303)
covered only the datastack. His objection to a separate save/restore pair --
"too many states and error conditions" -- argues for an atomic *swap* model.
Pyodide already uses one (`captureTaskState` / `restoreTaskState` each do
save+load in one step).

## What the two consumers actually do to `tstate`

| Field | greenlet | Pyodide | Notes |
|---|---|---|---|
| `context` + `context_ver++` | yes | yes | strong ref |
| `current_frame` | yes | yes | |
| `datastack_chunk/top/limit` | yes; frees root chunk via arena allocator in `did_finish` | yes; frees via `_PyObject_VirtualFree` (internal header) | new stack starts `NULL`; someone must free the lazily created root chunk |
| `exc_info` / `exc_state` | yes, `NULL` sentinel when `exc_info == &tstate->exc_state` | same trick | |
| `py_recursion_remaining` | as depth (`limit - remaining`) | as remaining | depth is robust to `sys.setrecursionlimit` |
| `delete_later` | yes, with a resurrecting INCREF | yes, borrowed | trashcan chain |
| `current_executor` | yes | yes | JIT; borrowed |
| `tracing`, `what_event` | no | yes | |
| `current_exception` | no (travels with control; this is how `throw` works) | yes (defensively zeroed) | |
| `_PyThreadStateImpl.asyncio_running_task` | no | yes (internal header) | |
| `critical_section` + `_PyCriticalSection_SuspendAll/Resume` | free-threaded only | n/a | internal |
| `_PyThreadStateImpl.c_stack_refs` + strong-ref snapshot for GC | free-threaded only | n/a | internal |
| top frame `stackpointer` | free-threaded 3.14 | no | |
| frame-chain rewriting (`expose_frames` / `unexpose_frames`) | 3.12+, so `gr_frame` / `f_back` are safe while entry frames live on a spilled C stack | **not done** | Pyodide has the same latent hazard: `f_back` on a frame object captured by a suspended task walks `previous` through an entry frame on an evicted wasm shadow stack |
| GC: `tp_traverse` of suspended frames (`_PyGC_VisitFrameStack`, `_Py_VISIT_STACKREF`), `tp_clear` (`PyStackRef_CLEAR` loop) | 3.15 | **not done** (refcounts only; cycles through suspended frames leak) | internal |

Everything marked "internal" is a release-to-release hazard for both
libraries.

## The API

An opaque heap object representing one execution stack (the per-coroutine
half). Each `PyThreadState` has exactly one *running* `ExecStack` at any time;
every other stack is `NEW` or `SUSPENDED`. The one operation that mutates the
thread state is an atomic swap.

```c
/* Include/cpython/pystate.h  (unstable tier) */

typedef struct _PyExecStack PyUnstable_ExecStack;

typedef enum {
    PyUnstable_ExecStack_NEW = 0,
    PyUnstable_ExecStack_RUNNING = 1,
    PyUnstable_ExecStack_SUSPENDED = 2,
} PyUnstable_ExecStack_State;

/* Lifecycle ---------------------------------------------------------------- */

/* A fresh stack: no frames, no datastack, empty exception stack, context =
   Py_XNewRef(context) (NULL => empty context). Recursion depth and tracing
   depth are inherited from the stack being switched *from* on the first
   Switch into it. Returns NULL with MemoryError/TypeError on failure. */
PyAPI_FUNC(PyUnstable_ExecStack *) PyUnstable_ExecStack_New(PyObject *context);

/* New reference to the stack currently running on tstate; lazily creates the
   thread's root stack on first use. Release with Free(). NULL + MemoryError
   on failure.

   Stacks are reference counted: New() and this function each hand out one
   reference; the thread state holds one reference to its root stack. A root
   whose thread has exited becomes a detached, empty SUSPENDED stack that its
   remaining owners can still inspect and free. (Needed because greenlet's
   main greenlet can outlive its thread.) */
PyAPI_FUNC(PyUnstable_ExecStack *) PyUnstable_ThreadState_GetExecStack(PyThreadState *);

/* Release everything the stack owns: context, exc_value, asyncio task,
   datastack chunks, and -- if it is SUSPENDED with live frames -- clear those
   frames the way gen_clear / _PyFrame_ClearExceptCode do. The stack stays
   allocated but can no longer be switched into. -1 + SystemError if RUNNING. */
PyAPI_FUNC(int) PyUnstable_ExecStack_Clear(PyUnstable_ExecStack *);

/* Release a reference; the last release clears and deallocates. -1 +
   SystemError if that would deallocate a RUNNING stack. */
PyAPI_FUNC(int) PyUnstable_ExecStack_Free(PyUnstable_ExecStack *);

/* Switching ---------------------------------------------------------------- */

/* Move the per-stack fields of tstate into the running stack and mark it
   SUSPENDED. Returns that stack (borrowed), or NULL + exception with tstate
   untouched. Must be called while the *outgoing* C stack is still intact:
   the interpreter reads the entry frames it keeps there. Until Resume(), no
   stack is running on tstate and nothing but the low-level C stack switch
   may happen. */
PyAPI_FUNC(PyUnstable_ExecStack *) PyUnstable_ExecStack_Suspend(PyThreadState *);

/* Install `target` (NEW, or SUSPENDED and previously run on tstate) as the
   running stack after a Suspend(). Call once the *incoming* C stack is in
   place. -1 + SystemError on misuse only. */
PyAPI_FUNC(int) PyUnstable_ExecStack_Resume(PyThreadState *, PyUnstable_ExecStack *target);

/* Suspend() + Resume(target) for implementations whose two C stacks are
   both intact at the switch point (separately allocated stacks; Pyodide).
   Returns the previously running stack (borrowed); Switch(current) is a
   no-op. None of these allocate Python objects or run Python code.

   Implementation detail, invisible to callers: this is where critical
   sections are suspended/resumed, c_stack_refs swapped, entry frames
   unlinked/relinked, context_ver bumped, asyncio_running_task swapped. */
PyAPI_FUNC(PyUnstable_ExecStack *) PyUnstable_ExecStack_Switch(
        PyThreadState *tstate, PyUnstable_ExecStack *target);

/* Introspection ------------------------------------------------------------ */

PyAPI_FUNC(PyUnstable_ExecStack_State) PyUnstable_ExecStack_GetState(PyUnstable_ExecStack *);

/* New reference to the topmost *complete* Python frame, or NULL (error
   cleared) if there is none -- same contract as PyThreadState_GetFrame().
   For a SUSPENDED stack the result and its f_back chain are safe to walk;
   CPython is responsible for skipping entry frames. */
PyAPI_FUNC(PyFrameObject *) PyUnstable_ExecStack_GetFrame(PyUnstable_ExecStack *);

PyAPI_FUNC(PyObject *) PyUnstable_ExecStack_GetContext(PyUnstable_ExecStack *); /* new ref or NULL */
PyAPI_FUNC(int)        PyUnstable_ExecStack_SetContext(PyUnstable_ExecStack *, PyObject *);

/* GC integration ----------------------------------------------------------- */

/* For the owning object's tp_traverse. Visits context, exc_value, asyncio
   task, and -- when SUSPENDED -- every thread-owned frame and its evaluation
   stack (same as gen_traverse), plus objects held by C-stack refs captured
   at suspension. Visits only the context while RUNNING or NEW. */
PyAPI_FUNC(int) PyUnstable_ExecStack_Traverse(PyUnstable_ExecStack *, visitproc, void *);
```

### Field classification baked into the implementation

Per-stack (swapped by `Switch`): `current_frame`, `datastack_*`,
`exc_info`/`exc_state`, `py_recursion_remaining` (stored as depth),
`context`/`context_ver`, `delete_later`, `current_executor`, `tracing`,
`what_event`, `critical_section` (with suspend/resume), `c_stack_refs`,
`asyncio_running_task`.

Travels with control (deliberately *not* swapped): `current_exception`. This
is how a library throws into a stack -- `PyErr_SetRaisedException(exc);
Switch(target)` -- and it is what greenlet relies on today. Pyodide's
defensive zeroing becomes an assertion.

Per-thread (not swapped): everything else, including `asyncio_running_loop`,
`c_profilefunc`/`c_tracefunc`, `dict`, `async_gen_*`,
`coroutine_origin_tracking_depth`, and the C-stack bounds. The bounds stay out
because `PyUnstable_ThreadState_SetStackProtection` (3.14) already exists for
implementations that run on separately allocated C stacks; greenlet and
Pyodide both run on slices of the thread's own stack.

### Semantics worth pinning down

- **One running stack per thread**, except in the window between `Suspend`
  and `Resume`, in which there is none and the thread state must not be
  used. `GetExecStack`/`Switch`/`Suspend` fail with `SystemError` in that
  window, and `Resume` fails outside it, so the "too many states" concern
  about a save/restore pair reduces to one well-defined, checkable state.
- **`Switch` is the only place the frame chain is rewired.** On suspend,
  CPython walks the chain while the C stack is still live and, for every
  datastack frame whose `previous` is a C-stack entry frame
  (`owner >= FRAME_OWNED_BY_INTERPRETER`, possibly several in a row when a
  finalizer re-entered the eval loop), records the original link and rewrites
  `previous` to skip the entry frame(s). The raw `current_frame` (which can
  itself transiently be an entry frame during `_PyEval_FrameClearAndPop`) is
  saved separately from the first visible frame. Resume relinks. This is
  greenlet's `expose_frames`, done by code that owns the frame layout.
- **Finishing a coroutine is `Switch` to the parent, then `Free`.** No
  `did_finish` special case running on the dying stack; `Free` releases the
  root datastack chunk.
- **Killing from a dead thread** (greenlet's `murder_in_place` /
  `tp_clear(own_top_frame=true)`) is `Clear`/`Free` on a `SUSPENDED` stack;
  CPython does the stackref clearing top-down so `take_ownership` can still
  link `f_back`.
- **GC model = suspended generator.** A `SUSPENDED` stack is owned by its
  library object and traversed via that object's `tp_traverse`, so cycles
  through it are collectable. It is *not* registered as a thread-stack root
  (that would pin everything a suspended coroutine references). On
  free-threaded builds the C-stack refs that were live at suspension are
  snapshotted as strong references, as greenlet does.
- **Thread exit.** `PyThreadState_Clear` drops the thread state's reference
  to the root stack and detaches whatever stack is running (empty,
  `SUSPENDED`, no thread) so remaining owners can still inspect and `Free`
  it. Without reference counting, greenlet's main greenlet -- which other
  threads' greenlets keep alive -- would hold a dangling pointer between
  `PyThreadState_Clear` and its own thread-local destructor.

## What the consumers become

**greenlet.** `PythonState`, `ExceptionState`, `expose_frames` /
`unexpose_frames`, `set_new_cframe`, `did_finish`, `capture_c_stack_refs`, and
the `#if GREENLET_PY3xx` ladders are not compiled when `Py_HAVE_EXECSTACK` is
defined; each `Greenlet` holds one `PyUnstable_ExecStack*`. `g_switchstack`
calls `Suspend` where `operator<<` used to be (before `slp_switch()`), and
`g_switchstack_success` calls `Resume` where `operator>>` was. `tp_traverse`
calls `Traverse`; `tp_clear` calls `Clear` for dead/unstarted/dead-thread
greenlets and only drops the context for a suspended greenlet in a live
thread (its frames are unwound by the `GreenletExit` thrown at dealloc). The
main greenlet holds a reference from `PyUnstable_ThreadState_GetExecStack`.
`is_currently_running` becomes `GetState() == RUNNING`; `gr_frame` is
`GetFrame` unless running. As on 3.15, `tp_is_gc` is dropped so suspended
greenlets are collectable. Stack copying (`StackState`, `slp_switch`) stays
greenlet's. ~260 lines added, almost all `#if` scaffolding to keep the old
path for older Pythons; the actual new logic is ~60 lines.

**Pyodide.** `pystate.c` + `pystate_pycore.c` reduce to roughly:

```c
static PyUnstable_ExecStack *outer;        /* the event loop's stack */

int enter_promising_task(void) {
    PyObject *ctx = PyContext_CopyCurrent();
    if (!ctx) return -1;
    PyUnstable_ExecStack *task = PyUnstable_ExecStack_New(ctx);
    Py_DECREF(ctx);
    if (!task) return -1;
    outer = PyUnstable_ExecStack_Switch(tstate, task);
    return outer ? 0 : -1;
}
void exit_promising_task(void) {
    PyUnstable_ExecStack *task = PyUnstable_ExecStack_Switch(tstate, outer);
    PyUnstable_ExecStack_Free(task);
}
PyUnstable_ExecStack *captureTaskState(void) { return PyUnstable_ExecStack_Switch(tstate, outer); }
void restoreTaskState(PyUnstable_ExecStack *s) { outer = PyUnstable_ExecStack_Switch(tstate, s); }
```

`pystate_pycore.c` (internal headers) goes away. Pyodide also gains the two
things it currently lacks: safe `f_back` across suspended tasks and GC
traversal of suspended frames (via a small owner object, or by calling
`Traverse` from the JS-side state object's finalizer). The real `pystate.c`
additionally stashes any stray error indicator alongside the stack pointer
in the saved state, preserving the old isolation guarantee.

## What the implementation changed

Things the first draft got wrong or left out, found by making greenlet and
Pyodide actually run:

1. **An atomic swap is not enough for greenlet.** `slp_switch()` overwrites
   the outgoing greenlet's C stack with the incoming one's before any code
   after it runs, and the outgoing chain's entry frames live on that C
   stack. The save must happen before the C stack switch and the load
   after, so `Suspend`/`Resume` are the primitives and `Switch` is sugar.
   Pyodide can use `Switch` because its outer argument-stack region is never
   evicted while a task runs. This is exactly the shape of greenlet's
   existing `operator<<` / `operator>>` split and is why Mark Shannon's
   swap-only proposal would not have fit.
2. **Stacks need a reference count.** greenlet's main greenlet can outlive
   its thread (any greenlet of another thread that has it as `parent` keeps
   it alive) and its `tp_traverse` must not dereference a root stack that
   `PyThreadState_Clear` freed. `GetExecStack` returns a new reference and
   a root whose thread exited becomes a detached, empty `SUSPENDED` stack.
3. **The error indicator travels with control.** greenlet's `throw` works
   by setting the error indicator and switching; so does throwing into a
   never-started greenlet. Making `current_exception` per-stack would have
   broken that, so it is deliberately excluded (and the C-API test covers
   `throw` into both `NEW` and `SUSPENDED` stacks).
4. **`tp_clear` of a live suspended coroutine must not clear its frames.**
   greenlet unwinds them by throwing `GreenletExit` at dealloc so `finally`
   blocks run; `Clear` is for stacks that will never run again. The API is
   fine as designed -- `Clear` plus `SetContext(NULL)` cover both cases --
   but the distinction needs to be in the documentation.
5. **Entry frames can be consecutive and `current_frame` can be one.**
   During `_PyEval_FrameClearAndPop` a finalizer can switch while
   `tstate->current_frame` is the entry frame, and a finalizer that
   re-enters the eval loop produces two adjacent entry frames. The unlink
   pass skips runs of them and keeps the raw `current_frame` separately
   from the first visible frame. Tested.
6. **`stackpointer == NULL` frames** (GH-129236) exist in the suspended
   chain in debug builds; `Traverse` skips their evaluation stack the way
   the free-threaded GC does.
7. **No `DEAD` state** was needed; "finished" is a library-level notion.

## Open questions

1. **Context on `New`**: `NULL` meaning "empty context" (greenlet semantics)
   vs "copy of current" (Pyodide). Proposed: `NULL` = empty; callers pass
   `PyContext_CopyCurrent()` if they want a copy. One line either way.
2. **Inherit vs reset recursion/tracing depth** on first switch into a `NEW`
   stack. Both libraries inherit today; keep that but document it.
3. **`asyncio_running_task`**: Pyodide swaps it, greenlet doesn't. Per-stack
   is semantically right (a task step is lexically inside a stack), and it
   changes greenlet's observable behaviour for gevent+asyncio users -- they
   should confirm.
4. **`tracing`/`what_event`**: swapping is more correct (a trace callback that
   switches shouldn't silence tracing on the other stack) but is a behaviour
   change for greenlet.
5. **Should `Switch` take a `PyThreadState*`** or always use the current one?
   Both callers always use the current one; taking it explicitly matches
   `PyThreadState_EnterTracing`-style APIs and costs nothing.
6. **Free-threaded `critical_section`**: suspending locks on switch is what
   greenlet does today, but a coroutine can observe a critical section
   released and re-taken under it. Document rather than add a knob.
7. **`delete_later`** on a stack that is `Clear`ed while suspended: the
   trashcan chain leaks. Practically unreachable (greenlet's tests cannot
   provoke it on 3.14); note it and move on.
8. **Can `_PyEntryFrame` move onto the datastack** instead of the C stack?
   That would remove the unlink/relink dance entirely and is probably the
   cleanest long-term change; the API above is compatible with either
   implementation.
9. **Error reporting from `Free`/`Clear`**: `int` return is awkward from
   `tp_dealloc`. Alternative is `void` + `Py_FatalError` on misuse. Both
   consumers can only hit the error paths through their own bugs.
10. **Windows / PCbuild and `Misc/stable_abi.toml`**: unstable API, so no
    stable-ABI entry; PCbuild has `Python/execstack.c` added but is
    untested.
11. **Free-threaded builds** compile but were not tested; greenlet's
    `critical_section` suspend/resume and `c_stack_refs` snapshot logic was
    transcribed into `execstack.c`, and greenlet's FT-specific tests
    (`test_issue515_*`, `test_c_stack_refs_suspended_gc`) would be the
    verification.
12. **Should `Suspend` be allowed to fail at all?** Its only failure is
    `PyMem_RawRealloc` for the entry-frame link table. Reserving that at
    `New()` time with a fixed capacity and growing lazily on the running
    stack (where allocation is safe) would make `Suspend` infallible, which
    greenlet's `g_switchstack` would prefer.
