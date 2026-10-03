# ELF Archive and Backtrace Symbolization — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** archive every CI-built `firmware.elf` under its own SHA-256, and turn a
crash backtrace of raw addresses into named frames — so a maintainer reads a
stack trace instead of chasing a blob and a matching ELF.

**Architecture:** a CDK stack (private S3 bucket + DynamoDB index + HTTP API + a
Python Lambda that shells `addr2line` against the archived ELF), a CI step that
uploads each build's ELF via GitHub OIDC, and a host script so a maintainer can
symbolize a hand-downloaded `/debug/crash` summary on day one.

**Tech Stack:** AWS CDK (TypeScript), Python 3.12 Lambda, DynamoDB, S3, GitHub
Actions OIDC, `xtensa-esp-elf-addr2line` (1.79 MB) vendored into the Lambda
package — no container image.

**Spec:** `docs/superpowers/specs/2026-09-27-crash-report-upload-design.md`
(implements §10 and the bucket/index half of §9; the device uploader §3/§6 and
the GUI button are Plans B and C.)

## Global Constraints

- **The ELF is keyed by its own SHA-256, not by version.** The firmware already
  reports `elf_sha256` in `/debug/crash` (`src/diagnostics.cpp:363`, from
  `esp_app_desc`), and it is byte-identical to `sha256sum firmware.elf`
  (verified). Version and buildenv are stored as metadata for humans, never as
  the lookup key: two `_modified` builds of one commit share a version but not a
  hash.
- **`elf_sha256` arrives in a request body, so it is validated before it reaches
  an S3 key** — exactly 64 lowercase hex characters or the request is rejected.
- **Bucket is private.** Block-public-access on, encrypted, no versioning (see
  Task 2 — versioning turns an expiry into a delete marker and the bytes stay).
- **Reports retain 90 days; ELFs 400 days** — the ELF must outlive any report
  that needs it (spec §10).
- **Xtensa only.** The firmware sends the string `riscv-no-unwind` instead of a
  backtrace on RISC-V parts, so there is nothing to symbolize there. Do not add
  a RISC-V toolchain.
- **No AWS credential reaches a charger.** CI uses OIDC; devices (Plan B) get
  presigned URLs.
- **Account-portable** (spec §4): no hardcoded account ids, region and domain
  from CDK context.

## Review Focus

Input classes the spec implies that no happy-path test would exercise. Each has
a test pinned to the task that owns the code.

1. **A hostile or malformed `elf_sha256`** — it comes from a request body and
   becomes an S3 key. `../../` or a 2 MB string must be rejected, not fetched.
   (Task 1)
2. **A report whose ELF was never uploaded** — every local build. Must be stored
   and flagged `unsymbolized`; never dropped, never a 500. (Task 4)
3. **An address inside an inlined function** — `addr2line -i` emits *more than
   two lines* for one address, which silently misattributes every later frame.
   (Task 4)
4. **`riscv-no-unwind` as the backtrace field** — a string where a list is
   expected. (Task 4)
5. **A missing or empty `elf_sha256` with a non-empty backtrace** — must degrade
   to `unsymbolized`, not raise. (Task 4)

---

## File Structure

New repo `openevse-crash-service`:

| File | Responsibility |
|---|---|
| `bin/crash-service.ts` | CDK app entry; reads region/domain/github org from context |
| `lib/storage-stack.ts` | S3 bucket, lifecycle rules, DynamoDB index |
| `lib/symbolize-stack.ts` | Lambda, HTTP API, CI OIDC role |
| `lib/keys.ts` | `elf_sha256` validation → S3 key |
| `lambda/symbolize/handler.py` | request handling, index write, response shape |
| `lambda/symbolize/addr2line.py` | ELF fetch + `addr2line` invocation + output parsing |
| `lambda/symbolize/bin/xtensa-esp-elf-addr2line` | vendored, 1.79 MB |
| `test/*.test.ts`, `lambda/symbolize/test_*.py` | tests |

Firmware repo:

| File | Responsibility |
|---|---|
| `.github/workflows/build.yaml` | `id-token: write` on the build job; ELF upload step |
| `scripts/symbolize_crash.py` | host script for today's manual flow |

---

## Task 1: ELF key from a validated hash

**Files:**
- Create: `lib/keys.ts`
- Test: `test/keys.test.ts`

**Interfaces:**
- Produces: `elfKey(sha256: string): string` → `elf/<sha>.elf`;
  `isValidSha256(s: string): boolean`.

- [ ] **Step 1: Scaffold**

```bash
mkdir -p openevse-crash-service && cd openevse-crash-service
npx cdk init app --language typescript
npm install --save-dev @types/jest jest ts-jest
```

- [ ] **Step 2: Write the failing test**

`test/keys.test.ts`:

```typescript
import { isValidSha256, elfKey } from '../lib/keys';

const GOOD = '0652fd8da081daefaf79e5efed961dd5da7af8e4dcf05ce0bdaba0066190ceb0';

test('accepts a real 64-char lowercase hash', () => {
  expect(isValidSha256(GOOD)).toBe(true);
  expect(elfKey(GOOD)).toBe(`elf/${GOOD}.elf`);
});

test('rejects traversal, which would otherwise become an S3 key', () => {
  // Review Focus 1: this value arrives in a request body.
  expect(isValidSha256('../../etc/passwd')).toBe(false);
  expect(() => elfKey('../../etc/passwd')).toThrow();
});

test('rejects wrong length, uppercase and non-hex', () => {
  expect(isValidSha256('abc')).toBe(false);
  expect(isValidSha256(GOOD.toUpperCase())).toBe(false);
  expect(isValidSha256(GOOD.slice(0, 63) + 'z')).toBe(false);
  expect(isValidSha256(GOOD + '0')).toBe(false);
});

test('rejects empty and absurd input', () => {
  expect(isValidSha256('')).toBe(false);
  expect(isValidSha256('a'.repeat(2_000_000))).toBe(false);
});
```

- [ ] **Step 3: Run it and watch it fail**

Run: `npx jest test/keys.test.ts`
Expected: FAIL — `Cannot find module '../lib/keys'`.

- [ ] **Step 4: Implement**

`lib/keys.ts`:

```typescript
// The ELF is keyed by its own SHA-256, which the firmware already reports as
// `elf_sha256` (src/diagnostics.cpp) and which equals `sha256sum firmware.elf`.
// Keying on (version, buildenv) instead would let two '_modified' builds of one
// commit share a key and symbolize against the wrong ELF.
//
// The value arrives in a request body, so it is validated before it is ever
// concatenated into a key. Anchored, fixed-length, lowercase hex only: there is
// no escaping to get wrong because nothing outside [0-9a-f] is accepted.
const SHA256 = /^[0-9a-f]{64}$/;

export function isValidSha256(s: string): boolean {
  return typeof s === 'string' && SHA256.test(s);
}

export function elfKey(sha256: string): string {
  if (!isValidSha256(sha256)) {
    throw new Error('elf_sha256 must be 64 lowercase hex characters');
  }
  return `elf/${sha256}.elf`;
}
```

- [ ] **Step 5: Run the tests**

Run: `npx jest test/keys.test.ts`
Expected: PASS, 4 tests.

- [ ] **Step 6: Commit**

```bash
git add lib/keys.ts test/keys.test.ts package.json
git commit -m "feat: validated SHA-256 ELF key"
```

---

## Task 2: Storage stack

**Files:**
- Create: `lib/storage-stack.ts`
- Test: `test/storage-stack.test.ts`

**Interfaces:**
- Produces: `StorageStack` with `readonly bucket: s3.Bucket` and
  `readonly table: dynamodb.Table`.

- [ ] **Step 1: Write the failing test**

`test/storage-stack.test.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import { Template, Match } from 'aws-cdk-lib/assertions';
import { StorageStack } from '../lib/storage-stack';

const template = () => Template.fromStack(new StorageStack(new cdk.App(), 'T'));

test('bucket blocks all public access and forces TLS', () => {
  const t = template();
  t.hasResourceProperties('AWS::S3::Bucket', {
    PublicAccessBlockConfiguration: {
      BlockPublicAcls: true, BlockPublicPolicy: true,
      IgnorePublicAcls: true, RestrictPublicBuckets: true,
    },
  });
  expect(JSON.stringify(t.findResources('AWS::S3::BucketPolicy')))
    .toContain('aws:SecureTransport');
});

test('versioning is OFF so an expiry actually deletes the bytes', () => {
  // On a versioned bucket, `expiration` only writes a delete marker and the
  // object persists as a noncurrent version. The spec's 90-day limit on
  // credential-bearing memory images has to be real, so no versioning.
  const t = template();
  const buckets = t.findResources('AWS::S3::Bucket');
  const props = Object.values(buckets)[0] as any;
  expect(props.Properties.VersioningConfiguration).toBeUndefined();
});

test('reports expire at 90 days and ELFs outlive them', () => {
  template().hasResourceProperties('AWS::S3::Bucket', {
    LifecycleConfiguration: { Rules: Match.arrayWith([
      Match.objectLike({ Prefix: 'reports/', ExpirationInDays: 90, Status: 'Enabled' }),
      Match.objectLike({ Prefix: 'elf/', ExpirationInDays: 400, Status: 'Enabled' }),
    ])},
  });
});

test('index is keyed by report_id and queryable by elf hash', () => {
  template().hasResourceProperties('AWS::DynamoDB::Table', {
    KeySchema: [{ AttributeName: 'report_id', KeyType: 'HASH' }],
    GlobalSecondaryIndexes: Match.arrayWith([
      Match.objectLike({ IndexName: 'by-elf' }),
    ]),
  });
});
```

- [ ] **Step 2: Run it and watch it fail**

Run: `npx jest test/storage-stack.test.ts`
Expected: FAIL — `Cannot find module '../lib/storage-stack'`.

- [ ] **Step 3: Implement**

`lib/storage-stack.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as dynamodb from 'aws-cdk-lib/aws-dynamodb';
import { Construct } from 'constructs';

export const REPORT_RETENTION_DAYS = 90;
// Longer than the report retention, never equal: a report arriving late in a
// build's life still needs its ELF to be readable (spec §10).
export const ELF_RETENTION_DAYS = 400;

export class StorageStack extends cdk.Stack {
  public readonly bucket: s3.Bucket;
  public readonly table: dynamodb.Table;

  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    this.bucket = new s3.Bucket(this, 'CrashBucket', {
      blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
      encryption: s3.BucketEncryption.S3_MANAGED,
      enforceSSL: true,
      // Deliberately NOT versioned. See the test: on a versioned bucket an
      // expiration rule only adds a delete marker and the object bytes live on
      // as a noncurrent version, so the retention promise would be false.
      lifecycleRules: [
        { id: 'reports', prefix: 'reports/', enabled: true,
          expiration: cdk.Duration.days(REPORT_RETENTION_DAYS) },
        { id: 'elf', prefix: 'elf/', enabled: true,
          expiration: cdk.Duration.days(ELF_RETENTION_DAYS) },
      ],
    });

    this.table = new dynamodb.Table(this, 'Reports', {
      partitionKey: { name: 'report_id', type: dynamodb.AttributeType.STRING },
      billingMode: dynamodb.BillingMode.PAY_PER_REQUEST,
      timeToLiveAttribute: 'expires_at',
    });
    this.table.addGlobalSecondaryIndex({
      indexName: 'by-elf',
      partitionKey: { name: 'elf_sha256', type: dynamodb.AttributeType.STRING },
      sortKey: { name: 'created_at', type: dynamodb.AttributeType.STRING },
    });
    this.table.addGlobalSecondaryIndex({
      indexName: 'by-chip',
      partitionKey: { name: 'chip_id', type: dynamodb.AttributeType.STRING },
      sortKey: { name: 'created_at', type: dynamodb.AttributeType.STRING },
    });
  }
}
```

- [ ] **Step 4: Run the tests**

Run: `npx jest`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add lib/storage-stack.ts test/storage-stack.test.ts
git commit -m "feat: private unversioned bucket with split retention, plus index"
```

---

## Task 3: CI uploads the ELF under its hash

**Files:**
- Create: `lib/symbolize-stack.ts`, `bin/crash-service.ts`
- Modify (firmware repo): `.github/workflows/build.yaml`
- Test: `test/symbolize-stack.test.ts`

**Interfaces:**
- Consumes: `StorageStack.bucket`.
- Produces: an OIDC role writable only to `elf/*`; `CfnOutput` `CiRoleArn`.

- [ ] **Step 1: Write the failing test**

`test/symbolize-stack.test.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import { Template } from 'aws-cdk-lib/assertions';
import { StorageStack } from '../lib/storage-stack';
import { SymbolizeStack } from '../lib/symbolize-stack';

const template = () => {
  const app = new cdk.App();
  const storage = new StorageStack(app, 'S');
  return Template.fromStack(new SymbolizeStack(app, 'T', {
    bucket: storage.bucket, table: storage.table,
    githubOrg: 'OpenEVSE', githubRepo: 'openevse_esp32_firmware',
    oidcProviderArn: 'arn:aws:iam::123456789012:oidc-provider/token.actions.githubusercontent.com',
  }));
};

test('CI role can write ELFs and cannot touch reports', () => {
  // A leaked CI credential must not read users' memory images. Asserted by
  // scanning the rendered policy rather than by shape-matching: CDK renders a
  // single action as a string, not a one-element array.
  const policies = JSON.stringify(template().findResources('AWS::IAM::Policy'));
  expect(policies).toContain('s3:PutObject');
  expect(policies).toContain('elf/*');
  expect(policies).not.toContain('reports/');
  expect(policies).not.toContain('s3:GetObject');
});

test('CI role is restricted to this repo', () => {
  expect(JSON.stringify(template().findResources('AWS::IAM::Role')))
    .toContain('repo:OpenEVSE/openevse_esp32_firmware:');
});

test('the OIDC provider is referenced, not created', () => {
  // One GitHub provider exists per account; creating a second fails deploy with
  // EntityAlreadyExists in any account that already uses GitHub Actions.
  expect(template().findResources('Custom::AWSCDKOpenIdConnectProvider'))
    .toEqual({});
});
```

- [ ] **Step 2: Run it and watch it fail**

Run: `npx jest test/symbolize-stack.test.ts`
Expected: FAIL — `Cannot find module '../lib/symbolize-stack'`.

- [ ] **Step 3: Implement**

`lib/symbolize-stack.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as dynamodb from 'aws-cdk-lib/aws-dynamodb';
import { Construct } from 'constructs';

export interface SymbolizeStackProps extends cdk.StackProps {
  bucket: s3.Bucket;
  table: dynamodb.Table;
  githubOrg: string;
  githubRepo: string;
  /** Existing provider ARN. Accounts already using GitHub Actions have one,
   *  and a second cannot be created for the same URL. */
  oidcProviderArn: string;
}

export class SymbolizeStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: SymbolizeStackProps) {
    super(scope, id, props);

    const provider = iam.OpenIdConnectProvider.fromOpenIdConnectProviderArn(
      this, 'GitHubOidc', props.oidcProviderArn);

    const ciRole = new iam.Role(this, 'CiElfUploadRole', {
      assumedBy: new iam.WebIdentityPrincipal(provider.openIdConnectProviderArn, {
        StringEquals: { 'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com' },
        StringLike: {
          'token.actions.githubusercontent.com:sub':
            `repo:${props.githubOrg}/${props.githubRepo}:*`,
        },
      }),
    });
    // PutObject only, elf/ only. No GetObject: CI never reads anything back.
    ciRole.addToPolicy(new iam.PolicyStatement({
      actions: ['s3:PutObject'],
      resources: [props.bucket.arnForObjects('elf/*')],
    }));

    new cdk.CfnOutput(this, 'CiRoleArn', { value: ciRole.roleArn });
  }
}
```

`bin/crash-service.ts`:

```typescript
#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { StorageStack } from '../lib/storage-stack';
import { SymbolizeStack } from '../lib/symbolize-stack';

const app = new cdk.App();
// Everything environment-specific comes from context, so `cdk deploy` into
// OpenEVSE's account is the whole handover (spec §4).
const env = {
  account: process.env.CDK_DEFAULT_ACCOUNT,
  region: app.node.tryGetContext('region') ?? process.env.CDK_DEFAULT_REGION,
};

const storage = new StorageStack(app, 'CrashStorage', { env });
new SymbolizeStack(app, 'CrashSymbolize', {
  env,
  bucket: storage.bucket,
  table: storage.table,
  githubOrg: app.node.tryGetContext('githubOrg') ?? 'OpenEVSE',
  githubRepo: app.node.tryGetContext('githubRepo') ?? 'openevse_esp32_firmware',
  oidcProviderArn: app.node.tryGetContext('oidcProviderArn'),
});
```

- [ ] **Step 4: Run the tests**

Run: `npx jest`
Expected: PASS, 11 tests.

- [ ] **Step 5: Add the CI permission and upload step (firmware repo)**

In `.github/workflows/build.yaml`, add to the `build` job, beside `runs-on`:

```yaml
    permissions:
      contents: read
      id-token: write   # required for the OIDC exchange below
```

Then, after the `Upload output to GitHub` step:

```yaml
    - name: Configure AWS credentials for ELF archive
      # Only on pushes to this repo. A fork PR runs in the base repo, so
      # `github.repository` is TRUE there while secrets are empty -- gating on
      # the event keeps a fork's build from failing on a missing secret.
      if: ${{ github.event_name == 'push' && github.repository == 'OpenEVSE/openevse_esp32_firmware' && matrix.env != 'native_openevse' }}
      uses: aws-actions/configure-aws-credentials@v4
      with:
        role-to-assume: ${{ secrets.CRASH_ELF_ROLE_ARN }}
        aws-region: ${{ vars.CRASH_AWS_REGION }}

    - name: Upload firmware.elf under its own SHA-256
      if: ${{ github.event_name == 'push' && github.repository == 'OpenEVSE/openevse_esp32_firmware' && matrix.env != 'native_openevse' }}
      run: |
        set -euo pipefail
        ELF=".pio/build/${{ matrix.env }}/firmware.elf"
        # The device reports this exact value as elf_sha256 in /debug/crash --
        # esptool's "ELF file SHA256" is byte-identical to sha256sum of the ELF
        # (verified). No version string, no escaping, no ambiguity between two
        # builds that happen to share a branch and commit.
        SHA=$(sha256sum "$ELF" | cut -d' ' -f1)
        echo "elf/$SHA.elf  <- ${{ matrix.env }}"
        aws s3 cp "$ELF" "s3://${{ secrets.CRASH_BUCKET }}/elf/$SHA.elf" \
          --metadata "buildenv=${{ matrix.env }},ref=${{ github.ref_name }},sha=${{ github.sha }}"
```

- [ ] **Step 6: Verify the key matches what a device reports**

Against a flashed device, on the commit it was built from:

```bash
sha256sum .pio/build/openevse_wifi_v1/firmware.elf | cut -d' ' -f1
curl -s http://10.75.1.6/debug/crash | python3 -c "import sys,json;print(json.load(sys.stdin).get('elf_sha256'))"
```
Expected: identical strings when the device is running that build. If the
device has no stored dump, `/debug/crash` returns `{"present":false}` — flash a
build and trigger a panic, or compare against a device you know the build of.

- [ ] **Step 7: Commit (both repos)**

```bash
# service repo
git add lib/symbolize-stack.ts bin/crash-service.ts test/symbolize-stack.test.ts
git commit -m "feat: OIDC role for CI ELF upload, scoped to elf/ only"
# firmware repo
git add .github/workflows/build.yaml
git commit -m "ci: archive firmware.elf under its own SHA-256"
```

---

## Task 4: The symbolizer

**Files:**
- Create: `lambda/symbolize/addr2line.py`, `lambda/symbolize/handler.py`
- Create: `lambda/symbolize/bin/xtensa-esp-elf-addr2line`
- Test: `lambda/symbolize/test_addr2line.py`, `lambda/symbolize/test_handler.py`

**Interfaces:**
- Produces: `parse_addr2line(addrs, stdout) -> list[dict]` with keys `addr`,
  `func`, `file`, `line`; `symbolize(elf_path, addrs) -> list[dict]`;
  `classify_backtrace(bt) -> tuple[str, list]`; `handler(event, context)`
  returning `{report_id, status, frames}` where `status` is `symbolized`,
  `unsymbolized` or `no-backtrace`.

- [ ] **Step 1: Vendor the real binary**

```bash
mkdir -p lambda/symbolize/bin
# NOTE: xtensa-esp32-elf-addr2line (422 KB) is a Rust launcher that picks a
# chip from its own filename and execs the real tool. Renamed, it panics with
# 'Target chip can not be "esp"'. Vendor the real 1.79 MB binary, which runs
# standalone.
cp ~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-addr2line \
   lambda/symbolize/bin/
chmod +x lambda/symbolize/bin/xtensa-esp-elf-addr2line
./lambda/symbolize/bin/xtensa-esp-elf-addr2line --version | head -1
```
Expected: a GNU addr2line version banner, **not** a Rust panic.

- [ ] **Step 2: Write the failing parser test**

These are pure parser tests over recorded `addr2line` output — no ELF, no
toolchain, no 46 MB fixture in git.

`lambda/symbolize/test_addr2line.py`:

```python
from addr2line import parse_addr2line

def test_pairs_two_lines_per_address():
    out = "loop\n/src/main.cpp:100\nsetup\n/src/main.cpp:50\n"
    frames = parse_addr2line(['0x1', '0x2'], out)
    assert [f['func'] for f in frames] == ['loop', 'setup']
    assert frames[0]['file'] == '/src/main.cpp'
    assert frames[0]['line'] == '100'

def test_unknown_address_degrades_to_the_raw_value():
    # Review Focus 2/3: a corrupt stack is the normal case in a crash worth
    # investigating; one bad frame must not fail the request.
    frames = parse_addr2line(['0xdeadbeef'], "??\n??:0\n")
    assert frames[0]['addr'] == '0xdeadbeef'
    assert frames[0]['func'] == '??'

def test_strips_the_discriminator_suffix():
    # addr2line writes 'file.cpp:553 (discriminator 1)' for ~5% of lines.
    frames = parse_addr2line(['0x1'], "loop\n/src/main.cpp:553 (discriminator 1)\n")
    assert frames[0]['line'] == '553'
    assert frames[0]['file'] == '/src/main.cpp'

def test_truncated_output_does_not_index_past_the_end():
    frames = parse_addr2line(['0x1', '0x2'], "loop\n")
    assert len(frames) == 2
    assert frames[1]['func'] == '??'
```

- [ ] **Step 3: Run it and watch it fail**

Run: `cd lambda/symbolize && python -m pytest test_addr2line.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'addr2line'`.

- [ ] **Step 4: Implement**

`lambda/symbolize/addr2line.py`:

```python
import os
import re
import subprocess

BIN = os.path.join(os.path.dirname(__file__), 'bin', 'xtensa-esp-elf-addr2line')

# 'main.cpp:553 (discriminator 1)' -> line 553.
_DISCRIMINATOR = re.compile(r'\s*\(discriminator\s+\d+\)\s*$')


class Addr2LineMissing(RuntimeError):
    pass


def parse_addr2line(addrs, stdout):
    """Pair addr2line's two-lines-per-address output back onto its inputs.

    Strictly two lines per address, which is why the caller must NOT pass -i:
    with inlining enabled addr2line emits an extra func/file:line pair per
    inlined frame, with no marker to detect it in non-pretty mode, and every
    frame after the first inlined one would be attributed to the wrong address.
    """
    lines = stdout.splitlines()
    frames = []
    for i, addr in enumerate(addrs):
        func = lines[2 * i] if 2 * i < len(lines) else '??'
        loc = lines[2 * i + 1] if 2 * i + 1 < len(lines) else '??:0'
        loc = _DISCRIMINATOR.sub('', loc)
        file, _, line = loc.rpartition(':')
        frames.append({
            'addr': addr,
            'func': func or '??',
            'file': file or '??',
            'line': line or '0',
        })
    return frames


def symbolize(elf_path, addrs):
    """Xtensa only, deliberately: the firmware sends 'riscv-no-unwind' instead
    of a backtrace on RISC-V parts, so there is nothing to resolve there."""
    if not os.path.exists(BIN):
        raise Addr2LineMissing(BIN)
    if not addrs:
        return []
    proc = subprocess.run(
        [BIN, '-e', elf_path, '-f', '-C'] + list(addrs),
        capture_output=True, text=True, timeout=30)
    return parse_addr2line(addrs, proc.stdout)
```

- [ ] **Step 5: Run the parser tests**

Run: `cd lambda/symbolize && python -m pytest test_addr2line.py -v`
Expected: PASS, 4 tests.

- [ ] **Step 6: Write the failing handler test**

`lambda/symbolize/test_handler.py`:

```python
import os
import pytest

# Import must not require AWS. handler.py reads env and creates clients lazily
# precisely so these run on a developer machine with no credentials.
from handler import classify_backtrace, elf_key_or_none

def test_list_of_addresses_is_symbolizable():
    assert classify_backtrace(['0x400d0000']) == ('symbolized', ['0x400d0000'])

def test_riscv_no_unwind_is_not_an_error():
    # Review Focus 4.
    assert classify_backtrace('riscv-no-unwind') == ('no-backtrace', [])

def test_missing_and_empty_backtraces_are_not_errors():
    assert classify_backtrace(None) == ('no-backtrace', [])
    assert classify_backtrace([]) == ('no-backtrace', [])

def test_non_string_frames_are_rejected_not_passed_to_a_shell():
    assert classify_backtrace([{'evil': 1}]) == ('no-backtrace', [])

def test_missing_elf_sha_returns_none_rather_than_raising():
    # Review Focus 5: must degrade to 'unsymbolized', never a 500.
    assert elf_key_or_none('') is None
    assert elf_key_or_none(None) is None
    assert elf_key_or_none('../../etc/passwd') is None
    good = '0' * 64
    assert elf_key_or_none(good) == 'elf/%s.elf' % good
```

- [ ] **Step 7: Run it and watch it fail**

Run: `python -m pytest test_handler.py -v`
Expected: FAIL — `ImportError: cannot import name 'classify_backtrace'`.

- [ ] **Step 8: Implement**

`lambda/symbolize/handler.py`:

```python
import json
import os
import re
import tempfile
import uuid
from datetime import datetime, timedelta, timezone

from addr2line import symbolize, Addr2LineMissing

RETENTION_DAYS = 90
SHA256 = re.compile(r'^[0-9a-f]{64}$')

_s3 = None
_table = None


def _clients():
    """Created on first use, not at import. Keeps the pure-logic tests above
    runnable on a machine with no AWS credentials or region."""
    global _s3, _table
    if _s3 is None:
        import boto3
        _s3 = boto3.client('s3')
        _table = boto3.resource('dynamodb').Table(os.environ['TABLE'])
    return _s3, _table


def elf_key_or_none(sha256):
    """None rather than an exception: an absent or malformed hash is a report
    we still want to keep, flagged unsymbolized."""
    if not isinstance(sha256, str) or not SHA256.match(sha256):
        return None
    return 'elf/%s.elf' % sha256


def classify_backtrace(bt):
    """A list on Xtensa, the string 'riscv-no-unwind' on RISC-V. Anything that
    is not a list of strings is treated as no backtrace -- these values reach a
    subprocess argv."""
    if isinstance(bt, list) and bt and all(isinstance(a, str) for a in bt):
        return 'symbolized', bt
    return 'no-backtrace', []


def handler(event, context):
    body = json.loads(event.get('body') or '{}')
    report_id = str(uuid.uuid4())
    sha = body.get('elf_sha256')

    status, addrs = classify_backtrace(body.get('bt'))
    frames = []

    if status == 'symbolized':
        key = elf_key_or_none(sha)
        if key is None:
            status = 'unsymbolized'
        else:
            s3, _ = _clients()
            try:
                with tempfile.NamedTemporaryFile(suffix='.elf') as fh:
                    s3.download_fileobj(os.environ['BUCKET'], key, fh)
                    fh.flush()
                    frames = symbolize(fh.name, addrs)
            except Exception:
                # A local build has no archived ELF; a slow or missing toolchain
                # is not the report's fault. Keep it, say so, never 500.
                status = 'unsymbolized'
                frames = []

    now = datetime.now(timezone.utc)
    _, table = _clients()
    table.put_item(Item={
        'report_id': report_id,
        'elf_sha256': sha if isinstance(sha, str) else 'unknown',
        'version': body.get('version', 'unknown'),
        'buildenv': body.get('buildenv', 'unknown'),
        'chip_id': body.get('chip_id', 'unknown'),
        'created_at': now.isoformat(),
        'status': status,
        'frames': frames,
        'summary': body.get('summary', {}),
        'diagnostics': body.get('diagnostics', {}),
        'expires_at': int((now + timedelta(days=RETENTION_DAYS)).timestamp()),
    })

    return {
        'statusCode': 200,
        'headers': {'content-type': 'application/json'},
        'body': json.dumps({'report_id': report_id, 'status': status,
                            'frames': frames}),
    }
```

- [ ] **Step 9: Run all the Lambda tests**

Run: `cd lambda/symbolize && python -m pytest -v`
Expected: PASS, 9 tests, with no AWS credentials configured.

- [ ] **Step 10: Optional live check against a real ELF**

```bash
cd lambda/symbolize
python -c "
from addr2line import symbolize
import subprocess
elf='../../../openevse_esp32_firmware/.pio/build/openevse_wifi_v1/firmware.elf'
nm=subprocess.run(['./bin/xtensa-esp-elf-nm','-C',elf],capture_output=True,text=True).stdout
addr=next(l.split()[0] for l in nm.splitlines() if ' T ' in l)
print(symbolize(elf, ['0x'+addr, '0xdeadbeef']))
"
```
Expected: first frame named, second `??`. Skip if no ELF is to hand — the
parser tests above are the gate.

- [ ] **Step 11: Commit**

```bash
echo 'fixtures/' >> .gitignore
git add lambda/symbolize .gitignore
git commit -m "feat: symbolize backtraces against the archived ELF"
```

---

## Task 5: HTTP API

**Files:**
- Modify: `lib/symbolize-stack.ts`
- Test: `test/symbolize-stack.test.ts`

**Interfaces:**
- Produces: `POST <apiEndpoint>/v1/reports` accepting the Task 4 body.

- [ ] **Step 1: Add the failing test**

Append to `test/symbolize-stack.test.ts`:

```typescript
test('lambda gets bucket and table by environment', () => {
  template().hasResourceProperties('AWS::Lambda::Function', {
    Environment: { Variables: { BUCKET: {}, TABLE: {} } },
    Runtime: 'python3.12',
  });
});

test('lambda timeout fits inside the API Gateway 30s cap', () => {
  // HTTP API integrations hard-cap at 30s. A longer function timeout means the
  // caller gets a 504 while the Lambda runs on and still writes an index row.
  const fns = template().findResources('AWS::Lambda::Function');
  const timeout = (Object.values(fns)[0] as any).Properties.Timeout;
  expect(timeout).toBeLessThanOrEqual(29);
});

test('the ELF is not bundled into the lambda asset', () => {
  // A 46 MB firmware.elf swept into fromAsset() would be hashed on every synth
  // and shipped on every deploy.
  const fs = require('fs');
  expect(fs.existsSync('lambda/symbolize/fixtures')).toBe(false);
});
```

- [ ] **Step 2: Run and watch it fail**

Run: `npx jest test/symbolize-stack.test.ts`
Expected: FAIL — no `AWS::Lambda::Function` matching those properties.

- [ ] **Step 3: Add the function and API**

Imports in `lib/symbolize-stack.ts`:

```typescript
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as apigwv2 from 'aws-cdk-lib/aws-apigatewayv2';
import * as integrations from 'aws-cdk-lib/aws-apigatewayv2-integrations';
```

In the constructor, after `ciRole`:

```typescript
    const fn = new lambda.Function(this, 'Symbolize', {
      runtime: lambda.Runtime.PYTHON_3_12,
      handler: 'handler.handler',
      code: lambda.Code.fromAsset('lambda/symbolize', {
        exclude: ['fixtures', 'test_*.py', '__pycache__', '.pytest_cache'],
      }),
      // Under the API Gateway HTTP API 30s integration cap, so a caller never
      // sees a 504 for work that then completes and writes an index row.
      timeout: cdk.Duration.seconds(29),
      memorySize: 1536,   // ELF download dominates; CPU scales with memory
      environment: {
        BUCKET: props.bucket.bucketName,
        TABLE: props.table.tableName,
      },
    });
    props.bucket.grantRead(fn, 'elf/*');
    props.table.grantWriteData(fn);

    const api = new apigwv2.HttpApi(this, 'Api');
    api.addRoutes({
      path: '/v1/reports',
      methods: [apigwv2.HttpMethod.POST],
      integration: new integrations.HttpLambdaIntegration('Symbolize', fn),
    });

    new cdk.CfnOutput(this, 'ApiUrl', { value: api.apiEndpoint });
```

- [ ] **Step 4: Run the tests**

Run: `npx jest`
Expected: PASS, 14 tests.

- [ ] **Step 5: Deploy and smoke-test**

```bash
npx cdk deploy --all \
  -c region=eu-west-1 \
  -c oidcProviderArn=arn:aws:iam::<account>:oidc-provider/token.actions.githubusercontent.com
API=$(aws cloudformation describe-stacks --stack-name CrashSymbolize \
  --query 'Stacks[0].Outputs[?OutputKey==`ApiUrl`].OutputValue' --output text)
curl -s -X POST "$API/v1/reports" -H 'content-type: application/json' \
  -d '{"elf_sha256":"'"$(printf '0%.0s' {1..64})"'","bt":["0x400d0000"]}'
```
Expected: `{"report_id":"...","status":"unsymbolized","frames":[]}` — Review
Focus 2 proven on the real deployment.

- [ ] **Step 6: Commit**

```bash
git add lib/symbolize-stack.ts test/symbolize-stack.test.ts
git commit -m "feat: HTTP API in front of the symbolizer"
```

---

## Task 6: Host script

**Files:**
- Create (firmware repo): `scripts/symbolize_crash.py`, `scripts/test_symbolize_crash.py`

- [ ] **Step 1: Write the failing test**

`scripts/test_symbolize_crash.py`:

```python
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
```

- [ ] **Step 2: Run and watch it fail**

Run: `cd scripts && python -m pytest test_symbolize_crash.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'symbolize_crash'`.

- [ ] **Step 3: Implement**

`scripts/symbolize_crash.py`:

```python
#!/usr/bin/env python3
"""Symbolize a crash summary downloaded from a charger.

Takes the JSON that Settings -> Developer Tools -> "Download summary" writes and
prints named frames.

    python scripts/symbolize_crash.py coredump-summary.json \\
        --endpoint https://<api>/v1/reports

The device reports addresses plus elf_sha256; the ELF that names them lives in
the crash service under that hash.
"""
import argparse
import json
import sys
import urllib.request


def build_request(summary, config):
    return {
        # From the SUMMARY: it identifies the build that crashed, which is not
        # necessarily the build running now.
        'elf_sha256': summary.get('elf_sha256', ''),
        'bt': summary.get('bt'),
        'version': config.get('version', ''),
        'buildenv': config.get('buildenv', ''),
        'chip_id': config.get('chip_id', 'unknown'),
        'summary': summary,
    }


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('summary', help='coredump-summary.json from the web UI')
    ap.add_argument('--endpoint', required=True, help='crash service /v1/reports URL')
    ap.add_argument('--host', help='device address, to fetch /config for metadata')
    ap.add_argument('--config', help='a saved /config JSON instead of --host')
    args = ap.parse_args(argv)

    summary = json.load(open(args.summary))
    config = {}
    if args.config:
        config = json.load(open(args.config))
    elif args.host:
        with urllib.request.urlopen('http://%s/config' % args.host) as r:
            config = json.load(r)

    body = json.dumps(build_request(summary, config)).encode()
    rq = urllib.request.Request(args.endpoint, data=body,
                                headers={'content-type': 'application/json'})
    with urllib.request.urlopen(rq) as r:
        result = json.load(r)

    print('status: %s' % result['status'])
    if result['status'] == 'unsymbolized':
        print('  no archived ELF for %s' % (summary.get('elf_sha256') or '(none reported)'))
    for f in result.get('frames', []):
        print('  %s  %s  %s:%s' % (f['addr'], f['func'], f['file'], f['line']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 4: Run the tests**

Run: `cd scripts && python -m pytest test_symbolize_crash.py -v`
Expected: PASS, 3 tests.

- [ ] **Step 5: End-to-end against a real device**

```bash
curl -s http://10.75.1.6/debug/crash > /tmp/summary.json
python scripts/symbolize_crash.py /tmp/summary.json --endpoint "$API/v1/reports"
```
Expected: named frames, or `status: unsymbolized` naming the hash. Only a
traceback is a failure. (`{"present":false}` means no stored dump — pick a
device that has one.)

- [ ] **Step 6: Commit**

```bash
git add scripts/symbolize_crash.py scripts/test_symbolize_crash.py
git commit -m "tools: symbolize a downloaded crash summary against the archive"
```

---

## Deferred

- **Custom domain.** The spec pins `crash.openevse.com` so the firmware's
  allowlist survives a handover (§4). That matters for Plan B's device uploader,
  which ships a compiled-in hostname; nothing here has one, so the API endpoint
  is passed explicitly. Add ACM + the domain in Plan B, before any firmware
  pins it.
- Device uploader and `/debug/crash/upload` — Plan B.
- GUI button — Plan C.
- No presigned PUT for the raw 64 KB dump; this plan moves the summary only.
- No full `esp-coredump` register/stack decode — backtrace naming only.
