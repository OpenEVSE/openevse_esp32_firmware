import subprocess
import os
import shutil

def get_build_flag():
    # If git is not available, fall back to environment variables or "unknown"
    if not shutil.which("git"):
        short_hash = os.environ.get('GITHUB_SHA', 'unknown')[:8]
        ref_name = os.environ.get('GITHUB_REF_NAME')
        if ref_name:
            if ref_name.startswith('v'):
                build_version = ref_name
            else:
                build_version = ref_name + '_' + short_hash
        else:
            build_version = 'local_unknown_' + short_hash

        build_flags = '-D BUILD_TAG=' + build_version + ' -D BUILD_HASH=' + short_hash
        return build_flags

    # Use git when available, but tolerate failures and missing info
    try:
        ret = subprocess.run(["git", "rev-parse", "HEAD"], stdout=subprocess.PIPE, text=True, check=False)
        full_hash = ret.stdout.strip() if ret.returncode == 0 else 'unknown'
        ret = subprocess.run(["git", "symbolic-ref", "--short", "HEAD"], stdout=subprocess.PIPE, text=True, check=False)
        branch = ret.stdout.strip() if ret.returncode == 0 else 'unknown'
        short_hash = full_hash[:8] if full_hash != 'unknown' else 'unknown'

        build_version = "local_" + branch + "_" + short_hash

        # get the GITHUB_REF_NAME
        ref_name = os.environ.get('GITHUB_REF_NAME')
        if ref_name:
            if ref_name.startswith("v"):
                build_version = ref_name
            else:
                build_version = ref_name + "_" + short_hash

        # Check if the source has been modified since the last commit
        ret = subprocess.run(["git", "diff-index", "--quiet", "HEAD", "--"], stdout=subprocess.PIPE, text=True)
        if ret.returncode != 0:
            build_version += "_modified"
            short_hash += "_modified"
            full_hash += "_modified"

        build_flags = "-D BUILD_TAG=" + build_version + " -D BUILD_HASH=" + short_hash

        return build_flags
    except Exception:
        return "-D BUILD_TAG=unknown -D BUILD_HASH=unknown"

def get_manifest_flag():
    """-D MIGRATE_MANIFEST_URL for a release build, else nothing.

    The release job publishes migrate_v1_16mb.json (plus the 16MB app and the
    migrator it names) under the release for the ref being built: `latest`
    for master, the tag's own release for a v* tag. The firmware has to look
    for its manifest in the same place, or "Expand to 16 MB" on a new release
    would migrate a user onto whichever 16MB app an older release carried.
    Only refs that get a release are handled here; every other build keeps
    the compile-time default in src/flash_migrate.cpp (the development build).
    """
    ref_name = os.environ.get('GITHUB_REF_NAME')
    if ref_name == 'master':
        tag = 'latest'
    elif ref_name and ref_name.startswith('v'):
        tag = ref_name
    else:
        return ''

    server = os.environ.get('GITHUB_SERVER_URL', 'https://github.com')
    repo = os.environ.get('GITHUB_REPOSITORY', 'OpenEVSE/openevse_esp32_firmware')
    url = '%s/%s/releases/download/%s/migrate_v1_16mb.json' % (server, repo, tag)
    return ' -D MIGRATE_MANIFEST_URL=\\"%s\\"' % url

build_flags = get_build_flag() + get_manifest_flag()

if "SCons.Script" == __name__:
    print ("Firmware Revision: " + build_flags)
    Import("env")
    env.Append(
        BUILD_FLAGS=[build_flags]
    )
elif "__main__" == __name__:
    print(build_flags)
