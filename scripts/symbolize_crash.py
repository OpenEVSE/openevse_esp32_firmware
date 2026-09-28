#!/usr/bin/env python3
"""Symbolize a crash summary downloaded from a charger.

Takes the JSON that Settings -> Developer Tools -> "Download summary" writes and
prints named frames:

    curl -s http://<charger>/debug/crash > summary.json
    python scripts/symbolize_crash.py summary.json \\
        --endpoint https://<api>/v1/reports

The device reports addresses plus `elf_sha256`; the ELF that names them lives in
the crash service under that hash, uploaded by CI. A summary from a local build
comes back `unsymbolized` -- that is expected, not an error.
"""
import argparse
import json
import sys
import urllib.request

# The broker's own work is capped at 29 s, and a device answers /config in well
# under that; either read hanging past this is a failure worth reporting.
TIMEOUT = 45


def build_request(summary, config):
    return {
        # From the SUMMARY, not the config: it identifies the build that
        # crashed, which is not necessarily the build running now.
        'elf_sha256': summary.get('elf_sha256', ''),
        'bt': summary.get('bt'),
        # Neither the summary nor /config says which version crashed, and the
        # device may have been updated since. The firmware's own uploader sends
        # 'unknown' plus running_version in exactly this case; so does this.
        'version': 'unknown',
        'running_version': config.get('version', ''),
        # The board, which an OTA update does not change.
        'buildenv': config.get('buildenv', ''),
        'chip_id': config.get('chip_id', 'unknown'),
        'summary': summary,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Symbolize an OpenEVSE crash summary.')
    ap.add_argument('summary', help='coredump-summary.json from the web UI')
    ap.add_argument('--endpoint', required=True,
                    help='crash service /v1/reports URL')
    ap.add_argument('--host', help='device address, to fetch /config metadata')
    ap.add_argument('--config', help='a saved /config JSON instead of --host')
    args = ap.parse_args(argv)

    summary = json.load(open(args.summary))
    if not summary.get('present', True):
        print('no crash dump stored on that device', file=sys.stderr)
        return 1

    config = {}
    if args.config:
        config = json.load(open(args.config))
    elif args.host:
        with urllib.request.urlopen('http://%s/config' % args.host,
                                    timeout=TIMEOUT) as r:
            config = json.load(r)

    body = json.dumps(build_request(summary, config)).encode()
    rq = urllib.request.Request(args.endpoint, data=body,
                                headers={'content-type': 'application/json'})
    with urllib.request.urlopen(rq, timeout=TIMEOUT) as r:
        result = json.load(r)

    print('status: %s' % result['status'])
    if result['status'] == 'unsymbolized':
        print('  no archived ELF for %s'
              % (summary.get('elf_sha256') or '(none reported)'))
    elif result['status'] == 'no-backtrace':
        print('  %s' % (summary.get('bt') or 'no backtrace in this summary'))
    for f in result.get('frames', []):
        print('  %s  %s  %s:%s' % (f['addr'], f['func'], f['file'], f['line']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
