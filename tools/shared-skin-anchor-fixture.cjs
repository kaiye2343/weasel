"use strict";
// Test-only bridge: supplied browser frame geometry -> real native anchor code.
// This does not claim that native text layout has been measured or compared.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const G = require("../designer/geometry.js");
const P = require("../designer/package.js");
const fixturePath = path.join(__dirname, "../tests/fixtures/skin-anchor-layout-v1.json");

function loadCases() {
  const bytes = fs.readFileSync(fixturePath);
  const fixture = JSON.parse(bytes);
  assert.equal(fixture.schemaVersion, 1);
  assert.equal(fixture.scope, "decoration-anchors");
  assert.deepEqual(fixture.dpi, [96, 144, 192]);
  assert.ok(fixture.cases.length > 0 && fixture.cases.length <= 64);
  const metrics = fixture.measurements;
  for (const n of Object.values(metrics)) assert.ok(Number.isInteger(n) && n > 0 && n <= 128);
  const ids = new Set();
  const cases = [];
  for (const item of fixture.cases) {
    assert.match(item.id, /^[a-z0-9-]{1,64}$/);
    assert.ok(!ids.has(item.id)); ids.add(item.id);
    assert.ok(["collapse", "reserve"].includes(item.emptyAnnotationMode));
    assert.ok(Array.isArray(item.frameOrigin) && item.frameOrigin.length === 2);
    for (const n of item.frameOrigin) assert.ok(Number.isInteger(n) && n >= 0 && n <= 64);
    assert.ok(item.candidates.length > 0 && item.candidates.length <= 10);
    for (const c of item.candidates) {
      assert.equal(typeof c.text, "string"); assert.ok(c.text.length > 0 && c.text.length <= 64);
      assert.equal(typeof c.comment, "string"); assert.ok(c.comment.length <= 128);
    }
    assert.ok(item.layers.length <= 8);
    const layerIds = new Set();
    for (const layer of item.layers) {
      assert.match(layer.id, /^[a-z0-9-]{1,64}$/);
      assert.ok(!layerIds.has(layer.id)); layerIds.add(layer.id);
      assert.ok(["top_left", "top_right", "bottom_left", "bottom_right"].includes(layer.anchor));
      for (const key of ["x", "y", "width", "height"]) {
        assert.ok(Number.isInteger(layer[key]) && Math.abs(layer[key]) <= 2048);
      }
      assert.ok(layer.width > 0 && layer.height > 0);
    }
    const style = P.style({ fontPoint:12, commentFontPoint:10, paddingLeft:4, paddingTop:4,
      paddingRight:4, paddingBottom:4, candidateSpacing:8, annotationPosition:item.annotationPosition });
    const measure = (text, font) => font === style.fontPoint * 4 / 3
      ? {width:text.length * metrics.textAdvance, height:metrics.textHeight}
      : {width:text.length * metrics.commentAdvance, height:metrics.commentHeight};
    const geometry = G.layout(style, item.layers, item.candidates, true, measure,
      {emptyAnnotationMode:item.emptyAnnotationMode});
    for (const dpi of fixture.dpi) {
      const scale = dpi / 96;
      const q = value => Math.trunc(value * scale);
      const [ox, oy] = item.frameOrigin;
      const frame = [q(ox), q(oy), q(ox + geometry.width), q(oy + geometry.height)];
      const canvas = [0, 0, q(geometry.width + ox * 2), q(geometry.height + oy * 2)];
      const layers = item.layers.map(l => ({id:l.id, right:l.anchor.endsWith("right"),
        bottom:l.anchor.startsWith("bottom"), x:q(l.x), y:q(l.y), width:q(l.width), height:q(l.height)}));
      cases.push({id:`${item.id}-${dpi}`, dpi, frame, canvas, layers, geometry, origin:[ox,oy], scale});
    }
  }
  return {fixtureSha256:crypto.createHash("sha256").update(bytes).digest("hex"), cases};
}

function generate(destination) {
  const data = loadCases();
  const lines = [`// Generated from the versioned synthetic fixture; do not edit.`,
    `static constexpr const char* kFixtureSha256 = "${data.fixtureSha256}";`,
    "#ifdef ANCHOR_EXPECTED_BITS",
    'static_assert(sizeof(void*) * 8 == ANCHOR_EXPECTED_BITS, "Wrong fixture architecture");',
    "#endif"];
  data.cases.forEach((c,i) => {
    if (!c.layers.length) return;
    lines.push(`static const AnchorLayer kLayers${i}[] = {`);
    for (const l of c.layers) lines.push(`  {"${l.id}", ${l.right}, ${l.bottom}, ${l.x}, ${l.y}, ${l.width}, ${l.height}},`);
    lines.push("};");
  });
  lines.push("static const AnchorCase kCases[] = {");
  data.cases.forEach((c,i) => lines.push(`  {"${c.id}", ${c.dpi}, {${c.frame}}, {${c.canvas}}, ${c.layers.length ? `kLayers${i}` : "nullptr"}, ${c.layers.length}},`));
  lines.push("};", "");
  fs.writeFileSync(destination, lines.join("\n"));
}

function compare(report) {
  const data = loadCases();
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.scope, "decoration-anchors");
  assert.equal(report.coordinateSpace, "physical-pixels");
  assert.equal(report.fixtureSha256, data.fixtureSha256);
  assert.equal(report.cases.length, data.cases.length);
  const near = (actual, expected, label) => {
    assert.equal(actual.length, expected.length, label);
    actual.forEach((v,i) => assert.ok(Number.isInteger(v) && Math.abs(v-expected[i]) <= 1,
      `${label}[${i}]: native=${v}, browser=${expected[i]}`));
  };
  report.cases.forEach((actual,i) => {
    const c = data.cases[i];
    assert.equal(actual.id, c.id); assert.equal(actual.dpi, c.dpi);
    assert.deepEqual(actual.frame, c.frame);
    assert.equal(actual.layers.length, c.layers.length);
    let bounds = [...c.canvas];
    const rectangles = c.geometry.layers.map(l => [l.left+c.origin[0], l.top+c.origin[1],
      l.left+c.origin[0]+l.width, l.top+c.origin[1]+l.height].map(v=>v*c.scale));
    rectangles.forEach((r,j) => {
      assert.equal(actual.layers[j].id, c.layers[j].id);
      near(actual.layers[j].rect, r, `${c.id}/${c.layers[j].id}`);
      bounds = [Math.min(bounds[0],r[0]),Math.min(bounds[1],r[1]),
        Math.max(bounds[2],r[2]),Math.max(bounds[3],r[3])];
    });
    near(actual.canvas, bounds, `${c.id}/canvas`);
    near(actual.origin, [-bounds[0],-bounds[1]], `${c.id}/origin`);
    actual.layers.forEach((l,j) => near(l.windowRect, rectangles[j].map((v,k)=>v-bounds[k%2]), `${c.id}/${l.id}/window`));
  });
  return data.cases.length;
}

if (require.main === module) {
  try {
    const [mode, file] = process.argv.slice(2);
    if (process.argv.length !== 4 || !["generate", "compare"].includes(mode)) throw Error("Expected generate <include> or compare <report>");
    if (mode === "generate") generate(file);
    else console.log(`PASS ${compare(JSON.parse(fs.readFileSync(file,"utf8")))} native/browser anchor cases (<=1 physical pixel); native text layout not measured`);
  } catch (error) { console.error(error.message); process.exitCode=1; }
}
module.exports = {loadCases, generate, compare};
