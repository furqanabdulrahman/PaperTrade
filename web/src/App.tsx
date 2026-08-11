import { useEffect, useState } from "react";

type Health = { status: string; service: string; phase: number };

export default function App() {
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetch("/health")
      .then((r) => {
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        return r.json();
      })
      .then(setHealth)
      .catch((e) => setError(String(e)));
  }, []);

  return (
    <main className="min-h-full flex items-center justify-center p-6">
      <div className="max-w-md w-full rounded-xl border border-panel-line bg-panel-surface p-8 text-center shadow-2xl">
        <div className="mb-1 font-mono text-xs uppercase tracking-[0.3em] text-signal-calm">
          Phase 1 · Scaffolding
        </div>
        <h1 className="text-3xl font-semibold tracking-tight">PaperTrade</h1>
        <p className="mt-2 text-sm text-slate-400">
          Practice trading with real market data. A DSA-driven simulator.
        </p>

        <div className="mt-6 rounded-lg border border-panel-line bg-panel-bg p-4 text-left font-mono text-sm">
          <div className="text-xs uppercase tracking-widest text-slate-500">
            backend link
          </div>
          {health && (
            <div className="mt-1 text-signal-up">
              ● {health.service} — {health.status} (phase {health.phase})
            </div>
          )}
          {error && <div className="mt-1 text-signal-down">● {error}</div>}
          {!health && !error && (
            <div className="mt-1 animate-pulse text-slate-500">
              ○ contacting backend…
            </div>
          )}
        </div>
      </div>
    </main>
  );
}
