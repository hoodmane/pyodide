import re
import subprocess
from pathlib import Path


def fix_f2c_input(f2c_input_path: str) -> None:
    f2c_input = Path(f2c_input_path)
    with open(f2c_input) as f:
        lines = f.readlines()
    new_lines = []
    for line in lines:
        if f2c_input_path.endswith("_flapack-f2pywrappers.f"):
            line = line.replace("character cmach", "integer cmach")
            line = line.replace("character norm", "integer norm")

        if f2c_input.name in [
            "_lapack_subroutine_wrappers.f",
            "_blas_subroutine_wrappers.f",
        ]:
            line = line.replace("character", "integer")
            line = line.replace("ret = chla_transtype(", "call chla_transtype(ret, 1,")

        new_lines.append(line)

    with open(f2c_input_path, "w") as f:
        f.writelines(new_lines)


def fix_f2c_output(f2c_output_path: str) -> str | None:
    """
    This function is called on the name of each C output file. It fixes up the C
    output in various ways to compensate for the lack of f2c support for Fortran
    90 and Fortran 95.
    """
    f2c_output = Path(f2c_output_path)
    with open(f2c_output) as f:
        lines = f.readlines()

    if f2c_output.name == "_lapack_subroutine_wrappers.c":
        lines = [
            line.replace("integer chla_transtype__", "void chla_transtype__")
            for line in lines
        ]

    if "PROPACK" in str(f2c_output):
        if f2c_output.name.endswith("lansvd.c"):
            lines.append(
                """
                #include <time.h>

                int second_(real *t) {
                    *t = clock()/1000;
                    return 0;
                }
                """
            )

    # Fix signature of c_abs to match the OpenBLAS one
    if "REVCOM.c" in str(f2c_output):
        lines = [line.replace("double c_abs(", "float c_abs(") for line in lines]

    with open(f2c_output, "w") as f:
        f.writelines(lines)

    return None


def scipy_fix_cfile(path: str) -> None:
    """
    Replace void return types with int return types in various generated .c and
    .h files. We can't achieve this with a simple patch because these files are
    not in the sdist, they are generated as part of the build.
    """
    source_path = Path(path)
    text = source_path.read_text()
    text = text.replace("extern void F_WRAPPEDFUNC", "extern int F_WRAPPEDFUNC")
    text = text.replace("extern void F_FUNC", "extern int F_FUNC")
    text = text.replace("void (*f2py_func)", "int (*f2py_func)")
    text = text.replace("static void cb_", "static int cb_")
    text = text.replace("typedef void(*cb_", "typedef int(*cb_")
    text = text.replace("void(*)", "int(*)")
    text = text.replace("static void f2py_setup_", "static int f2py_setup_")

    if path.endswith("_flapackmodule.c"):
        text = text.replace(",size_t", "")
        text = re.sub(r",slen\([a-z]*\)\)", ")", text)

    if path.endswith("stats/statlib/spearman.c"):
        # in scipy/stats/statlib/swilk.f ALNORM is called with a double, and in
        # scipy/stats/statlib/spearman.f with a real this generates
        # inconsistent signature. Let's use double in both, I don't think this
        # code path will work (but at least it will compile) since it needs
        # "ALNORM = algorithm AS66", which I don't think we have with the f2c
        # route
        text = text.replace("extern real alnorm_", "extern doublereal alnorm_")

    source_path.write_text(text)

    for lib in ["lapack", "blas"]:
        if path.endswith(f"cython_{lib}.c"):
            header_name = f"_{lib}_subroutines.h"
            header_dir = Path(path).parent
            header_path = find_header(header_dir, header_name)

            header_text = header_path.read_text()
            header_text = header_text.replace("void F_FUNC", "int F_FUNC")
            header_path.write_text(header_text)


def find_header(source_dir: Path, header_name: str) -> Path:
    """
    Find the header file that corresponds to a source file.
    """
    while not (header_path := source_dir / header_name).exists():
        # meson copies the source files into a subdirectory of the build
        source_dir = source_dir.parent
        if source_dir == Path("/"):
            raise RuntimeError(f"Could not find header file {header_name}")

    return header_path


def scipy_fixes(args: list[str]) -> None:
    for arg in args:
        if arg.endswith(".c"):
            scipy_fix_cfile(arg)


def replay_f2c(
    f2c_path: str, args: list[str], dryrun: bool = False
) -> list[str] | None:
    """Apply f2c to compilation arguments

    Parameters
    ----------
    args
       input compiler arguments
    dryrun
       if False run f2c on detected fortran files

    Returns
    -------
    new_args
       output compiler arguments


    Examples
    --------

    >>> replay_f2c(['gfortran', 'test.f'], dryrun=True)
    ['gcc', 'test.c']
    """

    new_args = ["gcc"]
    found_source = False
    for arg in args[1:]:
        if arg.endswith(".f") or arg.endswith(".F"):
            filepath = Path(arg).resolve()
            if not dryrun:
                fix_f2c_input(arg)
                if arg.endswith(".F"):
                    # .F files apparently expect to be run through the C
                    # preprocessor (they have #ifdef's in them)
                    # Use gfortran frontend, as gcc frontend might not be
                    # present on osx
                    # The file-system might be not case-sensitive,
                    # so take care to handle this by renaming.
                    # For preprocessing and further operation the
                    # expected file-name and extension needs to be preserved.
                    subprocess.check_call(
                        [
                            "gfortran",
                            "-E",
                            "-C",
                            "-P",
                            filepath,
                            "-o",
                            filepath.with_suffix(".f77"),
                        ]
                    )
                    filepath = filepath.with_suffix(".f77")
                # -R flag is important, it means that Fortran functions that
                # return real e.g. sdot will be transformed into C functions
                # that return float. For historic reasons, by default f2c
                # transform them into functions that return a double. Using -R
                # allows to match what OpenBLAS has done when they f2ced their
                # Fortran files, see
                # https://github.com/xianyi/OpenBLAS/pull/3539#issuecomment-1493897254
                # for more details
                with (
                    open(filepath) as input_pipe,
                    open(filepath.with_suffix(".c"), "w") as output_pipe,
                ):
                    subprocess.check_call(
                        [f2c_path, "-R"],
                        stdin=input_pipe,
                        stdout=output_pipe,
                        cwd=filepath.parent,
                    )
                fix_f2c_output(arg[:-2] + ".c")
            new_args.append(arg[:-2] + ".c")
            found_source = True
        else:
            new_args.append(arg)

    new_args_str = " ".join(args)
    if ".so" in new_args_str and "libgfortran.so" not in new_args_str:
        found_source = True

    if not found_source:
        return None
    return new_args
