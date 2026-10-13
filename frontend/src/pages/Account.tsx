import { useState, type FormEvent } from 'react';
import { Link } from 'react-router-dom';
import { ApiError } from '../lib/api';
import { useAuth } from '../lib/auth';
import { localRuns } from '../lib/runs';

export default function Account() {
  const { user, logout, syncedRuns } = useAuth();
  if (user) {
    return (
      <div className="account">
        <div className="card account__card">
          <p className="eyebrow">Signed in</p>
          <h1>{user.displayName}</h1>
          <p className="muted">{user.email} · member since {new Date(user.createdAt).toLocaleDateString()}</p>
          {syncedRuns > 0 && <p className="ok">Moved {syncedRuns} {syncedRuns === 1 ? 'run' : 'runs'} from this device into your account.</p>}
          <div className="row">
            <Link to="/history" className="btn btn--primary">See progress</Link>
            <button type="button" className="btn btn--ghost" onClick={logout}>Sign out</button>
          </div>
        </div>
      </div>
    );
  }
  return <AuthForm />;
}

function AuthForm() {
  const { login, register } = useAuth();
  const [mode, setMode] = useState<'login' | 'register'>('register');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [name, setName] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [fields, setFields] = useState<Record<string, string>>({});
  const pending = localRuns().length;

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    setFields({});
    try {
      if (mode === 'login') await login(email, password);
      else await register(email, password, name);
    } catch (err) {
      if (err instanceof ApiError) {
        setError(err.message);
        const f = (err.details as { fields?: Record<string, string> } | undefined)?.fields;
        if (f) setFields(f);
      } else {
        setError('Something went wrong. Please try again.');
      }
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="account">
      <form className="card account__card" onSubmit={submit} noValidate>
        <div className="tabs" role="tablist">
          <button type="button" role="tab" aria-selected={mode === 'register'} className={mode === 'register' ? 'is-active' : ''} onClick={() => setMode('register')}>Create account</button>
          <button type="button" role="tab" aria-selected={mode === 'login'} className={mode === 'login' ? 'is-active' : ''} onClick={() => setMode('login')}>Sign in</button>
        </div>
        <p className="muted">
          An account keeps your solves, times, streaks and quiz scores across devices.
          {pending > 0 && ` The ${pending} run${pending === 1 ? '' : 's'} on this device will move to your account.`}
        </p>
        {mode === 'register' && (
          <label className="field">
            <span>Display name</span>
            <input value={name} onChange={(e) => setName(e.target.value)} autoComplete="nickname" required minLength={2} maxLength={32} />
            {fields.displayName && <em className="field__error">{fields.displayName}</em>}
          </label>
        )}
        <label className="field">
          <span>Email</span>
          <input type="email" value={email} onChange={(e) => setEmail(e.target.value)} autoComplete="email" required />
          {fields.email && <em className="field__error">{fields.email}</em>}
        </label>
        <label className="field">
          <span>Password</span>
          <input type="password" value={password} onChange={(e) => setPassword(e.target.value)}
            autoComplete={mode === 'login' ? 'current-password' : 'new-password'} required minLength={8} />
          {fields.password && <em className="field__error">{fields.password}</em>}
        </label>
        {error && <p className="error">{error}</p>}
        <button type="submit" className="btn btn--primary btn--lg btn--block" disabled={busy}>
          {busy ? 'One moment…' : mode === 'login' ? 'Sign in' : 'Create account'}
        </button>
        <p className="muted small">No account needed to solve: everything works as a guest.</p>
      </form>
    </div>
  );
}
