"""
PlatformIO pre-script to patch MicroOcpp library for ArduinoJson v7 compatibility.

MicroOcpp 1.2.0 uses ArduinoJson v6 APIs that are incompatible with v7:
- MemberProxy copy constructor is private in v7 (proxy objects are non-copyable)
  e.g. `payload["connectorId"] < 0` fails because it copies the MemberProxy
- Fix: use `.as<int>()` to extract value before comparison

This script patches the downloaded library source after PlatformIO resolves deps.
It can be removed once MicroOcpp releases a version compatible with ArduinoJson v7.
"""
import os
import re

Import("env")

def patch_file(filepath, replacements):
    """Apply text replacements to a file. Returns True if any changes made."""
    if not os.path.exists(filepath):
        return False

    with open(filepath, 'r') as f:
        content = f.read()

    original = content
    for old, new in replacements:
        content = content.replace(old, new)

    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False


def patch_microocpp_lib(env):
    """Patch MicroOcpp source files for ArduinoJson v7 compatibility."""
    lib_deps_dir = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),
        env.subst("$PIOENV")
    )

    if not os.path.isdir(lib_deps_dir):
        return

    # Find MicroOcpp directory
    microocpp_dir = None
    for d in os.listdir(lib_deps_dir):
        if d.startswith("MicroOcpp") and not d.startswith("MicroOcppMongoose"):
            candidate = os.path.join(lib_deps_dir, d)
            if os.path.isdir(candidate):
                microocpp_dir = candidate
                break

    if not microocpp_dir:
        return

    marker = os.path.join(microocpp_dir, ".patched_for_arduinojson_v7")
    if os.path.exists(marker):
        return  # Already patched

    print("Patching MicroOcpp for ArduinoJson v7 compatibility...")
    patched_count = 0

    # Patch ReserveNow.cpp:
    # payload["connectorId"] < 0 -> (payload["connectorId"] | -1) < 0
    # payload.containsKey("x") -> payload["x"].is<JsonVariantConst>()
    reserve_now = os.path.join(
        microocpp_dir, "src", "MicroOcpp", "Operations", "ReserveNow.cpp"
    )
    if patch_file(reserve_now, [
        (
            '!payload.containsKey("connectorId") ||\n            payload["connectorId"] < 0 ||',
            '!(payload["connectorId"] | -1) >= 0 ||'
        ),
        (
            '!payload.containsKey("expiryDate")',
            '!payload["expiryDate"].is<const char*>()'
        ),
        (
            '!payload.containsKey("idTag")',
            '!payload["idTag"].is<const char*>()'
        ),
        (
            '!payload.containsKey("reservationId")',
            '!payload["reservationId"].is<int>()'
        ),
    ]):
        patched_count += 1
        print("  Patched ReserveNow.cpp")

    if patched_count > 0:
        # Create marker to avoid re-patching
        with open(marker, 'w') as f:
            f.write("Patched for ArduinoJson v7\n")
        print("MicroOcpp: patched %d file(s)" % patched_count)


# Run patch immediately (before build starts, after deps are installed)
patch_microocpp_lib(env)
