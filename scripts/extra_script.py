from os.path import join, isfile, isdir, basename
from os import listdir, system, environ
from pprint import pprint
import hashlib
import pathlib
import glob
import struct
import zlib

Import("env")

# ---------------------------------------------------------------------------
# Patch ArduinoMongoose's MongooseSntpClient so that:
#
#  1. getTime() closes any stale Mongoose UDP connection before starting a
#     new one.  MG_EV_CLOSE may never fire for UDP SNTP, so _nc stays
#     non-NULL after the first sync and silently blocks every subsequent
#     getTime() call (returns false without sending any traffic).
#
#  2. MG_EV_CLOSE only clears _nc when it matches the closing connection
#     so a delayed close of an old connection doesn't clobber a freshly
#     started new one.
#
# This patch is applied every build and is idempotent.
# ---------------------------------------------------------------------------

SNTP_PATCH_MARKER = "MG_F_CLOSE_IMMEDIATELY"  # present only after patching

OLD_GET_TIME = """\
bool MongooseSntpClient::getTime(const char *server, MongooseSntpTimeHandler onTime)
{
  if(NULL == _nc)
  {
    DBUGF("Trying to connect to %s", server);
    _onTime = onTime;

    _nc = mg_sntp_get_time(Mongoose.getMgr(), eventHandler, server, this);
    if(_nc) {
      return true;
    }

    DBUGF("Failed to connect to %s", server);
  }

  return false;
}"""

NEW_GET_TIME = """\
bool MongooseSntpClient::getTime(const char *server, MongooseSntpTimeHandler onTime)
{
  // Close any stale connection — MG_EV_CLOSE may never fire for UDP SNTP,
  // leaving _nc permanently non-NULL and silently blocking all future calls.
  if(_nc != NULL) {
    _nc->flags |= MG_F_CLOSE_IMMEDIATELY;
    _nc = NULL;
  }
  DBUGF("Trying to connect to %s", server);
  _onTime = onTime;
  _nc = mg_sntp_get_time(Mongoose.getMgr(), eventHandler, server, this);
  if(_nc) {
    return true;
  }
  DBUGF("Failed to connect to %s", server);
  return false;
}"""

OLD_EV_CLOSE = """\
    case MG_EV_CLOSE: {
      DBUGF("Connection %p closed", nc);
      _nc = NULL;
      break;
    }"""

NEW_EV_CLOSE = """\
    case MG_EV_CLOSE: {
      DBUGF("Connection %p closed", nc);
      // Only clear _nc if it still refers to THIS connection so a delayed
      // close of an old connection doesn't clobber a newly started one.
      if(_nc == nc) {
        _nc = NULL;
      }
      break;
    }"""

def patch_sntp_client():
    libdeps_dir = join(env.subst("$PROJECT_DIR"), ".pio", "libdeps")
    if not isdir(libdeps_dir):
        return
    for candidate in glob.glob(join(libdeps_dir, "*", "ArduinoMongoose",
                                    "src", "MongooseSntpClient.cpp")):
        try:
            with open(candidate, "r") as f:
                original = f.read()
            if SNTP_PATCH_MARKER in original:
                continue  # already patched
            # Normalize trailing whitespace on each line before matching —
            # some library versions have a trailing space on `if(NULL == _nc)`
            # which would cause an exact-string match to silently fail.
            content = "\n".join(line.rstrip() for line in original.splitlines())
            if OLD_GET_TIME not in content:
                print("Warning: MongooseSntpClient patch target not found in %s" % candidate)
                continue
            content = content.replace(OLD_GET_TIME, NEW_GET_TIME)
            content = content.replace(OLD_EV_CLOSE, NEW_EV_CLOSE)
            with open(candidate, "w") as f:
                f.write(content)
            print("Patched MongooseSntpClient: %s" % candidate)
        except Exception as e:
            print("Warning: Could not patch %s: %s" % (candidate, e))

patch_sntp_client()

# Dump construction environment (for debug purpose)
#print(env.Dump())

# Install pre-requisites
npm_installed = (0 == system("npm --version"))

#
# Dump build environment (for debug)
# print env.Dump()
#print("Current build targets", map(str, BUILD_TARGETS))
#

def get_c_name(source_file):
    return basename(source_file).upper().replace('.', '_').replace('-', '_')

def text_to_header(source_file):
    with open(source_file) as source_fh:
        original = source_fh.read()
    filename = get_c_name(source_file)
    output = "static const char CONTENT_{}[] PROGMEM = ".format(filename)
    lines = original.splitlines()
    if len(lines) > 0:
        for line in lines:
            output += u"\n  \"{}\\n\"".format(line.replace('\\', '\\\\').replace('"', '\\"'))
    else:
        output += "\"\""
    output += ";\n"
    output += "static const char CONTENT_{}_ETAG[] PROGMEM = \"{}\";\n".format(filename, hashlib.sha256(original.encode('utf-8')).hexdigest())
    return output

def _png_chunks(raw):
    off = 8
    while off + 8 <= len(raw):
        (length,) = struct.unpack(">I", raw[off:off + 4])
        yield raw[off + 4:off + 8], raw[off + 8:off + 8 + length]
        off += length + 12


def _png_chunk(chunk_type, data):
    return (struct.pack(">I", len(data)) + chunk_type + data +
            struct.pack(">I", zlib.crc32(chunk_type + data) & 0xffffffff))


def optimise_png(raw):
    """Losslessly shrink a PNG by re-deflating its image data.

    PNG stores its pixels as a zlib stream, and the encoders that produce
    these icons do not use the highest compression setting. Concatenating
    the IDAT chunks and re-deflating that byte stream at level 9, trying
    each strategy and keeping the smallest, typically recovers 15-35% with
    no change whatsoever to the image: the *filtered* scanline data handed
    to zlib is passed through untouched, so this cannot alter a pixel. The
    result is verified to round-trip before it is used.

    Worth ~11KB across the PWA icons, on a 4MB image that has been running
    at ~99.7% of its app partition -- see docs/flash_budget.md.

    Every other chunk (IHDR, PLTE, tRNS, pHYs, ...) is copied through
    unchanged, so nothing but the compression setting differs.
    """
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        return raw

    image_data = b""
    keep = []
    idat_added = False
    try:
        for chunk_type, data in _png_chunks(raw):
            if chunk_type == b"IDAT":
                image_data += data
                if not idat_added:
                    keep.append((chunk_type, None))
                    idat_added = True
            else:
                keep.append((chunk_type, data))
        filtered = zlib.decompress(image_data)
    except Exception:
        return raw  # not something we understand - leave it alone

    best = None
    for strategy in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_RLE):
        deflate = zlib.compressobj(9, zlib.DEFLATED, 15, 9, strategy)
        blob = deflate.compress(filtered) + deflate.flush()
        if best is None or len(blob) < len(best):
            best = blob

    out = b"\x89PNG\r\n\x1a\n"
    for chunk_type, data in keep:
        out += _png_chunk(chunk_type, best if chunk_type == b"IDAT" else data)

    # Refuse to emit anything whose image data does not decompress back to
    # exactly what we were given, and never grow a file.
    check = b"".join(d for t, d in _png_chunks(out) if t == b"IDAT")
    if zlib.decompress(check) != filtered or len(out) >= len(raw):
        return raw

    return out


def binary_to_header(source_file):
    filename = get_c_name(source_file)
    output = "static const char CONTENT_"+filename+"[] PROGMEM = {\n  "
    count = 0

    etag = hashlib.sha256()

    with open(source_file, "rb") as source_fh:
        content = source_fh.read()

    if source_file.lower().endswith(".png"):
        content = optimise_png(content)

    for byte in content:
        output += "0x{:02x}, ".format(byte)
        etag.update(bytes([byte]))
        count += 1
        if 16 == count:
            output += "\n  "
            count = 0

    output += "0x00 };\n"
    output += "static const char CONTENT_{}_ETAG[] PROGMEM = \"{}\";\n".format(filename, etag.hexdigest())
    return output

def data_to_header(env, target, source):
    output = ""
    for source_file in source:
        #print("Reading {}".format(source_file))
        file = source_file.get_abspath()
        if file.endswith(".css") or file.endswith(".js") or file.endswith(".htm") or file.endswith(".html") or file.endswith(".svg") or file.endswith(".json") or file.endswith(".webmanifest"):            output += text_to_header(file)
        else:
            output += binary_to_header(file)
    target_file = target[0].get_abspath()
    print("Generating {}".format(target_file))
    with open(target_file, "w") as output_file:
        output_file.write(output)

def filtered_listdir_scan(dir):
    out_files = []
    for file in listdir(dir):
        path = join(dir, file)
        if isfile(path) and (pathlib.Path(file).suffix in (".html", ".js", ".css", ".json", ".gz", ".png", ".jpg", ".ico", ".woff", ".woff2", ".webmanifest")):            out_files.append(path)
        elif isdir(path):
            out_files.extend(filtered_listdir_scan(path))

    return out_files

def filtered_listdir(dir):
    files = filtered_listdir_scan(dir)

    # Sort files to make sure the order is constant
    files = sorted(files)

    # filter out and GZipped files
    out_files = []
    for file in files:
        if file.endswith(".gz") or file+".gz" not in files:
            file = file.replace(join(dir, ""), "")
            out_files.append(file)

    return out_files

def make_safe(file):
    chars = "\\/`*{}[]()>#+-.!$"
    for c in chars:
        if c in file:
            file = file.replace(c, "_")

    return file

def make_static_web(env, target, source):
    return make_static(env, target, source, "web_server", dist_dir)

def make_static(env, target, source, prefix, files_dir):
    output = ""

    out_files = filtered_listdir(files_dir)

    # include the files
    for out_file in out_files:
        filename = prefix+"."+make_safe(out_file)+".h"
        output += "#include \"{}\"\n".format(filename)

    output += "StaticFile "+prefix+"_static_files[] = {\n"

    for out_file in out_files:
        filetype = None
        compress = out_file.endswith(".gz")
        out_file = out_file.replace("\\","/") # Windows: out_file generated with \ as directory separator
        if out_file.endswith(".css") or out_file.endswith(".css.gz"):
            filetype = "CSS"
        elif out_file.endswith(".js") or out_file.endswith(".js.gz"):
            filetype = "JS"
        elif out_file.endswith(".htm") or out_file.endswith(".html") or out_file.endswith(".htm.gz") or out_file.endswith(".html.gz"):
            filetype = "HTML"
        elif out_file.endswith(".jpg"):
            filetype = "JPEG"
        elif out_file.endswith(".png"):
            filetype = "PNG"
        elif out_file.endswith(".ico"):
            filetype = "ICO"
        elif out_file.endswith(".svg") or out_file.endswith(".svg.gz"):
            filetype = "SVG"
        elif out_file.endswith(".json") or out_file.endswith(".json.gz"):
            filetype = "JSON"
        elif out_file.endswith(".woff"):
            filetype = "WOFF"
        elif out_file.endswith(".woff2"):
            filetype = "WOFF2"
        elif out_file.endswith(".webmanifest"):
            filetype = "MANIFEST"

        if filetype is not None:
            c_name = get_c_name(out_file)
            # NULL means "served as-is"; otherwise this is the Content-Encoding
            # the bytes on disk were written with.
            encoding = "\"gzip\"" if compress else "NULL"
            output += "  { \"/"+out_file.replace(".gz","")+"\", CONTENT_"+c_name+", sizeof(CONTENT_"+c_name+") - 1, _CONTENT_TYPE_"+filetype+", CONTENT_"+c_name+"_ETAG, "+encoding+" },\n"
        else:
            print("Warning: Could not detect filetype for %s" % (out_file))

    output += "};\n"

    target_file = target[0].get_abspath()
    print("Generating {}".format(target_file))
    with open(target_file, "w") as output_file:
        output_file.write(output)

def process_html_app(source, dest, env, prefix, static_func):
    web_server_static_files = join(dest, prefix+"_static_files.h")
    web_server_static = join(env.subst("$BUILD_DIR"), "src/"+prefix+"_static.cpp.o")

    files = filtered_listdir(source)

    for file in files:
        data_file = join(source, file)
        header_file = join(dest, prefix+"."+make_safe(file)+".h")
        env.Command(header_file, data_file, data_to_header)
        env.Depends(web_server_static_files, header_file)

    env.Depends(web_server_static, env.Command(web_server_static_files, source, static_func))

#
# Generate Web app resources
#
if npm_installed:
    headers_src = join(env.subst("$PROJECTSRC_DIR"), "web_static")

    gui_name = environ.get("GUI_NAME")
    if gui_name in (None, ""):
        # Default web UI for this fork. Override with GUI_NAME=gui-v2 to build
        # the upstream gui-v2 instead. Both are submodules (see .gitmodules).
        gui_name = "gui-nightshift"

    gui_dir = join(env.subst("$PROJECT_DIR"), gui_name)
    dist_dir = join(gui_dir, "dist")
    node_modules = join(gui_dir, "node_modules")

    # Check the GUI dir has been checked out
    if(isfile(join(gui_dir, "package.json"))):
        # Check to see if the Node modules have been downloaded
        if(isdir(node_modules)):
            if(isdir(dist_dir)):
                process_html_app(dist_dir, headers_src, env, "web_server", make_static_web)
            else:
                print("Warning: GUI not built, run 'cd %s; npm run build'" % (gui_dir))
        else:
            print("Warning: GUI dependencies not found, run 'cd %s; npm install'" % (gui_dir))
    else:
        print("Warning: GUI files not found, run 'git submodule update --init' (%s)" % (gui_dir))
else:
  print("Warning: Node.JS and NPM required to update the UI")

print("PATH="+env['ENV']['PATH'])
