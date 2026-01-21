const fs = require("fs");
const path = require("path");

const dir = "/Users/ahstanin/GitHub/Olib-AI/olib-browser/fingerprint_results";
const files = fs.readdirSync(dir).filter(f => f.endsWith(".json")).sort();

const profiles = files.map(f => {
  const data = JSON.parse(fs.readFileSync(path.join(dir, f)));
  const raw = data.products?.rawDeviceAttributes?.data || {};
  const id = data.products?.identification?.data || {};

  return {
    file: f.replace("fingerprint_", "").replace(".json", ""),
    visitorId: id.visitorId,
    // Canvas
    canvasGeo: raw.canvas?.value?.Geometry,
    canvasText: raw.canvas?.value?.Text,
    // WebGL
    webglParams: raw.webGlExtensions?.value?.parameters,
    webglExt: raw.webGlExtensions?.value?.extensions,
    webglCtx: raw.webGlExtensions?.value?.contextAttributes,
    webglExtParams: raw.webGlExtensions?.value?.extensionParameters,
    webglShader: raw.webGlExtensions?.value?.shaderPrecisions,
    // WebGL Basics
    webglRenderer: raw.webGlBasics?.value?.rendererUnmasked,
    webglVendor: raw.webGlBasics?.value?.vendorUnmasked,
    // Audio
    audio: raw.audio?.value,
    // Fonts
    fonts: raw.fonts?.value?.length,
    fontsSample: raw.fonts?.value?.slice(0, 5),
    // Screen
    screen: raw.screenResolution?.value,
    // Math
    math: raw.math?.value,
    // Emoji
    emojiHeight: raw.emoji?.value?.height,
    // Hardware
    hardwareConcurrency: raw.hardwareConcurrency?.value,
    deviceMemory: raw.deviceMemory?.value,
    // Font preferences
    fontPrefDefault: raw.fontPreferences?.value?.default,
    fontPrefMono: raw.fontPreferences?.value?.mono,
  };
});

console.log("=== FINGERPRINT COMPARISON (5 profiles) ===\n");

// Compare each field
const fields = [
  ["visitorId", "Visitor ID"],
  ["canvasGeo", "Canvas Geometry"],
  ["canvasText", "Canvas Text"],
  ["webglParams", "WebGL Parameters"],
  ["webglExt", "WebGL Extensions"],
  ["webglCtx", "WebGL Context Attrs"],
  ["webglExtParams", "WebGL Ext Params"],
  ["webglShader", "WebGL Shader Prec"],
  ["audio", "Audio Fingerprint"],
  ["math", "Math Hash"],
  ["fonts", "Font Count"],
  ["fontPrefDefault", "Font Pref Default"],
  ["fontPrefMono", "Font Pref Mono"],
  ["emojiHeight", "Emoji Height"],
  ["hardwareConcurrency", "CPU Cores"],
  ["deviceMemory", "Device Memory"],
];

for (const [key, label] of fields) {
  const values = profiles.map(p => p[key]);
  const unique = [...new Set(values.map(v => JSON.stringify(v)))];
  const allSame = unique.length === 1;
  const status = allSame ? "SAME" : `${unique.length} unique`;
  const icon = allSame ? "[-]" : "[+]";

  console.log(`${icon} ${label}: ${status}`);
  if (!allSame) {
    values.forEach((v, i) => {
      const display = typeof v === "string" && v.length > 20 ? v.slice(0, 16) + "..." : v;
      console.log(`    [${i+1}] ${display}`);
    });
  }
  console.log("");
}

// Show fonts sample
console.log("=== FONT SAMPLES ===");
profiles.forEach((p, i) => {
  console.log(`[${i+1}] ${p.fontsSample?.join(", ")}`);
});

// Show screen
console.log("\n=== SCREEN RESOLUTION ===");
profiles.forEach((p, i) => {
  console.log(`[${i+1}] ${p.screen?.join("x")}`);
});

// Show WebGL renderer
console.log("\n=== WEBGL RENDERER ===");
profiles.forEach((p, i) => {
  console.log(`[${i+1}] ${p.webglRenderer}`);
});

// Summary
console.log("\n=== SUMMARY ===");
const varying = fields.filter(([key]) => {
  const values = profiles.map(p => p[key]);
  const unique = [...new Set(values.map(v => JSON.stringify(v)))];
  return unique.length > 1;
});
const same = fields.filter(([key]) => {
  const values = profiles.map(p => p[key]);
  const unique = [...new Set(values.map(v => JSON.stringify(v)))];
  return unique.length === 1;
});

console.log(`Varying fields: ${varying.length} (${varying.map(f => f[1]).join(", ")})`);
console.log(`Same fields: ${same.length} (${same.map(f => f[1]).join(", ")})`);
