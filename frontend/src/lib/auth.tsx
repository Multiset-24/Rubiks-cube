import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
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

  const persist = useCallback((s: AuthState) => {
    setState(s);
    save(KEY, s.token ? s : null);
  }, []);

  const afterSignIn = useCallback(async (token: string, user: User) => {
    persist({ token, user });
    // Runs recorded while signed out move to the account (idempotent by clientId).
    setSyncedRuns(await syncGuestRuns(token));
  }, [persist]);

  // Validate a stored session once on load.
  useEffect(() => {
    if (!state.token) return;
    api.me(state.token).then(
      ({ user }) => persist({ token: state.token, user }),
      (e) => {
        if (e instanceof ApiError && e.status === 401) persist({ token: null, user: null });
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
