import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { api, ApiError, type User } from './api';
import { syncGuestRuns } from './runs';
import { load, save } from './storage';

interface AuthState {
  token: string | null;
  user: User | null;
}

interface AuthContextValue extends AuthState {
  login: (email: string, password: string) => Promise<void>;
  register: (email: string, password: string, displayName: string) => Promise<void>;
  logout: () => void;
  syncedRuns: number;
}

const KEY = 'zen-cube:auth';
const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<AuthState>(() => load(KEY, { token: null, user: null }));
  const [syncedRuns, setSyncedRuns] = useState(0);

  // The current token, for async work that must not act on a session the
  // user has since left.
  const tokenRef = useRef(state.token);

  const persist = useCallback((s: AuthState) => {
    tokenRef.current = s.token;
    setState(s);
    save(KEY, s.token ? s : null);
  }, []);

  const afterSignIn = useCallback(async (token: string, user: User) => {
    persist({ token, user });
    // Runs recorded while signed out move to the account (idempotent by clientId).
    setSyncedRuns(await syncGuestRuns(token));
  }, [persist]);

  // Validate a stored session once on load, then upload any runs that were
  // kept on this device while the server was unreachable.
  useEffect(() => {
    const token = state.token;
    if (!token) return;
    api.me(token).then(
      async ({ user }) => {
        if (tokenRef.current !== token) return;
        persist({ token, user });
        const synced = await syncGuestRuns(token);
        if (tokenRef.current === token) setSyncedRuns(synced);
      },
      (e) => {
        if (e instanceof ApiError && e.status === 401 && tokenRef.current === token) persist({ token: null, user: null });
      },
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const value = useMemo<AuthContextValue>(() => ({
    ...state,
    syncedRuns,
    login: async (email, password) => {
      const r = await api.login(email, password);
      await afterSignIn(r.token, r.user);
    },
    register: async (email, password, displayName) => {
      const r = await api.register(email, password, displayName);
      await afterSignIn(r.token, r.user);
    },
    logout: () => persist({ token: null, user: null }),
  }), [state, syncedRuns, afterSignIn, persist]);

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth outside AuthProvider');
  return ctx;
}
