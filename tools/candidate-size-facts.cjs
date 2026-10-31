"use strict";
const fs = require("node:fs");
const assert = require("node:assert/strict");
const fixture = require("../tests/fixtures/candidate-size-occlusion-v1.json");
function verify(report) {
  assert.equal(report.scope, fixture.scope);
  assert.equal(report.coordinateSpace, "physical-pixels");
  assert.deepEqual(report.input, fixture);
  assert.equal(report.cases.length, fixture.cases.length * fixture.dpi.length * 2);
  const seen = new Set(), pairs = [];
  for (const row of report.cases) {
    const source = fixture.cases.find(c => c.id === row.id);
    assert.ok(source && fixture.dpi.includes(row.dpi));
    assert.ok(["fixed", "directwrite"].includes(row.measurement));
    const key = `${row.id}/${row.dpi}/${row.measurement}`;
    assert.ok(!seen.has(key)); seen.add(key);
    const q = n => Math.trunc(n * row.dpi / 96);
    assert.equal(row.fontPoint, source.style.fontPoint);
    assert.equal(row.commentFontPoint, source.style.commentFontPoint);
    assert.ok(Math.abs(row.fontEm - source.style.fontPoint * row.dpi / 72) < 0.001);
    assert.ok(Math.abs(row.commentEm - source.style.commentFontPoint * row.dpi / 72) < 0.001);
    assert.equal(row.images.length, source.layers.length);
    assert.ok(row.frame.every(Number.isInteger));
    const f = row.frame;
    row.images.forEach((rect, i) => {
      const layer = source.layers[i];
      const x = (layer.anchor.endsWith("right") ? f[2] : f[0]) + q(layer.x);
      const y = (layer.anchor.startsWith("bottom") ? f[3] : f[1]) + q(layer.y);
      assert.deepEqual(rect, [x, y, x + q(layer.width), y + q(layer.height)]);
    });
    const bounds = row.images.reduce((b, r) => [Math.min(b[0], r[0]), Math.min(b[1], r[1]),
      Math.max(b[2], r[2]), Math.max(b[3], r[3])], [0, 0, ...row.size]);
    assert.deepEqual(row.canvasBounds, bounds);
    assert.deepEqual(row.canvasOrigin, [-bounds[0], -bounds[1]]);
    assert.deepEqual(row.canvasSize, [bounds[2] - bounds[0], bounds[3] - bounds[1]]);
    assert.equal(row.cells.length, source.candidates.length);
    assert.equal(new Set(row.cells.map(c => c.textBottom)).size, 1);
    if (source.candidates.every(c => !c.comment)) assert.equal(row.separators.length, 0);
  }
  for (const mode of ["mixed", "empty", "long"]) for (const dpi of fixture.dpi) {
    for (const measurement of ["fixed", "directwrite"]) {
      const get = profile => report.cases.find(c => c.id === `${mode}-${profile}` && c.dpi === dpi && c.measurement === measurement);
      const original = get("original"), compact = get("compact");
      assert.ok(compact.size[0] < original.size[0], "compact text area must be narrower");
      assert.ok(compact.size[1] < original.size[1], "compact text area must be shorter");
      pairs.push({mode, dpi, measurement, originalTextSize:original.size, compactTextSize:compact.size,
        originalCanvasSize:original.canvasSize, compactCanvasSize:compact.canvasSize});
    }
  }
  return {scope:fixture.scope, pairs,
    note:"Production HorizontalLayout and pure anchor helpers; no HWND, TSF caret or desktop placement verified. Images intentionally retain size."};
}
if (require.main === module) {
  const result = verify(JSON.parse(fs.readFileSync(process.argv[2], "utf8")));
  fs.writeFileSync(process.argv[3], JSON.stringify(result, null, 2) + "\n");
  console.log(`PASS ${result.pairs.length} candidate size comparisons; single-scale decoration and canvas facts`);
}
module.exports = {verify};
