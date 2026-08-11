/** @type {import('tailwindcss').Config} */
// Starter theme only. The real token system is designed in Phase 11
// (docs/design-brief.md) — these values are placeholders proving the pipeline.
export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        // Instrument-panel starting thesis (subject to Phase 11 revision).
        panel: {
          bg: "#0b0f14",
          surface: "#121821",
          line: "#1e2732",
        },
        signal: {
          up: "#3ddc97",
          down: "#ff5c72",
          calm: "#5b8def",
        },
      },
      fontFamily: {
        mono: ["ui-monospace", "SFMono-Regular", "Menlo", "monospace"],
      },
    },
  },
  plugins: [],
};
