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

build_flags = get_build_flag()

if "SCons.Script" == __name__:
    print ("Firmware Revision: " + build_flags)
    Import("env")
    env.Append(
        BUILD_FLAGS=[get_build_flag()]
    )
elif "__main__" == __name__:
    print(build_flags)
