import subprocess
import os

def get_build_flag():
    ret = subprocess.run(["git", "rev-parse", "HEAD"], stdout=subprocess.PIPE, text=True) #Uses any tags
    full_hash = ret.stdout.strip()
    short_hash = full_hash[:8]

    build_version = "local_" + short_hash

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

    build_flags = "-D BUILD_TAG=" + build_version + " " + "-D BUILD_HASH=" + short_hash + ""

    return build_flags

build_flags = get_build_flag()

if "SCons.Script" == __name__:
    print ("Firmware Revision: " + build_flags)
    Import("env")
    env.Append(
        BUILD_FLAGS=[get_build_flag()]
    )
elif "__main__" == __name__:
    print(build_flags)
