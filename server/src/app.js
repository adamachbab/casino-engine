const express = require('express');
const { create_rate_limiter } = require('./lib/security');
const { bearer_token, mount_account_routes, resolve_session } = require('./account-service');
const { mount_casino_routes } = require('./casino-service');

function create_app(options) {
  const {
    store,
    starting_balance = 10000,
    daily_bonus = 1000,
    session_seconds = 7 * 24 * 60 * 60,
    action_limit = { window_ms: 60_000, max: 120 }
  } = options;

  const app = express();
  const limiter = create_rate_limiter();

  app.disable('x-powered-by');
  app.use(express.json({ limit: '16kb' }));
  app.use((req, res, next) => {
    res.set('X-Content-Type-Options', 'nosniff');
    res.set('Cache-Control', 'no-store');
    next();
  });

  function authenticate(req, res, next) {
    const resolved = resolve_session(store.read(), bearer_token(req));

    if (!resolved) {
      return res.status(401).json({ success: false, error: 'Authentication required' });
    }

    req.user_id = resolved.user.id;
    return next();
  }

  function limit_actions(req, res, next) {
    const result = limiter(`casino:${req.user_id}`, action_limit.max, action_limit.window_ms);

    if (!result.allowed) {
      res.set('Retry-After', String(result.retry_after));
      return res.status(429).json({ success: false, error: 'Too many casino actions' });
    }

    return next();
  }

  function send_mutation(req, res, callback) {
    try {
      const payload = store.transaction((db) => {
        const user = db.users[req.user_id];

        if (!user) {
          const error = new Error('Authentication required');
          error.status = 401;
          throw error;
        }

        return callback({ db, user });
      });

      return res.json(payload);
    } catch (error) {
      if (error.status === 401 || error.status === 409) {
        return res.status(error.status).json({ success: false, error: error.message });
      }

      console.error('[CASINO] Request failed:', error.message);
      return res.status(500).json({ success: false, error: 'Internal error' });
    }
  }

  const auth = express.Router();
  mount_account_routes(auth, { store, limiter, starting_balance, session_seconds });
  app.use('/v1/auth', auth);

  const casino = express.Router();
  casino.use(authenticate);
  mount_casino_routes(casino, { limit_actions, send_mutation, daily_bonus });
  app.use('/v1/casino', casino);

  app.get('/health', (req, res) => {
    res.json({ success: true, status: 'ok' });
  });

  app.use((req, res) => {
    res.status(404).json({ success: false, error: 'Not found' });
  });

  app.use((error, req, res, next) => {
    if (error.type === 'entity.parse.failed') {
      return res.status(400).json({ success: false, error: 'Invalid JSON body' });
    }

    console.error('[HTTP] Unhandled error:', error.message);
    return res.status(500).json({ success: false, error: 'Internal error' });
  });

  return app;
}

module.exports = {
  create_app
};
