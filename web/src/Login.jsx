import { useState } from "react";
import { signInWithEmailAndPassword } from "firebase/auth";
import { auth } from "./firebase";

const BAD = ["auth/invalid-credential", "auth/wrong-password", "auth/user-not-found", "auth/invalid-email"];

export default function Login() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  async function submit(e) {
    e.preventDefault();
    setBusy(true);
    setError("");
    try {
      await signInWithEmailAndPassword(auth, email.trim(), password);
    } catch (err) {
      setError(
        BAD.includes(err.code) ? "Wrong email or password."
        : err.code === "auth/too-many-requests" ? "Too many attempts. Try again later."
        : "Could not sign in (" + err.code + ")"
      );
    }
    setBusy(false);
  }

  return (
    <form className="login card" onSubmit={submit}>
      <h1>Print status</h1>
      <p className="sub">Sign in to see the printers</p>
      <label>Email
        <input type="email" value={email} onChange={(e) => setEmail(e.target.value)} autoComplete="username" required />
      </label>
      <label>Password
        <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} autoComplete="current-password" required />
      </label>
      {error && <p className="err">{error}</p>}
      <button type="submit" disabled={busy}>{busy ? "Signing in…" : "Sign in"}</button>
    </form>
  );
}
