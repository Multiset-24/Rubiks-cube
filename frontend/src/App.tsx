import { lazy, Suspense } from 'react';
import { Route, Routes, useLocation } from 'react-router-dom';
import { Breath } from './components/Breath';
import { Nav } from './components/Nav';
import { Home } from './pages/Home';

// Route-level code splitting: three.js and the camera code load only when needed.
const Scan = lazy(() => import('./pages/Scan'));
const Review = lazy(() => import('./pages/Review'));
const Solve = lazy(() => import('./pages/Solve'));
const Timer = lazy(() => import('./pages/Timer'));
const History = lazy(() => import('./pages/History'));
const Leaderboard = lazy(() => import('./pages/Leaderboard'));
const Learn = lazy(() => import('./pages/Learn'));
const Account = lazy(() => import('./pages/Account'));
const NotFound = lazy(() => import('./pages/NotFound'));

export function App() {
  const { pathname } = useLocation();
  return (
    <div className={`app ${pathname.startsWith('/solve') ? 'app--solve' : ''}`}>
      <Nav />
      <main className="main">
        <Suspense fallback={<Breath />}>
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="/scan" element={<Scan />} />
            <Route path="/review" element={<Review />} />
            <Route path="/solve" element={<Solve />} />
            <Route path="/timer" element={<Timer />} />
            <Route path="/history" element={<History />} />
            <Route path="/leaderboard" element={<Leaderboard />} />
            <Route path="/learn" element={<Learn />} />
            <Route path="/account" element={<Account />} />
            <Route path="*" element={<NotFound />} />
          </Routes>
        </Suspense>
      </main>
    </div>
  );
}
