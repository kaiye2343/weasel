'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const {
  collectDiagnostics,
  evaluateDiagnostics,
} = require('../tools/check-native-diagnostics.cjs');

function makeBaseline(entries = []) {
  return { schemaVersion: 1, entries };
}

function makeEntry(record, overrides = {}) {
  return {
    code: record.code,
    message: record.message,
    source: record.source,
    line: record.line,
    column: record.column,
    project: record.project,
    architecture: record.architecture,
    origin: record.origin,
    control: record.control,
    reason: 'expected diagnostic',
    reference: 'native_diagnostics_test',
    ...overrides,
  };
}

test('CLI reads BOM logs, rejects diagnostics and requires both production architectures', () => {
  const project = path.resolve(__dirname, '..');
  fs.mkdirSync(path.join(project, '.tools'), { recursive: true });
  const root = fs.mkdtempSync(path.join(project, '.tools', 'native-diagnostics-test-'));
  const parser = path.join(project, 'tools', 'check-native-diagnostics.cjs');
  const line = `${root}\\sample.cpp(2,3): warning C4244: deliberate fixture [Sample.vcxproj]\r\n`;
  fs.writeFileSync(path.join(root, 'x64.log'), '\ufeff' + line, 'utf16le');
  fs.writeFileSync(path.join(root, 'Win32.log'), '\ufeff' + line, 'utf8');
  fs.writeFileSync(path.join(root, 'baseline.json'), JSON.stringify(makeBaseline()));
  const inputs = { schemaVersion: 1, logs: ['x64', 'Win32'].map(architecture => ({
    path: architecture + '.log', architecture, origin: 'production', control: '',
  })) };
  const saveInputs = () => fs.writeFileSync(path.join(root, 'inputs.json'), JSON.stringify(inputs));
  const run = () => {
    const result = spawnSync(process.execPath, [parser, '--root', root,
      '--inputs', 'inputs.json', '--baseline', 'baseline.json',
      '--output', 'report.json', '--summary', 'summary.md'], { encoding: 'utf8' });
    assert.ifError(result.error);
    return result;
  };
  saveInputs();
  assert.equal(run().status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(root, 'report.json'), 'utf8'));
  assert.equal(report.sourceLogCount, 2);
  assert.equal(report.unapprovedWarnings.length, 2);
  assert.equal(report.records[0].source, 'sample.cpp');
  for (const architecture of ['x64', 'Win32']) fs.writeFileSync(path.join(root, architecture + '.log'), 'Build succeeded.\n');
  assert.equal(run().status, 0);
  inputs.logs.pop(); saveInputs();
  assert.equal(run().status, 2);
  inputs.logs.push({ path: '../outside.log', architecture: 'Win32', origin: 'production', control: '' }); saveInputs();
  assert.equal(run().status, 2);
  inputs.logs[1].path = 'absent.log'; saveInputs();
  assert.equal(run().status, 2);
  inputs.logs[1].path = 'Win32.log'; saveInputs();
  fs.writeFileSync(path.join(root, 'Win32.log'), '');
  assert.equal(run().status, 2);
  fs.writeFileSync(path.join(root, 'Win32.log'), line, 'utf16le');
  assert.equal(run().status, 2, 'UTF16 without a BOM must not become a zero-warning report');
  fs.writeFileSync(path.join(root, 'Win32.log'), Buffer.from([0xff, 0x80, 0x81]));
  assert.equal(run().status, 2, 'invalid UTF8 must not be silently replaced');
  fs.writeFileSync(path.join(root, 'Win32.log'), 'sample.cpp(3): 警告 C4244: deliberate fixture [Sample.vcxproj]');
  assert.equal(run().status, 2, 'unknown diagnostic locale must fail closed');
});

test('unknown external paths retain distinct identities and cannot be baselined', () => {
  const meta = { architecture: 'x64', origin: 'production', control: '' };
  const a = collectDiagnostics('C:/unknown-a/same.h(2): warning C4244: fixture [Sample.vcxproj]', meta, 'D:/repo')[0];
  const b = collectDiagnostics('C:/unknown-b/same.h(2): warning C4244: fixture [Sample.vcxproj]', meta, 'D:/repo')[0];
  assert.notEqual(a.source, b.source);
  assert.ok(!a.source.includes('unknown-a'));
  assert.throws(() => evaluateDiagnostics([a], makeBaseline([makeEntry(a)])), /explicitly identified dependency root/);
  assert.equal(evaluateDiagnostics([a, b], makeBaseline()).unapprovedWarnings.length, 2);
});

test('compile and summary duplicates count once and strip root', () => {
  const root = 'D:/a/repo with spaces/repo';
  const line = String.raw`D:\a\repo with spaces\repo\WeaselIPC\PipeChannel.cpp(27,9): warning C4267: 'argument': conversion from 'size_t' to 'DWORD', possible loss of data [D:\a\repo with spaces\repo\WeaselIPC\WeaselIPC.vcxproj]`;
  const records = collectDiagnostics(
    '  1>' + line + '\r\n' + line + '\r\n',
    { architecture: 'x64', origin: 'production', control: '' },
    root
  );

  assert.equal(records.length, 1);
  const record = records[0];
  assert.equal(record.severity, 'warning');
  assert.equal(record.code, 'C4267');
  assert.equal(
    record.message,
    "'argument': conversion from 'size_t' to 'DWORD', possible loss of data"
  );
  assert.equal(record.source, 'WeaselIPC/PipeChannel.cpp');
  assert.equal(record.line, 27);
  assert.equal(record.column, 9);
  assert.equal(record.project, 'WeaselIPC');
  assert.equal(record.architecture, 'x64');
  assert.equal(record.origin, 'production');
  assert.equal(record.control, '');
  assert.ok(!JSON.stringify(records).includes(root));
  assert.ok(
    !JSON.stringify(records).includes(
      String.raw`D:\a\repo with spaces\repo`
    )
  );
});

test('same warning in Win32 and x64 counts twice and LINK without location fails', () => {
  const root = 'D:/a/repo';
  const line = String.raw`LINK : warning LNK4020: corrupt type record [D:\a\repo\WeaselUI\WeaselUI.vcxproj]`;
  const x64 = collectDiagnostics(
    line,
    { architecture: 'x64', origin: 'production', control: '' },
    root
  );
  const win32 = collectDiagnostics(
    line,
    { architecture: 'Win32', origin: 'production', control: '' },
    root
  );
  const records = [...x64, ...win32];

  assert.equal(records.length, 2);
  assert.equal(records[0].source, 'LINK');
  assert.equal(records[0].line, 0);
  assert.equal(records[0].column, 0);
  assert.equal(records[0].project, 'WeaselUI');

  const result = evaluateDiagnostics(records, makeBaseline());
  assert.equal(result.errors.length, 0);
  assert.equal(result.unapprovedWarnings.length, 2);
  assert.equal(result.approvedWarnings.length, 0);
  assert.equal(result.passed, false);
});

test('fatal error remains an error and cannot be approved by baseline', () => {
  const root = 'D:/a/repo';
  const line = String.raw`file.cpp(3): fatal error C1083: cannot open file [x.vcxproj]`;
  const records = collectDiagnostics(
    line,
    { architecture: 'x64', origin: 'production', control: '' },
    root
  );

  assert.equal(records.length, 1);
  const record = records[0];
  assert.equal(record.severity, 'error');
  assert.equal(record.code, 'C1083');
  assert.equal(record.line, 3);
  assert.equal(record.column, 0);
  assert.equal(record.project, 'x');

  const result = evaluateDiagnostics(
    records,
    makeBaseline([makeEntry(record)])
  );
  assert.equal(result.errors.length, 1);
  assert.equal(result.approvedWarnings.length, 0);
  assert.equal(result.unusedBaseline.length, 1);
  assert.equal(result.passed, false);
});

test('non-diagnostic output is ignored', () => {
  const text = [
    String.raw`throw 'Native failed'`,
    String.raw`Write-Output 'warning C4244: pretend'`,
    String.raw`// warning C4244: pretend`,
    String.raw`some source line`,
  ].join('\r\n');

  const records = collectDiagnostics(
    text,
    { architecture: 'x64', origin: 'production', control: '' },
    'D:/a/repo'
  );
  assert.equal(records.length, 0);
});

test('command-line option warnings remain unapproved diagnostics', () => {
  const records = collectDiagnostics(
    "cl : Command line warning D9002 : ignoring unknown option '/foo'",
    { architecture: 'x64', origin: 'production', control: '' },
    'D:/a/repo'
  );
  assert.equal(records.length, 1);
  assert.equal(records[0].code, 'D9002');
  assert.equal(evaluateDiagnostics(records, makeBaseline()).passed, false);
});

test('MSVC Program Files (x86) normalization and exact baseline fields', () => {
  const root = 'D:/a/repo';
  const line = String.raw`1>C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.38.33130\include\vector(123,4): warning C4244: 'initializing': conversion from 'double' to 'int', possible loss of data [D:\a\repo\WeaselUI\WeaselUI.vcxproj]`;
  const records = collectDiagnostics(
    line,
    {
      architecture: 'x64',
      origin: 'negative-control',
      control: 'old-algorithms',
    },
    root
  );

  assert.equal(records.length, 1);
  const record = records[0];
  assert.equal(record.source, '$MSVC/include/vector');
  assert.equal(record.line, 123);
  assert.equal(record.column, 4);
  assert.equal(record.project, 'WeaselUI');
  assert.equal(record.origin, 'negative-control');
  assert.equal(record.control, 'old-algorithms');

  const baseline = makeBaseline([makeEntry(record)]);
  const approved = evaluateDiagnostics(records, baseline);
  assert.equal(approved.approvedWarnings.length, 1);
  assert.equal(approved.unapprovedWarnings.length, 0);
  assert.equal(approved.unusedBaseline.length, 0);
  assert.equal(approved.passed, true);

  const mutations = [
    { control: '' },
    { origin: 'production' },
    { architecture: 'Win32' },
    { column: 5 },
    { message: record.message + ' changed' },
  ];
  for (const mutation of mutations) {
    const result = evaluateDiagnostics([{ ...record, ...mutation }], baseline);
    assert.equal(result.approvedWarnings.length, 0);
    assert.equal(result.unapprovedWarnings.length, 1);
    assert.equal(result.unusedBaseline.length, 1);
    assert.equal(result.passed, false);
  }
});

test('unused and invalid baselines', () => {
  const record = {
    severity: 'warning',
    code: 'C4244',
    message: 'fixture message',
    source: 'src/file.cpp',
    line: 1,
    column: 0,
    project: 'fixture',
    architecture: 'x64',
    origin: 'production',
    control: '',
  };

  const unused = evaluateDiagnostics([], makeBaseline([makeEntry(record)]));
  assert.equal(unused.unusedBaseline.length, 1);
  assert.equal(unused.passed, false);

  assert.throws(
    () =>
      evaluateDiagnostics(
        [],
        makeBaseline([makeEntry(record), makeEntry(record)])
      ),
    /duplicate/i
  );
  assert.throws(
    () =>
      evaluateDiagnostics(
        [],
        makeBaseline([makeEntry(record, { reason: ' ' })])
      ),
    /reason/i
  );
  assert.throws(
    () =>
      evaluateDiagnostics(
        [],
        makeBaseline([makeEntry(record, { source: 'src/*.cpp' })])
      ),
    /glob|regex/i
  );
  assert.throws(
    () => evaluateDiagnostics([], { schemaVersion: 2, entries: [] }),
    /schemaVersion/i
  );
  assert.throws(
    () =>
      evaluateDiagnostics(
        [],
        makeBaseline([makeEntry(record, { extra: 'x' })])
      ),
    /keys/i
  );
});

test('control snapshot prefixes normalize and stay scoped by meta', () => {
  const root = 'D:/a/repo';
  const hash = '0123456789abcdef0123456789abcdef';

  for (const control of ['old-algorithms', 'old-placement']) {
    const line = String.raw`1>.tools/native-baseline-${hash}/${control}/sub/file.cpp(10,2): warning C4244: 'x': y [D:\a\repo\WeaselUI\WeaselUI.vcxproj]`;
    const records = collectDiagnostics(
      line,
      { architecture: 'x64', origin: 'negative-control', control },
      root
    );

    assert.equal(records.length, 1);
    const record = records[0];
    assert.equal(record.source, 'sub/file.cpp');
    assert.equal(record.control, control);
    assert.equal(record.origin, 'negative-control');

    const baseline = makeBaseline([makeEntry(record)]);
    const approved = evaluateDiagnostics(records, baseline);
    assert.equal(approved.approvedWarnings.length, 1);
    assert.equal(approved.passed, true);

    const production = { ...record, origin: 'production', control: '' };
    const leaked = evaluateDiagnostics([production], baseline);
    assert.equal(leaked.approvedWarnings.length, 0);
    assert.equal(leaked.unapprovedWarnings.length, 1);
    assert.equal(leaked.unusedBaseline.length, 1);
    assert.equal(leaked.passed, false);
  }
});
