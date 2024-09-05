from sysconfig import get_config_var

from setuptools import Extension, setup

if get_config_var("SIZEOF_VOID_P") != 4:
    # pyodide_build.pypabuild will run this three times, the first time it fails
    # and the exception is caught but the second and third times it must
    # succeed.
    raise Exception(
        """
This should appear in the log exactly one time. If it appears more than once,
the Pyodide build system has misconfigured sysconfigdata (and also the build
will fail).
"""
    )

setup(
    name="pycapsule-example",
    author="Hood Chatham",
    version="0.1",
    long_description_content_type="text/markdown",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    ext_modules=[Extension("pycapsule_example", ["pycapsule-example.c"])],
)
