Import("env")
import SCons.Util.sctypes
import os
import sys

# 1. Fix Windows SCons 4.11 bug:
# Under Python 3.12+ and SCons 4.11 on Windows, SCons' internal `_Null` object
# crashes when concatenating strings:
# TypeError: unsupported operand type(s) for +: '_Null' and 'str'
# Monkey-patching __add__ and __radd__ allows SCons to seamlessly treat _Null as an empty string.
SCons.Util.sctypes._Null.__add__ = lambda self, other: "" + str(other)
SCons.Util.sctypes._Null.__radd__ = lambda self, other: str(other) + ""

# 2. Auto-bundle web dashboard into include/wifi-webpage-gz.h before build:
# Automatically inlines and gzips web/ (index.html, style.css, scaler.js, app.js)
# so developers never have to manually run bundle_web.py after updating web assets.
try:
    project_dir = env.get("PROJECT_DIR", ".")
    scripts_dir = os.path.join(project_dir, "scripts")
    if os.path.exists(os.path.join(scripts_dir, "bundle_web.py")):
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)
        import bundle_web
        bundle_web.bundle_web()
except Exception as e:
    print(f"[pre_build] Warning: Auto-bundle failed: {e}")
