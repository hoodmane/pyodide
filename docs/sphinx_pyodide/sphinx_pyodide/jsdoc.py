from collections.abc import Iterator

from sphinx_js import ir, typedoc
from sphinx_js.ir import Class
from sphinx_js.typedoc import Analyzer as TsAnalyzer
from sphinx_js.typedoc import Base, Callable, Converter, ReflectionType

# Custom tags are a great way of conveniently passing information from the
# source code to this file. No custom tags will be seen by this code unless they
# are registered in src/js/tsdoc.json
#
# Modifier tags act like a flag, block tags have content.


def has_tag(doclet, tag):
    """Detects whether the doclet comes from a node that has the given modifier
    tag.
    """
    return ("@" + tag) in doclet.modifier_tags


def member_properties(self):
    return dict(
        is_abstract=self.flags.isAbstract,
        is_optional=self.flags.isOptional,
        is_static=self.flags.isStatic,
        is_private=self.flags.isPrivate or self.flags.isExternal,
    )


Base.member_properties = member_properties


def ts_should_destructure_arg(sig, param):
    return param.name == "options"


PYPROXY_METHODS = {}


def ts_post_convert(converter, node, doclet):
    doclet.exported_from = None
    doclet.name = doclet.name.replace("Symbol․Symbol․", "Symbol․")

    if has_tag(doclet, "hidetype"):
        doclet.type = ""
        if isinstance(node, typedoc.Callable):
            node.signatures[0].type = ""

    if isinstance(doclet, ir.Class) and has_tag(doclet, "hideconstructor"):
        doclet.constructor = None

    if node.name == "setStdin":
        fix_set_stdin(converter, node, doclet)

    if node.name == "mountNativeFS":
        fix_native_fs(converter, node, doclet)

    if doclet.deppath == "./js/pyproxy.gen" and doclet.path.segments[-1].endswith(
        "Methods"
    ):
        PYPROXY_METHODS[doclet.name] = doclet.members


def fix_set_stdin(converter, node, doclet):
    assert isinstance(node, Callable)
    options = node.signatures[0].parameters[0]
    assert isinstance(options.type, ReflectionType)
    for param in options.type.declaration.children:
        if param.name == "stdin":
            break
    target = converter.index[param.type.target]
    for docparam in doclet.params:
        if docparam.name == "stdin":
            break
    docparam.type = target.type.render_name(converter)


def fix_native_fs(converter, node, doclet):
    assert isinstance(node, Callable)
    ty = node.signatures[0].type
    target = converter.index[ty.typeArguments[0].target]
    ty.typeArguments[0] = target.type
    doclet.returns[0].type = ty.render_name(converter)


orig_convert_all_nodes = Converter.convert_all_nodes


# locate the ffi fields
FFI_FIELDS: set[str] = set()


def locate_ffi_fields(ffi_module):
    for child in ffi_module.children:
        if child.name == "ffi":
            break
    fields = child.type.declaration.children
    FFI_FIELDS.update(x.name for x in fields)


def children_dict(root):
    return {node.name: node for node in root.children}


def convert_all_nodes(self, root):
    children = children_dict(root)
    locate_ffi_fields(children["js/ffi"])
    return orig_convert_all_nodes(self, root)


Converter.convert_all_nodes = convert_all_nodes


def ts_xref_formatter(self, xref):
    from sphinx_pyodide.mdn_xrefs import JSDATA

    name = xref.name
    if name == "PyodideInterface":
        return ":ref:`PyodideInterface <js-api-pyodide>`"
    if name in JSDATA:
        result = f":js:data:`{name}`"
    elif name in FFI_FIELDS:
        result = f":js:class:`~pyodide.ffi.{name}`"
    else:
        result = f":js:class:`{name}`"
    return result


def doclet_is_private(doclet: ir.TopLevel) -> bool:
    if getattr(doclet, "is_private", False):
        return True
    key = doclet.path.segments
    key = [x for x in key if "/" not in x]
    filename = key[0]
    toplevelname = key[1]
    if key[-1].startswith("$"):
        return True
    if key[-1] == "constructor":
        # For whatever reason, sphinx-js does not properly record
        # whether constructors are private or not. For now, all
        # constructors are private so leave them all off. TODO: handle
        # this via a @private decorator in the documentation comment.
        return True

    if filename in ["module.", "compat."]:
        return True

    if filename == "pyproxy.gen." and toplevelname.endswith("Methods"):
        # Don't document methods classes. We moved them to the
        # corresponding PyProxy subclass.
        return True
    return False


def get_obj_mod(doclet: ir.TopLevel) -> str | None:
    """Search through the doclets generated by JsDoc and categorize them by
    summary section. Skip docs labeled as "@private".
    """
    key = doclet.path.segments
    key = [x for x in key if "/" not in x]
    filename = key[0]
    doclet.name = doclet.name.rpartition(".")[2]

    if filename == "pyodide.":
        return "globalThis"

    if filename == "canvas.":
        return "pyodide.canvas"

    if doclet.name in FFI_FIELDS and not has_tag(doclet, "alias"):
        return "pyodide.ffi"
    doclet.is_static = False
    return "pyodide"


def set_kind(obj: ir.TopLevel) -> None:
    k = obj.block_tags.get("dockind", [None])[0]
    if not k:
        return
    kind = k[0].text.strip()
    if kind == "class":
        kind += "es"
    else:
        kind += "s"
    obj.kind = kind


def fix_pyproxy_class(cls: ir.Class) -> None:
    methods_supers = [x for x in cls.supers if x.segments[-1] in PYPROXY_METHODS]
    cls.supers = [x for x in cls.supers if x.segments[-1] not in PYPROXY_METHODS]
    for x in cls.supers:
        x.segments = [x.segments[-1]]
    for x in methods_supers:
        cls.members.extend(PYPROXY_METHODS[x.segments[-1]])


def _get_toplevel_objects(
    self: TsAnalyzer, ir_objects: list[ir.TopLevel]
) -> Iterator[tuple[ir.TopLevel, str | None, str | None]]:
    for obj in ir_objects:
        if obj.name == "PyodideAPI":
            yield from _get_toplevel_objects(self, obj.members)
            continue
        if doclet_is_private(obj):
            continue
        mod = get_obj_mod(obj)
        set_kind(obj)
        if obj.deppath == "./js/pyproxy.gen" and isinstance(obj, Class):
            fix_pyproxy_class(obj)

        yield obj, mod, obj.kind


TsAnalyzer._get_toplevel_objects = _get_toplevel_objects
