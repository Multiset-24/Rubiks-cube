import { Link } from 'react-router-dom';

export default function NotFound() {
  return (
    <div className="empty card">
      <h2>Nothing here</h2>
      <p>This page drifted away. Breathe, and head back home.</p>
      <Link className="btn btn--primary" to="/">Home</Link>
    </div>
  );
}
