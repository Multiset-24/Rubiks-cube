import { NavLink } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import { ServerStatus } from './ServerStatus';

export function Nav() {
  const { user } = useAuth();
  const link = ({ isActive }: { isActive: boolean }) => `nav__link ${isActive ? 'is-active' : ''}`;
  return (
    <header className="nav">
      <NavLink to="/" className="nav__brand" aria-label="Zen Cube home">
        <img src="/favicon.svg" alt="" width={26} height={26} />
        <span>zen cube</span>
      </NavLink>
      <nav className="nav__links" aria-label="Main">
        <NavLink to="/scan" className={link}>Solve</NavLink>
        <NavLink to="/learn" className={link}>Learn</NavLink>
        <NavLink to="/timer" className={link}>Timer</NavLink>
        <NavLink to="/history" className={link}>Progress</NavLink>
        <NavLink to="/leaderboard" className={link}>Leaders</NavLink>
        <NavLink to="/account" className={link}>{user ? user.displayName : 'Sign in'}</NavLink>
      </nav>
      <ServerStatus />
    </header>
  );
}
