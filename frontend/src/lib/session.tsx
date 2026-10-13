import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import { DEFAULT_SCHEME, type Scheme } from './cube/colors';
import { load, safeStore, save } from './storage';

// The cube currently being worked on, kept per browser tab.
export interface CubeSession {
  facelets: string;
  scheme: Scheme;
  source: 'scan' | 'manual' | 'scramble';
  scramble?: string;
  unsure?: number[]; // stickers the scanner was not confident about
}

interface SessionContextValue {
  cube: CubeSession | null;
  setCube: (c: CubeSession | null) => void;
}

const KEY = 'zen-cube:cube';
const SessionContext = createContext<SessionContextValue | null>(null);

export function SessionProvider({ children }: { children: ReactNode }) {
  const [cube, setCubeState] = useState<CubeSession | null>(() => load(KEY, null, safeStore('session')));
  const setCube = useCallback((c: CubeSession | null) => {
    setCubeState(c);
    save(KEY, c, safeStore('session'));
  }, []);
  const value = useMemo(() => ({ cube, setCube }), [cube, setCube]);
  return <SessionContext.Provider value={value}>{children}</SessionContext.Provider>;
}

export function useCubeSession(): SessionContextValue {
  const ctx = useContext(SessionContext);
  if (!ctx) throw new Error('useCubeSession outside SessionProvider');
  return ctx;
}

export const defaultScheme = DEFAULT_SCHEME;
