import { Link } from 'react-router-dom';

export default function NotFound() {
  return (
    <div className="empty card">
      <h2>Page not found</h2>
      <Link className="btn btn--primary" to="/">Home</Link>
    </div>
  );
}
