import os.path
import json
from pprint import pprint

from css_html_js_minify.html_minifier import html_minify
from css_html_js_minify.css_minifier import css_minify
#from css_html_js_minify.js_minifier import js_minify

Import("env")

#
# Dump build environment (for debug)
#print env.Dump()
#print("Current build targets", map(str, BUILD_TARGETS))
#

import httplib, urllib, sys
def js_minify(original):
    params = urllib.urlencode([
        ('js_code', original),
        ('compilation_level', 'SIMPLE_OPTIMIZATIONS'),
        ('output_format', 'text'),
        ('output_info', 'compiled_code'),
    ])

    # Always use the following value for the Content-type header.
    headers = { "Content-type": "application/x-www-form-urlencoded" }
    conn = httplib.HTTPConnection('closure-compiler.appspot.com')
    conn.request('POST', '/compile', params, headers)
    response = conn.getresponse()
    data = response.read()
    conn.close()
    return data

def process_app_file(source_dir, source_files, dest):
    output = ""
    for file in source_files:
        source_file = os.path.join(source_dir, file)
        print("Reading {}".format(source_file))
        with open(source_file) as source_fh:
            original = source_fh.read().decode('utf-8')
        if file.endswith(".css"):
            output += css_minify(original, wrap=False, comments=False, sort=True)
        elif file.endswith(".js"):
            output += js_minify(original)
            #output += original
        elif file.endswith(".htm") or file.endswith(".html"):
            output += html_minify(original, comments=False)
    print("Writing {}".format(dest))
    with open(dest, "w") as output_file:
        output_file.write(output.encode('utf-8'))

def process_html_app(source, dest):
    manifest = os.path.join(source, "manifest.json")
    if os.path.isfile(manifest):
        with open(manifest) as manifest_file:
            man = json.load(manifest_file)
        for out_file in man:
            in_files = man[out_file]
            process_app_file(source, in_files, os.path.join(dest, out_file))


#
# Generate SPIFFS
#

def before_spiffs(source, target, env):
    htmlSrc = os.path.join(env.subst("$PROJECTSRC_DIR"), "html")
    process_html_app(htmlSrc, source[0].get_abspath())

env.AddPreAction("$BUILD_DIR/spiffs.bin", before_spiffs)
