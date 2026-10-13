import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter } from 'react-router-dom';
import { App } from './App';
import { AuthProvider } from './lib/auth';
import { SessionProvider } from './lib/session';
import { api } from './lib/api';
import './styles/global.css';

// Start waking the (possibly sleeping) API immediately.
void api.warmUp();

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BrowserRouter>
      <AuthProvider>
        <SessionProvider>
          <App />
        </SessionProvider>
      </AuthProvider>
    </BrowserRouter>
  </StrictMode>,
);
