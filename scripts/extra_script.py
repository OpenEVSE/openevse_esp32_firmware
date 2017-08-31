from os.path import join, isfile
import json
from pprint import pprint

from css_html_js_minify.html_minifier import html_minify
#from css_html_js_minify.css_minifier import css_minify
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
    headers = {"Content-type": "application/x-www-form-urlencoded"}
    conn = httplib.HTTPConnection('closure-compiler.appspot.com')
    conn.request('POST', '/compile', params, headers)
    response = conn.getresponse()
    data = response.read()
    conn.close()
    return data

import requests

def css_minify(original, wrap=False, comments=False, sort=True):
    url = 'https://cssminifier.com/raw'
    data = {'input': original }
    response = requests.post(url, data=data)

    return response.text

def minify(env, target, source):
    output = ""
    for source_file in source:
        print("Reading {}".format(source_file))
        file = source_file.get_abspath()
        with open(file) as source_fh:
            original = source_fh.read().decode('utf-8')
        if file.endswith(".css"):
            output += css_minify(original, wrap=False, comments=False, sort=True)
        elif file.endswith(".js"):
            output += js_minify(original)
            #output += original
        elif file.endswith(".htm") or source_file.endswith(".html"):
            output += html_minify(original, comments=False)
    target_file = target[0].get_abspath()
    print("Writing {}".format(target_file))
    with open(target_file, "w") as output_file:
        output_file.write(output.encode('utf-8'))

def process_app_file(source_dir, source_files, dest, env):
    source = []
    for file in source_files:
        source.append(join(source_dir, file))
    env.Depends("$BUILD_DIR/spiffs.bin", env.Command(dest, source, minify))

        #env.VerboseAction('"$PYTHONEXE" "%s" $TARGET %s' % (join(env.subst("$PROJECT_DIR"), "scripts", "minify.py"), " ".join(source)), "Generating $TARGET")))

def process_html_app(source, dest, env):
    manifest = join(source, "manifest.json")
    if isfile(manifest):
        with open(manifest) as manifest_file:
            man = json.load(manifest_file)
        for out_file in man:
            in_files = man[out_file]
            process_app_file(source, in_files, join(dest, out_file), env)

#
# Generate SPIFFS
#

#def before_spiffs(source, target, env):
#    htmlSrc = join(env.subst("$PROJECTSRC_DIR"), "html")
#    process_html_app(htmlSrc, source[0].get_abspath(), env)

#env.AddPreAction("$BUILD_DIR/spiffs.bin", before_spiffs)

htmlSrc = join(env.subst("$PROJECTSRC_DIR"), "html")
dataSrc = join(env.subst("$PROJECTSRC_DIR"), "data")
process_html_app(htmlSrc, dataSrc, env)
