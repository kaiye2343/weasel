"use strict";
const {test} = require("node:test");
const assert = require("node:assert/strict");
const {loadCases, compare} = require("../tools/shared-skin-anchor-fixture.cjs");

test("shared anchor fixtures cover DPI and frame-height changes", () => {
  const {cases, fixtureSha256} = loadCases();
  assert.equal(cases.length, 24);
  assert.match(fixtureSha256, /^[a-f0-9]{64}$/);
  for (const dpi of [96,144,192]) {
    const get = id => cases.find(c=>c.id===`${id}-${dpi}`);
    const collapse=get("empty-collapse"), reserve=get("empty-reserve");
    assert.ok(collapse.geometry.height < reserve.geometry.height);
    assert.equal(collapse.geometry.hasAnnotation, false);
    assert.equal(reserve.geometry.hasAnnotation, true);
    const mixed=get("above-mixed");
    assert.equal(new Set(mixed.geometry.cells.map(c=>c.textY)).size, 1);
    assert.equal(get("hidden-annotation").geometry.hasAnnotation, false);
    assert.equal(get("all-four-corners").layers.length, 4);
    assert.equal(get("plain-no-decoration").layers.length, 0);
    assert.ok(get("long-content").geometry.width > collapse.geometry.width);
  }
});

test("native report comparison rejects missing or unrelated output", () => {
  assert.throws(()=>compare({schemaVersion:2}));
  assert.throws(()=>compare({schemaVersion:1, scope:"decoration-anchors",
    coordinateSpace:"physical-pixels", fixtureSha256:"unrelated", cases:[]}));
  assert.throws(()=>compare({schemaVersion:1, scope:"decoration-anchors",
    coordinateSpace:"physical-pixels", fixtureSha256:loadCases().fixtureSha256, cases:[]}));
});
