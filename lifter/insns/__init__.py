"""Instruction families: one module per family, found by listing this directory.

Nothing is listed anywhere.  A module lifter/insns/<name>.py is a family because it is here;
importing it registers its translators with lifter/translate.py (see Family there, and
README.md in this directory).  lifter.translate calls discover() once, when it is imported.
"""

import importlib
import os
import pkgutil


def family_names():
    """Names of the family modules in this directory, sorted."""
    return sorted(m.name for m in pkgutil.iter_modules(__path__) if not m.name.startswith("_"))


def discover():
    """Import every family module, then check that the three places a family lives in agree:
    each module defined the family named after it, and every C header in harness/insns/
    belongs to a family that names it."""
    from .. import translate as T
    names = family_names()
    mods = {name: importlib.import_module("%s.%s" % (__name__, name)) for name in names}
    for name in names:
        # A family module that was imported directly is still half-way through its own import
        # here (it imports lifter.translate first); it registers when that import resumes.
        busy = getattr(getattr(mods[name], "__spec__", None), "_initializing", False)
        if name not in T.FAMILIES and not busy:
            raise T.RegistryError("lifter/insns/%s.py defines no family: it needs 'F = Family(__name__)'" % name)
    for name in sorted(T.FAMILIES):
        if name not in names:
            raise T.RegistryError("family %r is not a module in lifter/insns/" % name)
    if os.path.isdir(T.HARNESS_INSNS):
        for f in sorted(os.listdir(T.HARNESS_INSNS)):
            if f.endswith(".h") and f[:-2] not in T.FAMILIES:
                raise T.RegistryError("harness/insns/%s has no family: there is no lifter/insns/%s.py" % (f, f[:-2]))
    return names
