from symbolize_crash import build_request

SUMMARY = {'present': True, 'reason': 'StoreProhibited', 'task': 'loopTask',
           'elf_sha256': '0' * 64, 'bt': ['0x400d1234', '0x400d5678']}


def test_takes_the_hash_from_the_summary_not_the_config():
    # elf_sha256 identifies the build that CRASHED, which may not be the build
    # now running -- the device could have been updated since.
    req = build_request(SUMMARY, {'version': 'v9', 'buildenv': 'x'})
    assert req['elf_sha256'] == '0' * 64
    assert req['bt'] == ['0x400d1234', '0x400d5678']


def test_riscv_summary_passes_the_string_through():
    req = build_request({'bt': 'riscv-no-unwind'}, {})
    assert req['bt'] == 'riscv-no-unwind'


def test_missing_fields_do_not_crash():
    req = build_request({}, {})
    assert req['elf_sha256'] == ''
    assert req['version'] == 'unknown'


def test_the_running_version_is_not_reported_as_the_crashed_one():
    # Neither the summary nor /config says which version crashed, and the
    # device may have been updated since. The broker keeps the two apart.
    req = build_request(SUMMARY, {'version': 'v9', 'buildenv': 'x'})
    assert req['version'] == 'unknown'
    assert req['running_version'] == 'v9'


def test_every_network_read_has_a_timeout(monkeypatch, tmp_path):
    import io, json, urllib.request
    import symbolize_crash
    timeouts = []

    def fake_urlopen(url, *a, timeout=None, **kw):
        timeouts.append(timeout)
        body = {'status': 'symbolized', 'frames': []} if timeouts[1:] else {}
        return io.BytesIO(json.dumps(body).encode())

    monkeypatch.setattr(urllib.request, 'urlopen', fake_urlopen)
    summary = tmp_path / 's.json'
    summary.write_text(json.dumps(SUMMARY))
    assert symbolize_crash.main([str(summary), '--endpoint', 'https://x/v1/reports',
                                 '--host', '10.0.0.1']) == 0
    assert len(timeouts) == 2 and all(timeouts)


def test_no_hardware_identifier_is_sent():
    # The chip id is MAC-derived: personal data, and a hash of it brute-forces
    # back. The broker drops it anyway; it should not leave this machine.
    req = build_request(SUMMARY, {'chip_id': 'DEADBEEF01', 'wifi_serial': '3076F5EC2760'})
    assert 'chip_id' not in req
    assert '3076F5EC2760' not in str(req) and 'DEADBEEF01' not in str(req)
