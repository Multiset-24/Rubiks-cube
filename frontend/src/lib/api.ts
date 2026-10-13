import type { Scheme } from './cube/colors';
import { load, save } from './storage';

// ---------------------------------------------------------------- types

export type Method = 'beginner' | 'kociemba' | 'optimal';

export interface Quiz {
  question: string;
  options: string[];
  answer: number;
  explanation: string;
}

export interface Step {
  title: string;
  observe: string;
  instruction: string;
  tip: string;
  setup: string[];
  moves: string[];
  focus: number[];
  algorithm?: { name: string; notation: string; repeat: number };
  quiz?: Quiz;
}

export interface Stage {
  id: string;
  name: string;
  goal: string;
  recognize: string;
  algorithms: { name: string; notation: string }[];
  steps: Step[];
  skippedNote?: string;
}

export interface Solution {
  method: Method;
  solver: string;
  facelets: string;
  moves: string;
  moveList: string[];
  moveCount: number;
  stages: Stage[];
  optimal: boolean;
  cached: boolean;
  elapsedMs: number;
  fallbackFrom?: string;
  fallbackReason?: string;
}

export interface ValidationIssue {
  code: string;
  message: string;
  hint: string;
  facelets: number[];
}

export interface Validation {
  valid: boolean;
  issues: ValidationIssue[];
  facelets?: string;
}

export interface User {
  id: number;
  email: string;
  displayName: string;
  createdAt: string;
}

export type RunKind = 'tutor' | 'fast' | 'optimal' | 'timer';

export interface RunInput {
  kind: RunKind;
  method?: string;
  facelets?: string;
  scramble?: string;
  moveCount?: number;
  durationMs?: number;
  completed?: boolean;
  quizCorrect?: number;
  quizTotal?: number;
  stepsCompleted?: number;
  totalSteps?: number;
  clientId?: string;
  createdAtMs?: number;
}

export interface RunRecord extends Required<Omit<RunInput, 'createdAtMs'>> {
  id: number;
  createdAt: string;
}

export interface Stats {
  totalRuns: number;
  completedRuns: number;
  byKind: Partial<Record<RunKind, { total: number; completed: number }>>;
  movesFollowed: number;
  quiz: { correct: number; total: number; accuracy: number | null };
  timer: { bestMs: number | null; ao5Ms: number | null; ao12Ms: number | null; recentMs: number[] };
  streak: { current: number; longest: number };
  activeDays: string[];
}

export interface LeaderboardEntry {
  rank: number;
  displayName: string;
  bestMs: number;
  solves: number;
}

export class ApiError extends Error {
  constructor(
    public status: number,
    public code: string,
    message: string,
    public details?: unknown,
    public requestId?: string,
  ) {
    super(message);
  }
}

// ---------------------------------------------------------------- base URL

const API_KEY = 'zen-cube:api';

function resolveBase(): string {
  // ?api=https://... lets you point a deployed frontend at another backend.
  try {
    const fromQuery = new URLSearchParams(window.location.search).get('api');
    if (fromQuery) save(API_KEY, fromQuery);
  } catch {
    /* not in a browser */
  }
  const stored = load<string | null>(API_KEY, null);
  const base = stored || (import.meta.env.VITE_API_URL as string | undefined) || 'http://localhost:8080';
  return base.replace(/\/+$/, '');
}

export const API_BASE = resolveBase();

// ---------------------------------------------------------------- server status

// The free Render instance sleeps after 15 idle minutes and takes ~a minute to
// wake. The UI shows this status instead of a mysterious spinner.
export type ServerStatus = 'unknown' | 'waking' | 'online' | 'offline';
let status: ServerStatus = 'unknown';
const listeners = new Set<(s: ServerStatus) => void>();

function setStatus(s: ServerStatus) {
  if (s === status) return;
  status = s;
  listeners.forEach((l) => l(s));
}

export function serverStatus(): ServerStatus {
  return status;
}

export function onServerStatus(l: (s: ServerStatus) => void): () => void {
  listeners.add(l);
  return () => listeners.delete(l);
}

// ---------------------------------------------------------------- transport

interface RequestOptions {
  method?: 'GET' | 'POST' | 'DELETE';
  body?: unknown;
  token?: string | null;
  timeoutMs?: number;
  retries?: number;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function request<T>(path: string, opts: RequestOptions = {}): Promise<T> {
  const { method = 'GET', body, token, timeoutMs = 70000, retries = 2 } = opts;
  for (let attempt = 0; ; attempt++) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    // Requests that take long are almost always a sleeping server waking up.
    const slow = setTimeout(() => setStatus('waking'), 3500);
    try {
      const res = await fetch(API_BASE + path, {
        method,
        headers: {
          ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
        body: body !== undefined ? JSON.stringify(body) : undefined,
        signal: controller.signal,
      });
      setStatus('online');
      if (res.status === 204) return undefined as T;
      const data = await res.json().catch(() => ({}));
      if (res.ok) return data as T;
      const err = data?.error ?? {};
      const apiErr = new ApiError(res.status, err.code ?? 'HTTP_ERROR', err.message ?? res.statusText, err.details,
        err.requestId);
      // Retry only what is safe and likely to succeed: overload and warm-up.
      const retryAfter = Number(res.headers.get('Retry-After') ?? '0');
      if ((res.status === 503 || res.status === 429) && attempt < retries && retryAfter <= 10) {
        await sleep(Math.max(1, retryAfter) * 1000);
        continue;
      }
      throw apiErr;
    } catch (e) {
      if (e instanceof ApiError) throw e;
      if (attempt < retries) {
        setStatus('waking');
        await sleep(1500 * (attempt + 1));
        continue;
      }
      setStatus('offline');
      throw new ApiError(0, 'NETWORK', 'Could not reach the server. Check your connection and try again.');
    } finally {
      clearTimeout(timer);
      clearTimeout(slow);
    }
  }
}

// ---------------------------------------------------------------- endpoints

export const api = {
  // Fire-and-forget ping on page load, so a sleeping server starts waking up
  // while the user is still scanning their cube.
  warmUp: () => request<{ status: string }>('/healthz', { retries: 6, timeoutMs: 90000 }).catch(() => undefined),

  validate: (cube: string, colorNames?: Partial<Scheme>) =>
    request<Validation>('/api/v1/validate', { method: 'POST', body: { cube, colorNames } }),

  solve: (cube: string, method: Method, colorNames?: Partial<Scheme>) =>
    request<Solution>('/api/v1/solve', { method: 'POST', body: { cube, method, colorNames } }),

  scramble: () => request<{ scramble: string; moveList: string[]; facelets: string }>('/api/v1/scramble', { retries: 0, timeoutMs: 8000 }),

  register: (email: string, password: string, displayName: string) =>
    request<{ token: string; user: User }>('/api/v1/auth/register', {
      method: 'POST',
      body: { email, password, displayName },
    }),

  login: (email: string, password: string) =>
    request<{ token: string; user: User }>('/api/v1/auth/login', { method: 'POST', body: { email, password } }),

  me: (token: string) => request<{ user: User }>('/api/v1/me', { token }),

  createRun: (token: string, run: RunInput) => request<RunRecord>('/api/v1/runs', { method: 'POST', token, body: run }),

  importRuns: (token: string, runs: RunInput[]) =>
    request<{ runs: RunRecord[]; imported: number }>('/api/v1/runs/batch', { method: 'POST', token, body: { runs } }),

  listRuns: (token: string, cursor?: string | null, limit = 20) =>
    request<{ runs: RunRecord[]; nextCursor: string | null }>(
      `/api/v1/runs?limit=${limit}${cursor ? `&cursor=${encodeURIComponent(cursor)}` : ''}`,
      { token },
    ),

  deleteRun: (token: string, id: number) => request<void>(`/api/v1/runs/${id}`, { method: 'DELETE', token }),

  stats: (token: string) => request<Stats>('/api/v1/stats', { token }),

  leaderboard: (limit = 20) => request<{ entries: LeaderboardEntry[] }>(`/api/v1/leaderboard?limit=${limit}`),
};
