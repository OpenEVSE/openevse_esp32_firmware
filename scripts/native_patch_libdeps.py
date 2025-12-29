Import("env")

# Patch downloaded libdeps in-place for the native host build.
# Rationale: avoid carrying a fork of MicroOcppMongoose just to guard
# SSL-only Mongoose struct fields when MG_ENABLE_SSL=0.

def _patch_file(path: str) -> None:
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            content = f.read()
    except OSError:
        return

    needle = "opts.ssl_ca_cert = ca_string;"
    replacement = (
        "#if MG_ENABLE_SSL\n"
        "    opts.ssl_ca_cert = ca_string;\n"
        "    #else\n"
        "    (void) ca_string;\n"
        "    #endif"
    )

    if replacement in content:
        return

    if needle not in content:
        return

    content = content.replace(needle, replacement)

    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(content)


def _native_libdeps_patches(*args, **kwargs):
    # Only apply these patches for the native environment.
    if env.get("PIOENV") != "native":
        return

    _patch_file(
        ".pio/libdeps/native/MicroOcppMongoose/src/MicroOcppMongooseClient.cpp"
    )


env.AddPreAction("buildprog", _native_libdeps_patches)
