const assert = require('assert/strict');
const crypto = require('crypto');
const http = require('http');
const test = require('node:test');
const { create_app } = require('../src/app');
const { create_memory_store } = require('../src/lib/data-store');
const { commitment, fair_int } = require('../src/lib/fairness');
const { mines_multiplier } = require('../src/casino-service');

const client_seed = 'client-seed-for-tests';

function request(port, method, path, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body === undefined ? '' : JSON.stringify(body);
    const headers = {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload)
    };

    if (token) {
      headers.Authorization = `Bearer ${token}`;
    }

    const req = http.request({ hostname: '127.0.0.1', port, method, path, headers }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => resolve({ status: res.statusCode, body: JSON.parse(Buffer.concat(chunks).toString('utf8')) }));
    });

    req.on('error', reject);
    req.end(payload);
  });
}

function id(label) {
  return `casino-test-${label}-request`;
}

async function start(store) {
  const app = create_app({ store, starting_balance: 50000 });
  const server = await new Promise((resolve) => {
    const value = app.listen(0, '127.0.0.1', () => resolve(value));
  });

  return server;
}

test('fair integer mapping is deterministic and bounded', () => {
  const seed = '11'.repeat(32);
  const values = Array.from({ length: 100 }, (_, cursor) => fair_int(seed, client_seed, 7, 'roulette', 37, cursor));

  assert.ok(values.every((value) => Number.isInteger(value) && value >= 0 && value < 37));
  assert.deepEqual(values, Array.from({ length: 100 }, (_, cursor) => fair_int(seed, client_seed, 7, 'roulette', 37, cursor)));
  assert.equal(commitment(seed), crypto.createHash('sha256').update(seed).digest('hex'));
});

test('mines payout uses the published 1% house factor', () => {
  assert.equal(mines_multiplier(3, 0), 0.99);
  assert.equal(mines_multiplier(3, 1), 1.12);
  assert.ok(mines_multiplier(8, 4) > mines_multiplier(3, 4));
});

test('accounts and casino rounds are server authoritative and provably fair', async () => {
  const store = create_memory_store();
  const server = await start(store);
  const port = server.address().port;

  try {
    let result = await request(port, 'POST', '/v1/auth/register', { username: 'player_one', password: 'correct-horse-1' });
    assert.equal(result.status, 201);
    const token = result.body.session.token;

    result = await request(port, 'POST', '/v1/auth/register', { username: 'PLAYER_ONE', password: 'correct-horse-1' });
    assert.equal(result.status, 409);

    result = await request(port, 'POST', '/v1/auth/login', { username: 'player_one', password: 'wrong-password-1' });
    assert.equal(result.status, 401);

    result = await request(port, 'GET', '/v1/casino/state');
    assert.equal(result.status, 401);

    result = await request(port, 'GET', '/v1/casino/state', undefined, token);
    assert.equal(result.status, 200);
    assert.equal(result.body.balance, 50000);
    const committed = result.body.fairness.commitment;

    const roulette = {
      bets: [
        { type: 'red', amount: 50 },
        { type: 'straight', value: 17, amount: 10 },
        { type: 'dozen', value: 2, amount: 40 }
      ],
      client_seed,
      request_id: id('roulette')
    };
    result = await request(port, 'POST', '/v1/casino/roulette', roulette, token);
    assert.equal(result.status, 200);
    assert.equal(commitment(result.body.game.proof.server_seed), committed);
    assert.equal(result.body.game.number, fair_int(result.body.game.proof.server_seed, client_seed, result.body.game.proof.nonce, 'roulette', 37));
    const replayed = await request(port, 'POST', '/v1/casino/roulette', roulette, token);
    assert.deepEqual(replayed.body, result.body);

    result = await request(port, 'POST', '/v1/casino/mines/start', { bet: 250, mines: 5, client_seed, request_id: id('mines-start') }, token);
    assert.equal(result.status, 200);
    assert.equal(result.body.game.proof.server_seed, null);
    assert.equal(result.body.game.mine_positions, null);

    const user_id = Object.keys(store.read().users)[0];
    const hidden = store.read().users[user_id].casino.active.mines;
    const balance = store.read().users[user_id].balance;

    result = await request(port, 'POST', '/v1/casino/chicken/start', { bet: 100, risk: 'safe', client_seed, request_id: id('chicken-concurrent') }, token);
    assert.equal(result.status, 409);
    assert.equal(store.read().users[user_id].balance, balance);

    const safe_tile = Array.from({ length: 25 }, (_, index) => index).find((tile) => !hidden.mine_positions.includes(tile));
    result = await request(port, 'POST', '/v1/casino/mines/reveal', { tile: safe_tile, request_id: id('mines-reveal') }, token);
    assert.equal(result.body.game.status, 'playing');
    result = await request(port, 'POST', '/v1/casino/mines/cashout', { request_id: id('mines-cashout') }, token);
    assert.equal(result.body.game.status, 'cashed_out');
    assert.equal(result.body.game.payout, Math.floor(250 * mines_multiplier(5, 1)));

    result = await request(port, 'POST', '/v1/casino/crash/start', { bet: 100, client_seed, request_id: id('crash') }, token);
    assert.equal(result.status, 200);
    assert.equal(result.body.game.crash_point, null);
    result = await request(port, 'POST', '/v1/casino/crash/cashout', { request_id: id('crash-cashout') }, token);
    assert.equal(result.status, 200);
    assert.ok(['cashed_out', 'lost'].includes(result.body.game.status));
    assert.ok(result.body.game.crash_point >= 1);

    result = await request(port, 'POST', '/v1/casino/crate', { bet: 15, client_seed, request_id: id('crate') }, token);
    assert.equal(result.status, 404);

    result = await request(port, 'POST', '/v1/casino/blackjack/start', { bet: 100, client_seed, request_id: id('blackjack-start') }, token);
    assert.equal(result.status, 200);
    if (result.body.game.phase === 'insurance' && result.body.game.status === 'playing') {
      result = await request(port, 'POST', '/v1/casino/blackjack/action', { action: 'decline_insurance', request_id: id('blackjack-insurance') }, token);
      assert.equal(result.status, 200);
    }
    if (result.body.game.status === 'playing') {
      assert.equal(result.body.game.dealer_cards[1], '??');
      result = await request(port, 'POST', '/v1/casino/blackjack/action', { action: 'stand', request_id: id('blackjack-stand') }, token);
      assert.notEqual(result.body.game.status, 'playing');
    }

    result = await request(port, 'POST', '/v1/casino/roulette', { bets: [{ type: 'red', amount: 10_000_000 }], client_seed, request_id: id('too-big') }, token);
    assert.equal(result.status, 400);

    result = await request(port, 'POST', '/v1/casino/daily', {}, token);
    assert.equal(result.status, 200);
    result = await request(port, 'POST', '/v1/casino/daily', {}, token);
    assert.equal(result.status, 409);

    const ledger = store.read().coin_ledger.filter((entry) => entry.user_id === user_id);
    const total = ledger.reduce((sum, entry) => sum + entry.delta, 0);
    assert.equal(total, store.read().users[user_id].balance);
  } finally {
    server.close();
  }
});
