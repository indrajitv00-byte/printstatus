import { useEffect, useState } from "react";
import { onAuthStateChanged, signOut } from "firebase/auth";
import { auth } from "./firebase";
import Login from "./Login";
import Dashboard from "./Dashboard";

const articles = [
  { category: "Monitoring", title: "A calmer way to watch every print", text: "See the state, progress, file, and remaining time for each printer from one clear dashboard.", time: "4 min read" },
  { category: "Operations", title: "What to check when a printer goes offline", text: "A practical checklist for network, agent, and printer-side issues before a job falls behind.", time: "6 min read" },
  { category: "Automation", title: "Why a shared live status matters", text: "Give the whole team the same view of the print floor without asking for constant updates.", time: "5 min read" },
];

function SiteContent() {
  return (
    <>
      <section className="intro" id="about">
        <p className="eyebrow">Print operations, made visible</p>
        <h2>Keep every print moving.</h2>
        <p className="lede">PrintStatus brings live printer health, progress, and timing into one simple place so teams can act before a small delay becomes a missed deadline.</p>
        <a className="text-link" href="#what-we-do">See what we do <span aria-hidden="true">-&gt;</span></a>
      </section>

      <section className="content-section" id="what-we-do">
        <div className="section-heading">
          <div>
            <p className="eyebrow">What we do</p>
            <h2>Less guessing. More making.</h2>
          </div>
          <p>One shared source of truth for workshops, studios, labs, and anyone running more than one printer.</p>
        </div>
        <div className="feature-grid">
          <article className="feature-card"><span className="feature-number">01</span><h3>Track live status</h3><p>Know what is printing, paused, finished, idle, or offline without walking the floor.</p></article>
          <article className="feature-card"><span className="feature-number">02</span><h3>Plan the next job</h3><p>Use progress and remaining time to coordinate handoffs and make better production decisions.</p></article>
          <article className="feature-card"><span className="feature-number">03</span><h3>Stay ahead of issues</h3><p>See stale updates and printer errors early, while there is still time to respond.</p></article>
        </div>
      </section>

      <section className="content-section blog-section" id="blog">
        <div className="section-heading">
          <div><p className="eyebrow">From the blog</p><h2>Useful ideas for better output.</h2></div>
          <a className="text-link" href="#blog">Read all posts <span aria-hidden="true">-&gt;</span></a>
        </div>
        <div className="article-grid">
          {articles.map((article) => (
            <article className="article-card" key={article.title}>
              <p className="article-category">{article.category}</p>
              <h3>{article.title}</h3>
              <p>{article.text}</p>
              <span className="article-time">{article.time}</span>
            </article>
          ))}
        </div>
      </section>
    </>
  );
}

function Footer() {
  return (
    <footer className="site-footer">
      <div className="footer-main">
        <div><h2>Print<span>Status</span></h2><p>Live visibility for the work that keeps moving.</p></div>
        <nav className="footer-links" aria-label="Footer navigation">
          <a href="#what-we-do">What we do</a><a href="#blog">Blog</a><a href="#about">About</a>
        </nav>
      </div>
      <div className="footer-bottom"><span>PrintStatus</span><span>Built for focused print teams.</span></div>
    </footer>
  );
}

export default function App() {
  const [user, setUser] = useState(undefined); // undefined = still checking
  useEffect(() => onAuthStateChanged(auth, setUser), []);

  if (user === undefined) return <p className="center">Loading…</p>;
  if (!user) return <Login />;
  return (
    <main>
      <header>
        <div>
          <a className="brand" href="#about">Print<span>Status</span></a>
          <p className="sub">Signed in as {user.email}</p>
        </div>
        <div className="header-actions">
          <nav className="page-nav" aria-label="Page navigation"><a href="#what-we-do">What we do</a><a href="#blog">Blog</a><a href="#about">About</a></nav>
          <button className="ghost" onClick={() => signOut(auth)}>Sign out</button>
        </div>
      </header>
      <Dashboard />
      <SiteContent />
      <Footer />
    </main>
  );
}
