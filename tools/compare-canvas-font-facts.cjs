"use strict";
// Read-only comparison of independently produced native rectangles with Canvas metrics.
// No equality gate: a text rectangle bottom is not a glyph baseline.
const fs = require("node:fs"), crypto = require("node:crypto"), os = require("node:os");
const {chromium} = require(process.env.PLAYWRIGHT_MODULE_PATH || "../designer/node_modules/playwright");
async function main() {
  const [input, output] = process.argv.slice(2);
  if (!input || !output) throw Error("Provide native text facts JSON and a new report path");
  if (fs.existsSync(output)) throw Error("Do not overwrite an earlier measurement");
  const bytes = fs.readFileSync(input), native = JSON.parse(bytes);
  if (native.schemaVersion !== 1 || native.scope !== "native-text-facts" || native.font !== "Microsoft YaHei UI") throw Error("Unexpected native facts contract");
  const browser = await chromium.launch({headless: true, ...(process.env.DESIGNER_BROWSER_CHANNEL ? {channel: process.env.DESIGNER_BROWSER_CHANNEL} : {})});
  try {
    const rows = [], fontChecks = []; let requests = 0;
    for (const dpi of native.input.dpi) {
      const context = await browser.newContext({deviceScaleFactor: dpi / 96});
      try {
        await context.route("**/*", route => { requests++; return route.abort(); });
        const page = await context.newPage();
        const samples = [];
        for (const item of native.cases.filter(c => c.measurement === "directwrite" && c.dpi === dpi)) {
          const source = native.input.cases.find(c => c.id === item.id);
          item.cells.forEach((cell, index) => {
            samples.push({id: item.id, index, role: "candidate", text: source.candidates[index].text, pt: 12, rect: cell.text});
            if (source.candidates[index].comment && cell.comment[2] > cell.comment[0]) samples.push({id: item.id, index, role: "comment", text: source.candidates[index].comment, pt: 10, rect: cell.comment});
          });
        }
        const facts = await page.evaluate(async samples => {
          await document.fonts.ready;
          const canvas = document.createElement("canvas"), drawing = canvas.getContext("2d");
          return {fontCheck: document.fonts.check('16px "Microsoft YaHei UI"'), samples: samples.map(sample => {
            drawing.font = `${sample.pt * 96 / 72}px "Microsoft YaHei UI"`;
            const m = drawing.measureText(sample.text);
            return {...sample, widthDip: m.width, inkAscentDip: m.actualBoundingBoxAscent, inkDescentDip: m.actualBoundingBoxDescent};
          })};
        }, samples);
        fontChecks.push({dpi, canvasFontCheck: facts.fontCheck, note: "font check is not proof of the resolved native or fallback face"});
        for (const sample of facts.samples) {
          const width = sample.rect[2] - sample.rect[0], physical = Math.ceil(sample.widthDip * dpi / 96);
          rows.push({...sample, dpi, nativeRectWidthPx: width, canvasCeilWidthPx: physical, deltaPx: physical - width});
        }
      } finally { await context.close(); }
    }
    if (requests) throw Error("Unexpected network request during isolated font comparison");
    const groups = native.input.dpi.map(dpi => {
      const values = rows.filter(r => r.dpi === dpi).map(r => r.deltaPx);
      return {dpi, samples: values.length, minDeltaPx: Math.min(...values), maxDeltaPx: Math.max(...values), maxAbsoluteDeltaPx: Math.max(...values.map(Math.abs))};
    });
    const report = {schemaVersion: 1, scope: "canvas-native-font-facts", inputSha256: crypto.createHash("sha256").update(bytes).digest("hex"),
      browserVersion: browser.version(), node: process.version, platform: os.platform(), release: os.release(), requestedFont: native.font,
      nativeGlyphBaseline: "unavailable: native textBottom is a rectangle boundary, not a font baseline", resolvedFontFace: "not verified", networkRequests: requests,
      note: "Canvas width * dpi/96 is rounded upward and compared with native text rectangle width; native extent may include overhang or rounding. Differences are facts, not a pixel-equivalence assertion.", fontChecks, groups, rows};
    fs.writeFileSync(output, JSON.stringify(report, null, 2) + "\n");
    console.log(JSON.stringify({groups, browserVersion: report.browserVersion, nativeGlyphBaseline: report.nativeGlyphBaseline}));
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
