'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const BASELINE_KEYS = [
  'architecture',
  'code',
  'column',
  'control',
  'line',
  'message',
  'origin',
  'project',
  'reference',
  'reason',
  'source',
].sort();

const CODE_PATTERN = /^(C[0-9]{4}|D[0-9]{4}|RC[0-9]{4}|MIDL[0-9]{4}|LNK[0-9]{4}|MSB[0-9]{4})$/;
const PATTERN_CHARS = /[*?\[\]{}|^+\\]/;
const DOLLAR_PREFIX = /^(?:\$MSVC\/|\$WindowsSDK\/|\$EXTERNAL\/)/;

function normalizeSource(source, root) {
  let value = String(source == null ? '' : source).trim();
  if (value.length >= 2) {
    const first = value[0];
    const last = value[value.length - 1];
    if ((first === '"' && last === '"') || (first === "'" && last === "'")) {
      value = value.slice(1, -1).trim();
    }
  }

  value = value.replace(/\\/g, '/');
  const rootValue = String(root == null ? '' : root)
    .replace(/\\/g, '/')
    .replace(/\/+$/, '');
  if (rootValue) {
    const lowerValue = value.toLowerCase();
    const lowerRoot = rootValue.toLowerCase();
    if (lowerValue === lowerRoot) {
      value = '';
    } else if (lowerValue.startsWith(lowerRoot + '/')) {
      value = value.slice(rootValue.length + 1);
    }
  }

  value = value.replace(
    /^(?:\.\/)?\.tools\/native-baseline-[0-9a-fA-F]{32}\/(?:old-algorithms|old-placement)\//,
    ''
  );
  value = value.replace(
    /^.*\/VC\/Tools\/MSVC\/[^/]+\/include\//i,
    () => '$MSVC/include/'
  );
  value = value.replace(
    /^.*\/Windows Kits\/[^/]+\/Include\/[^/]+\//i,
    () => '$WindowsSDK/include/'
  );

  if (/^(?:[A-Za-z]:\/|\/\/|\/)/.test(value)) {
    const base = path.posix.basename(value);
    const identity = crypto.createHash('sha256').update(value.toLowerCase()).digest('hex');
    value = '$EXTERNAL/' + identity + '/' + base;
  }
  return value;
}

function diagnosticKey(value, includeSeverity = false) {
  const fields = includeSeverity
    ? [
        value.severity,
        value.code,
        value.message,
        value.source,
        value.line,
        value.column,
        value.project,
        value.architecture,
        value.origin,
        value.control,
      ]
    : [
        value.code,
        value.message,
        value.source,
        value.line,
        value.column,
        value.project,
        value.architecture,
        value.origin,
        value.control,
      ];
  return JSON.stringify(fields);
}

function validateBaseline(baseline) {
  if (!baseline || typeof baseline !== 'object' || Array.isArray(baseline)) {
    throw new Error('baseline must be an object');
  }
  if (baseline.schemaVersion !== 1) {
    throw new Error('baseline.schemaVersion must be 1');
  }
  if (!Array.isArray(baseline.entries)) {
    throw new Error('baseline.entries must be an array');
  }

  const signatures = new Set();
  baseline.entries.forEach((entry, index) => {
    const label = `baseline.entries[${index}]`;
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
      throw new Error(`${label} must be an object`);
    }
    const keys = Object.keys(entry).sort();
    if (
      keys.length !== BASELINE_KEYS.length ||
      keys.some((key, i) => key !== BASELINE_KEYS[i])
    ) {
      throw new Error(
        `${label} must have exactly these keys: ${BASELINE_KEYS.join(', ')}`
      );
    }

    for (const key of [
      'code',
      'message',
      'source',
      'project',
      'architecture',
      'origin',
      'control',
      'reason',
      'reference',
    ]) {
      if (typeof entry[key] !== 'string') {
        throw new Error(`${label}.${key} must be a string`);
      }
    }
    for (const key of ['line', 'column']) {
      if (!Number.isInteger(entry[key]) || entry[key] < 0) {
        throw new Error(`${label}.${key} must be a non-negative integer`);
      }
    }
    if (!CODE_PATTERN.test(entry.code)) {
      throw new Error(`${label}.code must be a diagnostic code`);
    }
    if (entry.source.startsWith('$EXTERNAL/')) {
      throw new Error(`${label}.source must use an explicitly identified dependency root`);
    }
    for (const key of ['code', 'source', 'project']) {
      if (
        PATTERN_CHARS.test(entry[key]) ||
        (entry[key].includes('$') &&
          !(key === 'source' && DOLLAR_PREFIX.test(entry[key])))
      ) {
        throw new Error(
          `${label}.${key} must not contain glob or regex characters`
        );
      }
    }
    if (entry.architecture !== 'x64' && entry.architecture !== 'Win32') {
      throw new Error(`${label}.architecture must be x64 or Win32`);
    }
    if (
      entry.origin !== 'production' &&
      entry.origin !== 'negative-control'
    ) {
      throw new Error(
        `${label}.origin must be production or negative-control`
      );
    }
    if (
      entry.control !== '' &&
      entry.control !== 'old-algorithms' &&
      entry.control !== 'old-placement'
    ) {
      throw new Error(`${label}.control is invalid`);
    }
    if (!entry.reason.trim()) {
      throw new Error(`${label}.reason must not be empty`);
    }
    if (!entry.reference.trim()) {
      throw new Error(`${label}.reference must not be empty`);
    }

    const key = diagnosticKey(entry, false);
    if (signatures.has(key)) {
      throw new Error(`${label} duplicates an earlier baseline signature`);
    }
    signatures.add(key);
  });
}

function readLog(filePath) {
  const buffer = fs.readFileSync(filePath);
  if (buffer.length === 0) {
    throw new Error(`log is empty: ${filePath}`);
  }
  if (buffer.length >= 2 && buffer[0] === 0xfe && buffer[1] === 0xff) {
    throw new Error(`UTF-16BE log is not supported: ${filePath}`);
  }

  let text;
  if (buffer.length >= 2 && buffer[0] === 0xff && buffer[1] === 0xfe) {
    text = new TextDecoder('utf-16le', { fatal: true }).decode(buffer.subarray(2));
  } else if (
    buffer.length >= 3 &&
    buffer[0] === 0xef &&
    buffer[1] === 0xbb &&
    buffer[2] === 0xbf
  ) {
    text = new TextDecoder('utf-8', { fatal: true }).decode(buffer.subarray(3));
  } else {
    text = new TextDecoder('utf-8', { fatal: true }).decode(buffer);
  }
  if (text.includes('\0')) { throw new Error('NUL in build log; unsupported or corrupt encoding'); }
  if (text.length === 0) {
    throw new Error(`log is empty: ${filePath}`);
  }
  return text;
}

function collectDiagnostics(text, meta, root) {
  if (typeof text !== 'string') {
    throw new Error('text must be a string');
  }
  if (!meta || typeof meta !== 'object') {
    throw new Error('meta must be an object');
  }

  const records = [];
  const seen = new Set();
  const lines = text.split(/\r\n|\n|\r/);
  for (const rawLine of lines) {
    let lineText = rawLine.replace(/^\s+/, '');
    lineText = lineText.replace(/^\d+>/, '');
    lineText = lineText.replace(/^\s+/, '');
    if (!lineText) {
      continue;
    }

    let project = '';
    const projectMatch = lineText.match(/\s*\[([^\[\]]*\.vcxproj)\]\s*$/i);
    if (projectMatch) {
      project = path.win32
        .basename(projectMatch[1])
        .replace(/\.vcxproj$/i, '');
      lineText = lineText.slice(0, projectMatch.index).trimEnd();
    }

    const match = lineText.match(
      /^(.+?):\s*(?:Command line\s+)?(warning|fatal error|error)\s+(C[0-9]{4}|D[0-9]{4}|RC[0-9]{4}|MIDL[0-9]{4}|LNK[0-9]{4}|MSB[0-9]{4})\s*:\s*(.*)$/
    );
    if (!match) {
      if (!/^(?:\/\/|\/\*|\*|#|throw\b|Write-Output\b)/i.test(lineText) &&
          /^.+?(?:\(\d+(?:,\d+)?\)|\bLINK|\bcl|\brc)\s*:\s*.*?\b(?:C|D|RC|MIDL|LNK|MSB)[0-9]{4}\s*:/i.test(lineText)) {
        throw new Error('Unrecognized native diagnostic format; check VSLANG and log encoding');
      }
      continue;
    }

    let source = match[1].trim();
    if (/^(?:\/\/|\/\*|\*|#)/.test(source)) {
      continue;
    }
    let line = 0;
    let column = 0;
    const location = source.match(/\s*\((\d+)(?:,(\d+))?\)\s*$/);
    if (location) {
      line = Number(location[1]);
      column = location[2] ? Number(location[2]) : 0;
      source = source.slice(0, location.index).trim();
    }
    if (!source) {
      continue;
    }

    const record = {
      severity: match[2] === 'warning' ? 'warning' : 'error',
      code: match[3],
      message: match[4].trim(),
      source: normalizeSource(source, root),
      line,
      column,
      project,
      architecture: meta.architecture,
      origin: meta.origin,
      control: meta.control,
    };
    const key = diagnosticKey(record, true);
    if (!seen.has(key)) {
      seen.add(key);
      records.push(record);
    }
  }
  return records;
}

function evaluateDiagnostics(records, baseline) {
  validateBaseline(baseline);
  if (!Array.isArray(records)) {
    throw new Error('records must be an array');
  }

  const unique = [];
  const seen = new Set();
  for (const record of records) {
    if (!record || typeof record !== 'object' || Array.isArray(record)) {
      throw new Error('each diagnostic record must be an object');
    }
    const key = diagnosticKey(record, true);
    if (!seen.has(key)) {
      seen.add(key);
      unique.push(record);
    }
  }

  const baselineByKey = new Map();
  for (const entry of baseline.entries) {
    baselineByKey.set(diagnosticKey(entry, false), entry);
  }

  const errors = [];
  const unapprovedWarnings = [];
  const approvedWarnings = [];
  const used = new Set();
  for (const record of unique) {
    if (record.severity === 'error') {
      errors.push(record);
      continue;
    }
    if (record.severity !== 'warning') {
      unapprovedWarnings.push(record);
      continue;
    }
    const key = diagnosticKey(record, false);
    if (baselineByKey.has(key)) {
      approvedWarnings.push(record);
      used.add(key);
    } else {
      unapprovedWarnings.push(record);
    }
  }

  const unusedBaseline = baseline.entries.filter(
    (entry) => !used.has(diagnosticKey(entry, false))
  );
  return {
    schemaVersion: 1,
    errors,
    unapprovedWarnings,
    approvedWarnings,
    unusedBaseline,
    passed:
      errors.length === 0 &&
      unapprovedWarnings.length === 0 &&
      unusedBaseline.length === 0,
  };
}

function main(argv) {
  const parseArgs = () => {
    const allowed = ['--root', '--inputs', '--baseline', '--output', '--summary'];
    const parsed = {};
    for (let i = 0; i < argv.length; i += 2) {
      const key = argv[i];
      const value = argv[i + 1];
      if (!allowed.includes(key)) {
        throw new Error(`unknown argument: ${key}`);
      }
      if (value === undefined || value.startsWith('--')) {
        throw new Error(`missing value for ${key}`);
      }
      if (Object.prototype.hasOwnProperty.call(parsed, key)) {
        throw new Error(`duplicate argument: ${key}`);
      }
      parsed[key] = value;
    }
    for (const key of allowed) {
      if (!parsed[key]) {
        throw new Error(`missing argument: ${key}`);
      }
    }
    return parsed;
  };

  const args = parseArgs();
  const root = path.resolve(args['--root']);

  const resolveConfigPath = (value, label) => {
    if (typeof value !== 'string' || !value) {
      throw new Error(`missing ${label} path`);
    }
    const full =
      path.isAbsolute(value) || path.win32.isAbsolute(value)
        ? path.resolve(value)
        : path.resolve(root, value);
    const relative = path.relative(root, full);
    if (
      relative === '..' ||
      relative.startsWith('..' + path.sep) ||
      path.isAbsolute(relative)
    ) {
      throw new Error(`${label} path must be inside root`);
    }
    return full;
  };

  const resolveLogPath = (value) => {
    if (typeof value !== 'string' || !value) {
      throw new Error('log path must be a non-empty relative path');
    }
    if (path.isAbsolute(value) || path.win32.isAbsolute(value)) {
      throw new Error(`log path must be relative: ${value}`);
    }
    const full = path.resolve(root, value);
    const relative = path.relative(root, full);
    if (
      relative === '..' ||
      relative.startsWith('..' + path.sep) ||
      path.isAbsolute(relative)
    ) {
      throw new Error(`log path escapes root: ${value}`);
    }
    return full;
  };

  const readJson = (filePath, label) => {
    let text;
    try {
      text = fs.readFileSync(filePath, 'utf8');
    } catch (error) {
      throw new Error(`cannot read ${label} file ${filePath}: ${error.message}`);
    }
    try {
      return JSON.parse(text);
    } catch (error) {
      throw new Error(`invalid JSON in ${label} file ${filePath}: ${error.message}`);
    }
  };

  const validateInputs = (inputs) => {
    if (!inputs || typeof inputs !== 'object' || Array.isArray(inputs)) {
      throw new Error('inputs must be an object');
    }
    if (inputs.schemaVersion !== 1) {
      throw new Error('inputs.schemaVersion must be 1');
    }
    if (!Array.isArray(inputs.logs)) {
      throw new Error('inputs.logs must be an array');
    }
    const expected = ['architecture', 'control', 'origin', 'path'];
    inputs.logs.forEach((log, index) => {
      const label = `inputs.logs[${index}]`;
      if (!log || typeof log !== 'object' || Array.isArray(log)) {
        throw new Error(`${label} must be an object`);
      }
      const keys = Object.keys(log).sort();
      if (
        keys.length !== expected.length ||
        keys.some((key, i) => key !== expected[i])
      ) {
        throw new Error(
          `${label} must have exactly these keys: ${expected.join(', ')}`
        );
      }
      if (typeof log.path !== 'string' || !log.path) {
        throw new Error(`${label}.path must be a non-empty string`);
      }
      if (log.architecture !== 'x64' && log.architecture !== 'Win32') {
        throw new Error(`${label}.architecture must be x64 or Win32`);
      }
      if (log.origin !== 'production' && log.origin !== 'negative-control') {
        throw new Error(
          `${label}.origin must be production or negative-control`
        );
      }
      if (
        log.control !== '' &&
        log.control !== 'old-algorithms' &&
        log.control !== 'old-placement'
      ) {
        throw new Error(`${label}.control is invalid`);
      }
    });

    const productionArchitectures = new Set(
      inputs.logs
        .filter((log) => log.origin === 'production')
        .map((log) => log.architecture)
    );
    if (
      !productionArchitectures.has('x64') ||
      !productionArchitectures.has('Win32')
    ) {
      throw new Error(
        'production logs must include both x64 and Win32 architectures'
      );
    }
  };

  const dedupeRecords = (values) => {
    const result = [];
    const seen = new Set();
    for (const value of values) {
      const key = diagnosticKey(value, true);
      if (!seen.has(key)) {
        seen.add(key);
        result.push(value);
      }
    }
    return result;
  };

  const makeReport = (values, evaluation, logCount, errorMessage) => {
    const errors = evaluation
      ? evaluation.errors
      : values.filter((value) => value.severity === 'error');
    const unapprovedWarnings = evaluation
      ? evaluation.unapprovedWarnings
      : values.filter((value) => value.severity === 'warning');
    const approvedWarnings = evaluation ? evaluation.approvedWarnings : [];
    const unusedBaseline = evaluation ? evaluation.unusedBaseline : [];
    const passed = evaluation ? evaluation.passed : false;
    const counts = {
      productionWarnings: values.filter(
        (value) =>
          value.severity === 'warning' && value.origin === 'production'
      ).length,
      negativeControlWarnings: values.filter(
        (value) =>
          value.severity === 'warning' && value.origin === 'negative-control'
      ).length,
      approvedBaseline: approvedWarnings.length,
      errors: errors.length,
      unapprovedWarnings: unapprovedWarnings.length,
      unusedBaseline: unusedBaseline.length,
    };
    const report = {
      schemaVersion: 1,
      sourceLogCount: logCount,
      records: values,
      errors,
      unapprovedWarnings,
      approvedWarnings,
      unusedBaseline,
      passed,
      counts,
    };
    if (errorMessage) {
      report.error = errorMessage;
    }
    return report;
  };

  const writeJson = (filePath, value) => {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.writeFileSync(filePath, JSON.stringify(value, null, 2) + '\n', 'utf8');
  };

  const writeSummary = (filePath, report) => {
    const lines = [
      '# Native diagnostics',
      '',
      `- Source logs: ${report.sourceLogCount}`,
      `- Production warnings: ${report.counts.productionWarnings}`,
      `- Negative-control warnings: ${report.counts.negativeControlWarnings}`,
      `- Approved baseline: ${report.counts.approvedBaseline}`,
      `- Errors: ${report.counts.errors}`,
      `- Unapproved warnings: ${report.counts.unapprovedWarnings}`,
      `- Unused baseline: ${report.counts.unusedBaseline}`,
      `- Result: ${report.passed ? 'PASS' : 'FAIL'}`,
      '',
    ];
    if (report.error) {
      lines.push(`- Error: ${report.error}`, '');
    }
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.writeFileSync(filePath, lines.join('\n'), 'utf8');
  };

  const outputPath = resolveConfigPath(args['--output'], 'output');
  const summaryPath = resolveConfigPath(args['--summary'], 'summary');
  let records = [];
  let sourceLogCount = 0;

  try {
    const inputsPath = resolveConfigPath(args['--inputs'], 'inputs');
    const baselinePath = resolveConfigPath(args['--baseline'], 'baseline');
    const inputs = readJson(inputsPath, 'inputs');
    validateInputs(inputs);
    sourceLogCount = inputs.logs.length;

    const collected = [];
    for (const log of inputs.logs) {
      const logPath = resolveLogPath(log.path);
      const text = readLog(logPath);
      collected.push(
        ...collectDiagnostics(
          text,
          {
            architecture: log.architecture,
            origin: log.origin,
            control: log.control,
          },
          root
        )
      );
    }
    records = dedupeRecords(collected);

    const baseline = readJson(baselinePath, 'baseline');
    const evaluation = evaluateDiagnostics(records, baseline);
    const report = makeReport(records, evaluation, sourceLogCount);
    writeJson(outputPath, report);
    writeSummary(summaryPath, report);
    return evaluation.passed ? 0 : 1;
  } catch (error) {
    const rawMessage =
      error && error.message ? error.message : String(error);
    const safeMessage = rawMessage.split(root).join('<root>');
    const report = makeReport(records, null, sourceLogCount, safeMessage);
    try {
      writeJson(outputPath, report);
      writeSummary(summaryPath, report);
    } catch (_) {
      // Preserve the original configuration/diagnostic error.
    }
    throw error;
  }
}

module.exports = { collectDiagnostics, evaluateDiagnostics };

if (require.main === module) {
  try {
    process.exitCode = main(process.argv.slice(2));
  } catch (error) {
    console.error(error && error.message ? error.message : String(error));
    process.exitCode = 2;
  }
}
