from _pyodide_core import _switch


class Continulet:
    def __eq__(self, other):
        return self is other

    def __init__(self, func, *args, **kwargs):
        if getattr(self, "_func", None) is not None:
            raise RuntimeError("continulet already __init__ialized")
        self._func = func
        self._args = args
        self._kwargs = kwargs
        self._continuation = None
        self._csp = None
        self._finished = False

    def is_pending(self):
        return hasattr(self, "_func") and not self._finished

    def switch(self, value=None, to=None):
        return _switch(self, None, 0, value, to)

    def throw(self, err, to=None):
        return _switch(self, None, 1, err, to)

    # def _switch(self, iserr, value, to=None):
    #     if not hasattr(self, "_func"):
    #         if to is not None:
    #             self = to
    #             to = None
    #         else:
    #             return [iserr, value]

    #     if self._finished:
    #         raise RuntimeError("continulet already finished")
    #     from js import Promise

    #     resolve = None

    #     if to is None:
    #         cont = self._continuation
    #     else:
    #         cont = to._continuation
    #         to._continuation = self._continuation

    #     def store_res(r, _):
    #         nonlocal resolve
    #         self._continuation = r

    #     p = Promise.new(store_res)

    #     if to is not None:
    #         self = to

    #     if cont:
    #         cont(create_proxy([iserr, value]))
    #     else:
    #         if value is not None:
    #             raise TypeError(
    #                 "can't send non-None value to a just-started continulet"
    #             )

    #         def func():
    #             try:
    #                 return [0, self._func(self, *self._args, **self._kwargs)]
    #             except BaseException as e:
    #                 if e.__traceback__:
    #                     e = e.with_traceback(e.__traceback__.tb_next)
    #                 return [1, e]
    #             finally:
    #                 self._finished = True

    #         self._csp = continuletRun(func)[0]

    #     r = continuletPromiseRace(self._csp, p)[0].syncify()
    #     if hasattr(r, "unwrap"):
    #         s = r
    #         r = r.unwrap()
    #         s.destroy()
    #     return r