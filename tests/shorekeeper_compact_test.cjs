"use strict";
// The sample's text density is a package choice, never a global skin override.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, "../skins/shorekeeper/manifest.json"), "utf8"));
assert.equal(manifest.version, "1.0.1", "changed defaults ship as a new rollbackable package version");
assert.deepEqual(manifest.variants.map(v => v.id), ["plush", "watermelon", "butterfly"]);
for (const variant of manifest.variants) {
  const s = variant.style;
  assert.equal(s.fontPoint, 12);
  assert.equal(s.commentFontPoint, 10);
  assert.equal(s.candidateSpacing, 8);
  assert.equal(s.paddingLeft, 8);
  assert.equal(s.paddingTop, 4);
  assert.equal(s.paddingBottom, 4);
  assert.equal(s.paddingRight, 192, "retain decoration reservation");
  const character = variant.layers[0];
  assert.deepEqual([character.width, character.height], variant.id === "butterfly" ? [205, 232] : [221, 184]);
  const wing = variant.layers[1];
  assert.deepEqual([wing.x, wing.y, wing.width, wing.height], [-29, -52, 75, 62]);
}
console.log("PASS shorekeeper compact package: three variants; original decoration size/reservation retained");
