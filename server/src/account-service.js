const crypto = require('crypto');
const {
  client_ip,
  digest,
  hash_password,
  normalize_username,
  random_token,
  validate_password,
  verify_password
} = require('./lib/security');

const dummy_hash_promise = hash_password(random_token(24));

function now_unix() {
  return Math.floor(Date.now() / 1000);
}

function public_user(user) {
  return {
    id: user.id,
    username: user.username,
    balance: user.balance,
    created_at: user.created_at
  };
}

function purge_sessions(db, now) {
  for (const [hash, session] of Object.entries(db.sessions)) {
    if (session.expires_at <= now) {
      delete db.sessions[hash];
    }
  }
}

function issue_session(db, user, ttl_seconds) {
  const token = random_token(32);
  const now = now_unix();

  purge_sessions(db, now);
  db.sessions[digest(token, 'session:v1')] = {
    user_id: user.id,
    created_at: now,
    expires_at: now + ttl_seconds
  };

  return { token, expires_at: now + ttl_seconds };
}

function bearer_token(req) {
  const header = String(req.get('authorization') || '');
  const match = /^Bearer ([A-Za-z0-9_-]{20,128})$/.exec(header);

  return match ? match[1] : null;
}

function resolve_session(db, token) {
  if (!token) {
    return null;
  }

  const hash = digest(token, 'session:v1');
  const session = db.sessions[hash];

  if (!session || session.expires_at <= now_unix()) {
    return null;
  }

  const user = db.users[session.user_id];

  return user ? { hash, session, user } : null;
}

function mount_account_routes(router, { store, limiter, starting_balance, session_seconds }) {
  function limited(req, res, scope, maximum, window_ms) {
    const result = limiter(`${scope}:${client_ip(req)}`, maximum, window_ms);

    if (!result.allowed) {
      res.set('Retry-After', String(result.retry_after));
      res.status(429).json({ success: false, error: 'Too many requests' });
      return true;
    }

    return false;
  }

  router.post('/register', async (req, res) => {
    if (limited(req, res, 'register', 10, 60 * 60 * 1000)) {
      return;
    }

    const username = normalize_username(req.body?.username);
    const password = req.body?.password;

    if (!username) {
      return res.status(400).json({ success: false, error: 'username must be 3-24 letters, digits, _ . or -' });
    }

    if (!validate_password(password)) {
      return res.status(400).json({ success: false, error: 'password must be 10-128 characters with a letter and a non-letter' });
    }

    const password_hash = await hash_password(password);

    try {
      const result = store.transaction((db) => {
        const key = username.toLowerCase();

        if (db.usernames[key]) {
          return null;
        }

        const user = {
          id: crypto.randomUUID(),
          username,
          password_hash,
          balance: starting_balance,
          casino: {},
          created_at: now_unix(),
          updated_at: now_unix()
        };

        db.users[user.id] = user;
        db.usernames[key] = user.id;
        db.coin_ledger.push({
          id: crypto.randomUUID(),
          user_id: user.id,
          delta: starting_balance,
          previous: 0,
          balance: starting_balance,
          reason: 'starting balance',
          round_id: null,
          game: null,
          created_at: now_unix()
        });

        return { user: public_user(user), session: issue_session(db, user, session_seconds) };
      });

      if (!result) {
        return res.status(409).json({ success: false, error: 'Username is already taken' });
      }

      return res.status(201).json({ success: true, ...result });
    } catch (error) {
      console.error('[ACCOUNT] Register failed:', error.message);
      return res.status(500).json({ success: false, error: 'Internal error' });
    }
  });

  router.post('/login', async (req, res) => {
    if (limited(req, res, 'login', 20, 15 * 60 * 1000)) {
      return;
    }

    const username = normalize_username(req.body?.username);
    const password = typeof req.body?.password === 'string' ? req.body.password : '';
    const db = store.read();
    const user_id = username ? db.usernames[username.toLowerCase()] : null;
    const user = user_id ? db.users[user_id] : null;
    const valid = await verify_password(password, user ? user.password_hash : await dummy_hash_promise);

    if (!user || !valid) {
      return res.status(401).json({ success: false, error: 'Invalid username or password' });
    }

    try {
      const session = store.transaction((current) => {
        const target = current.users[user.id];
        return target ? issue_session(current, target, session_seconds) : null;
      });

      if (!session) {
        return res.status(401).json({ success: false, error: 'Invalid username or password' });
      }

      return res.json({ success: true, user: public_user(user), session });
    } catch (error) {
      console.error('[ACCOUNT] Login failed:', error.message);
      return res.status(500).json({ success: false, error: 'Internal error' });
    }
  });

  router.post('/logout', (req, res) => {
    const token = bearer_token(req);

    store.transaction((db) => {
      const resolved = resolve_session(db, token);

      if (resolved) {
        delete db.sessions[resolved.hash];
      }
    });

    res.json({ success: true });
  });

  router.get('/me', (req, res) => {
    const resolved = resolve_session(store.read(), bearer_token(req));

    if (!resolved) {
      return res.status(401).json({ success: false, error: 'Authentication required' });
    }

    return res.json({ success: true, user: public_user(resolved.user) });
  });
}

module.exports = {
  bearer_token,
  mount_account_routes,
  resolve_session
};
