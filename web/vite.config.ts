import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The C++ backend serves the built app from web/dist and owns the API. In dev,
// Vite runs its own server on :5173 and proxies API + SSE + health calls to the
// backend on :8080 so the frontend can be developed against live data.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://localhost:8080",
        changeOrigin: true,
      },
      "/health": "http://localhost:8080",
    },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
