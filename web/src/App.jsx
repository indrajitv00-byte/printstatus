import { useEffect, useState } from "react";
import { onAuthStateChanged, signOut } from "firebase/auth";
import { auth } from "./firebase";
import Login from "./Login";
import Dashboard from "./Dashboard";

export default function App() {
  const [user, setUser] = useState(undefined); // undefined = still checking
  useEffect(() => onAuthStateChanged(auth, setUser), []);

  if (user === undefined) return <p className="center">Loading…</p>;
  if (!user) return <Login />;
  return (
    <main>
      <header>
        <div>
          <h1>Print status</h1>
          <p className="sub">Signed in as {user.email}</p>
        </div>
        <button className="ghost" onClick={() => signOut(auth)}>Sign out</button>
      </header>
      <Dashboard />
    </main>
  );
}
