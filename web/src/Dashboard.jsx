import { useEffect, useState } from "react";
import { ref, onValue } from "firebase/database";
import { db } from "./firebase";

const STALE_MS = 5 * 60 * 1000; // the ESP32 writes every minute
const COLOR = { idle: "--idle", printing: "--printing", paused: "--paused", finished: "--finished", error: "--error", offline: "--offline" };

const dur = (s) => {
  s = Math.max(0, Math.round(s));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h ? `${h}h ${m}m` : `${m}m`;
};
const when = (ms) => new Date(ms).toLocaleString([], { weekday: "short", hour: "numeric", minute: "2-digit" });

function Card({ p, now }) {
  const age = now - (p.updatedAt || 0);
  const stale = age > STALE_MS;
  const state = stale ? "offline" : p.state || "idle";
  const printing = state === "printing";
  const left = printing ? Math.max(0, (p.remainingSec || 0) - age / 1000) : 0;
  const pct = !stale && p.progress != null ? Math.round(p.progress * 100) : null;
  return (
    <div className="card">
      <div className="top">
        <span className="name">{p.name || p.id}</span>
        <span className="badge" style={{ background: `var(${COLOR[state] || "--idle"})` }}>{state}</span>
      </div>
      <div className="file">{stale ? `No update from the ESP32 for ${dur(age / 1000)}` : p.filename || ""}</div>
      <div className="bar"><i style={{ width: `${pct || 0}%` }} /></div>
      <div className="row"><span>Progress <b>{pct != null ? pct + "%" : "–"}</b></span><span>Left <b>{printing ? dur(left) : "–"}</b></span></div>
      <div className="row"><span>Free at</span><b>{printing ? when(now + left * 1000) : state === "paused" ? "paused" : "now"}</b></div>
      {p.error && !stale && <div className="err">{p.error}</div>}
    </div>
  );
}

export default function Dashboard() {
  const [data, setData] = useState(null);
  const [error, setError] = useState("");
  const [now, setNow] = useState(Date.now());

  useEffect(() => onValue(ref(db, "status"), (s) => { setData(s.val() || {}); setError(""); }, (e) => setError(e.message)), []);
  useEffect(() => { const t = setInterval(() => setNow(Date.now()), 15000); return () => clearInterval(t); }, []);

  if (error) return <p className="err">Cannot read data: {error}</p>;
  if (!data) return <p className="center">Loading…</p>;
  const list = Object.entries(data).map(([id, p]) => ({ id, ...p })).sort((a, b) => (a.name || a.id).localeCompare(b.name || b.id));
  if (!list.length) return <p className="center">No printer data yet. Is the ESP32 running?</p>;
  return <div className="grid">{list.map((p) => <Card key={p.id} p={p} now={now} />)}</div>;
}
