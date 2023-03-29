import subprocess
import os

Import("env")



def get_build_flag():
    #ret = subprocess.run(["git", "describe"], stdout=subprocess.PIPE, text=True) #Uses only annotated tags
    ret = subprocess.run(["git", "describe", "--tags"], stdout=subprocess.PIPE, text=True) #Uses any tags
    build_version = ret.stdout.strip()
    build_flag = "-D BUILD_TAG=" + build_version
    print ("Firmware Revision: " + build_flag)
    return (build_flag)

env.Append(
    BUILD_FLAGS=[get_build_flag()]
)