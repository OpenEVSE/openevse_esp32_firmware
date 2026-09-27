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
    assert req['version'] == ''
