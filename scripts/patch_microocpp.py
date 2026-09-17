"""
PlatformIO pre-script to patch MicroOcpp library for ArduinoJson v7 compatibility.

MicroOcpp 1.2.0 uses ArduinoJson v6 APIs that are incompatible with v7:
- MemberProxy copy constructor is private in v7 (proxy objects are non-copyable)
  e.g. `payload["connectorId"] < 0` fails because it copies the MemberProxy
- Fix: use `.as<int>()` to extract value before comparison

This script patches the downloaded library source after PlatformIO resolves deps.
It can be removed once MicroOcpp releases a version compatible with ArduinoJson v7.
The proper long-term fix is an OpenEVSE-owned fork of MicroOcpp pinned by SHA, the
way this repo already pins ArduinoMongoose.
"""
import os
import sys

Import("env")

def patch_file(filepath, replacements):
    """Apply text replacements to a file.

    Returns a list of booleans, one per entry in `replacements`, recording
    whether that replacement's search string was found (and applied). Returns
    None if the file does not exist.
    """
    if not os.path.exists(filepath):
        return None

    with open(filepath, 'r') as f:
        content = f.read()

    matched = []
    for old, new in replacements:
        found = old in content
        matched.append(found)
        if found:
            content = content.replace(old, new)

    if matched and all(matched):
        with open(filepath, 'w') as f:
            f.write(content)

    return matched


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
    reserve_now_replacements = [
        (
            '!payload.containsKey("connectorId") ||\n            payload["connectorId"] < 0 ||',
            '(payload["connectorId"] | -1) < 0 ||'
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
    ]
    matched = patch_file(reserve_now, reserve_now_replacements)
    if matched is None:
        # The library is installed but the file we patch is not where it
        # was: the same drift as an unmatched pattern, and just as silent
        # if let through.
        sys.stderr.write(
            "Error: scripts/patch_microocpp.py expected to patch %s for "
            "ArduinoJson v7 but the file does not exist. MicroOcpp was "
            "likely restructured in a new version; update the path in "
            "scripts/patch_microocpp.py (or delete the marker file and "
            "re-check if the patch is even still needed).\n" % reserve_now
        )
        env.Exit(1)
    else:
        if all(matched):
            patched_count += 1
            print("  Patched ReserveNow.cpp")
        else:
            first_unmatched = reserve_now_replacements[matched.index(False)][0]
            sys.stderr.write(
                "Error: scripts/patch_microocpp.py could not apply the "
                "ArduinoJson v7 patch to %s -- one or more expected search "
                "strings no longer match the file. First unmatched pattern:\n"
                "  %r\n"
                "MicroOcpp was likely bumped to a new version; update the "
                "replacements in scripts/patch_microocpp.py to match the new "
                "source (or delete the marker file and re-check if the patch "
                "is even still needed).\n" % (reserve_now, first_unmatched)
            )
            env.Exit(1)

    if patched_count > 0:
        # Create marker to avoid re-patching
        with open(marker, 'w') as f:
            f.write("Patched for ArduinoJson v7\n")
        print("MicroOcpp: patched %d file(s)" % patched_count)


# Run patch immediately (before build starts, after deps are installed)
patch_microocpp_lib(env)
