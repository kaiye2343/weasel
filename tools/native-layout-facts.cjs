"use strict";
// Native consumes raw candidates and computes HorizontalLayout independently.
// Fixed metrics compare layout, DirectWrite results remain native font facts.
const fs = require("node:fs");
const assert = require("node:assert/strict");
const G = require("../designer/geometry.js");
const P = require("../designer/package.js");
const source = require("../tests/fixtures/skin-anchor-layout-v1.json");
function compare(report) {
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.scope, "native-text-facts");
  assert.equal(report.coordinateSpace, "physical-pixels");
  assert.deepEqual(report.input, source, "report must use current raw fixture");
  assert.equal(report.cases.length, source.cases.length * source.dpi.length * 2);
  const seen = new Set();
  const differences = [];
  const inside = (r, x, y) => x >= r[0] && y >= r[1] && x < r[2] && y < r[3];
  for (const row of report.cases) {
    const key = `${row.id}/${row.dpi}/${row.measurement}`;
    assert.ok(!seen.has(key)); seen.add(key);
    const item = source.cases.find(c => c.id === row.id);
    assert.ok(item && source.dpi.includes(row.dpi));
    assert.ok(["fixed", "directwrite"].includes(row.measurement));
    const q = x => Math.trunc(x * row.dpi / 96);
    assert.deepEqual(row.origin, [q(20), q(10)]);
    assert.deepEqual(row.padding, [q(4), q(3)]);
    assert.equal(row.reorderY, q(7));
    assert.equal(row.cells.length, item.candidates.length);
    assert.ok(row.size.every(v => Number.isInteger(v) && v > 0 && v < 10000));
    const style = P.style({fontPoint:12, commentFontPoint:10, paddingLeft:4,
      paddingTop:4,paddingRight:4,paddingBottom:4,candidateSpacing:8,
      annotationPosition:item.annotationPosition});
    const metrics = source.measurements;
    const measure = (s, font) => font === 16 ? {width:s.length * metrics.textAdvance,height:metrics.textHeight}
      : {width:s.length * metrics.commentAdvance,height:metrics.commentHeight};
    const browser = G.layout(style, item.layers, item.candidates, true, measure,
      {emptyAnnotationMode:item.emptyAnnotationMode});
    const close = (actual, expected) => {
      assert.equal(actual.length, expected.length);
      actual.forEach((v, i) => assert.ok(Number.isInteger(v) && Math.abs(v-expected[i])<=1,
        `${key}: native ${actual} browser ${expected}`));
    };
    if (row.measurement === "fixed") close(row.size, [q(browser.width),q(browser.height)]);
    let textBottom;
    row.cells.forEach((c, index) => {
      const r = c.layout;
      for (const name of ["layout","label","text","comment","draw","hit","windowHit"])
        assert.ok(c[name].length === 4 && c[name].every(Number.isInteger));
      assert.ok(r[0] <= c.text[0] && r[1] <= c.text[1] && r[2] >= c.text[2] && r[3] >= c.text[3]);
      assert.equal(c.textBottom, c.text[3]);
      if (item.annotationPosition === "above") {
        if (textBottom !== undefined) assert.equal(c.textBottom, textBottom, "native row bottoms stable");
        textBottom = c.textBottom;
      }
      if (!item.candidates[index].comment || item.annotationPosition === "hidden")
        assert.ok(c.comment[0]===c.comment[2] && c.comment[1]===c.comment[3],"missing/hidden annotation remains empty");
      assert.deepEqual(c.draw,[r[0]-q(4),r[1]-q(3),r[2]+q(4),r[3]+q(3)]);
      assert.deepEqual(c.hit,[c.draw[0],c.draw[1]+q(7),c.draw[2],c.draw[3]+q(7)]);
      assert.deepEqual(c.windowHit,c.hit.map((v,i)=>v+row.origin[i%2]));
      const h = c.hit;
      assert.ok(inside(h,h[0],h[1]) && !inside(h,h[2],h[1]) && !inside(h,h[0],h[3]));
      // Explicit negative controls: closed right edge and forgotten origin must disagree.
      const closed = (r,x,y) => x>=r[0] && y>=r[1] && x<=r[2] && y<=r[3];
      const closedRight = closed(h,h[2],h[1]);
      assert.notEqual(closedRight,inside(h,h[2],h[1]));
      assert.notDeepEqual(c.windowHit,c.hit);
      if (row.measurement === "fixed") {
        const b = browser.cells[index];
        close(r,[q(b.x),q(browser.rowTop),q(b.x+b.width),q(browser.rowTop+b.height)]);
        close(c.text,[q(b.textX+b.labelWidth+browser.gap),q(b.textY),
          q(b.textX+b.labelWidth+browser.gap+b.textWidth),q(b.textY+metrics.textHeight)]);
      }
    });
    if (row.measurement === "directwrite") differences.push({id:row.id,dpi:row.dpi,
      nativeSize:row.size,fixedBrowserSize:[q(browser.width),q(browser.height)],
      note:"Different measurement sources; this is not an equality assertion or font baseline."});
    if (item.annotationPosition === "hidden" || (item.emptyAnnotationMode === "collapse" &&
        item.candidates.every(c=>!c.comment))) assert.equal(row.separators.length,0);
  }
  return {schemaVersion:1,fixedCases:source.cases.length*source.dpi.length,
    directWriteCases:differences.length,differences};
}
if (require.main === module) {
  const result = compare(JSON.parse(fs.readFileSync(process.argv[2],"utf8")));
  if (process.argv[3]) fs.writeFileSync(process.argv[3],JSON.stringify(result,null,2)+"\n");
  console.log(`PASS native text facts: ${result.fixedCases} independently computed fixed layouts; ${result.directWriteCases} DirectWrite facts (measurement differences recorded)`);
}
module.exports = {compare};
