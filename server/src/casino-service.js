const crypto = require('crypto');
const { algorithm, commitment, fair_int, new_seed, shuffled_deck } = require('./lib/fairness');

const max_bet = 1_000_000;
const client_seed_pattern = /^[A-Za-z0-9._:-]{8,96}$/;
const request_id_pattern = /^[A-Za-z0-9_-]{12,96}$/;
const roulette_red = new Set([1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36]);
const roulette_payouts = Object.freeze({
  straight: 36,
  red: 2,
  black: 2,
  even: 2,
  odd: 2,
  low: 2,
  high: 2,
  dozen: 3,
  column: 3
});
const max_roulette_bets = 40;

function parse_roulette_bets(raw) {
  if (!Array.isArray(raw) || raw.length < 1 || raw.length > max_roulette_bets) {
    return { error: `bets must be an array of 1-${max_roulette_bets} entries` };
  }

  const bets = [];
  let total = 0;

  for (const entry of raw) {
    const type = typeof entry?.type === 'string' ? entry.type.toLowerCase() : '';
    const amount = entry?.amount;
    const value = entry?.value;

    if (!Object.prototype.hasOwnProperty.call(roulette_payouts, type)) {
      return { error: 'unknown roulette bet type' };
    }

    if (!Number.isSafeInteger(amount) || amount < 1 || amount > max_bet) {
      return { error: `amount must be an integer from 1 to ${max_bet}` };
    }

    if (type === 'straight' && !(Number.isSafeInteger(value) && value >= 0 && value <= 36)) {
      return { error: 'straight bets need a value from 0 to 36' };
    }

    if ((type === 'dozen' || type === 'column') && ![1, 2, 3].includes(value)) {
      return { error: 'dozen and column bets need a value of 1, 2, or 3' };
    }

    const needs_value = type === 'straight' || type === 'dozen' || type === 'column';

    bets.push({ type, value: needs_value ? value : null, amount });
    total += amount;
  }

  if (total > max_bet) {
    return { error: `total bet must not exceed ${max_bet}` };
  }

  return { bets, total };
}

function roulette_bet_wins(bet, number) {
  if (number === 0) {
    return bet.type === 'straight' && bet.value === 0;
  }

  switch (bet.type) {
    case 'straight':
      return bet.value === number;
    case 'red':
      return roulette_red.has(number);
    case 'black':
      return !roulette_red.has(number);
    case 'even':
      return number % 2 === 0;
    case 'odd':
      return number % 2 === 1;
    case 'low':
      return number <= 18;
    case 'high':
      return number >= 19;
    case 'dozen':
      return Math.ceil(number / 12) === bet.value;
    case 'column':
      return ((number - 1) % 3) + 1 === bet.value;
    default:
      return false;
  }
}
const mines_counts = new Set([3, 5, 8]);
const chicken_rules = Object.freeze({
  safe: Object.freeze({ survival: 8500, multiplier: 1.15 }),
  spicy: Object.freeze({ survival: 6500, multiplier: 1.5 }),
  wild: Object.freeze({ survival: 3500, multiplier: 2.75 })
});

function now_unix() {
  return Math.floor(Date.now() / 1000);
}

function conflict(message) {
  const error = new Error(message);
  error.status = 409;
  return error;
}

function ensure_casino(user) {
  if (!user.casino || typeof user.casino !== 'object' || Array.isArray(user.casino)) {
    user.casino = {};
  }

  const casino = user.casino;

  if (typeof casino.server_seed !== 'string' || !/^[a-f0-9]{64}$/.test(casino.server_seed)) {
    casino.server_seed = new_seed();
  }

  casino.commitment = commitment(casino.server_seed);

  if (!Number.isSafeInteger(casino.nonce) || casino.nonce < 0) {
    casino.nonce = 0;
  }

  if (!casino.active || typeof casino.active !== 'object' || Array.isArray(casino.active)) {
    casino.active = {};
  }

  if (!casino.requests || typeof casino.requests !== 'object' || Array.isArray(casino.requests)) {
    casino.requests = {};
  }

  if (!Array.isArray(casino.request_order)) {
    casino.request_order = [];
  }

  if (!Array.isArray(casino.history)) {
    casino.history = [];
  }

  return casino;
}

function public_fairness(user) {
  const casino = ensure_casino(user);

  return {
    algorithm,
    commitment: casino.commitment,
    nonce: casino.nonce,
    rules: {
      roulette: { pockets: 37, max_bets: max_roulette_bets, payouts: roulette_payouts },
      mines: { board_size: 25, mine_counts: [3, 5, 8], house_factor: 0.99 },
      blackjack: { decks: 1, dealer_stands_on: 17, blackjack_payout: 1.5 },
      chicken: chicken_rules,
      crash: { growth: 'exp(0.12 * seconds)', house_factor: 0.99, manual_cashout: true }
    }
  };
}

function hand_value(cards) {
  let total = 0;
  let aces = 0;

  for (const card of cards || []) {
    const rank = String(card).slice(0, -1);

    if (rank === 'A') {
      total += 11;
      aces += 1;
    } else if (['J', 'Q', 'K'].includes(rank)) {
      total += 10;
    } else {
      total += Number(rank) || 0;
    }
  }

  while (total > 21 && aces > 0) {
    total -= 10;
    aces -= 1;
  }

  return total;
}

function card_points(card) {
  const rank = String(card).slice(0, -1);

  if (rank === 'A') {
    return 11;
  }

  return ['J', 'Q', 'K'].includes(rank) ? 10 : Number(rank) || 0;
}

function can_double(user, hand) {
  return Boolean(hand) && !hand.done && hand.cards.length === 2 && user.balance >= hand.bet;
}

function can_split(user, round, hand) {
  return Boolean(hand) &&
    !hand.done &&
    hand.cards.length === 2 &&
    round.hands.length < 4 &&
    card_points(hand.cards[0]) === card_points(hand.cards[1]) &&
    user.balance >= hand.bet;
}

function charge(db, user, round, amount, reason) {
  if (user.balance < amount) {
    throw conflict('Insufficient balance');
  }

  const previous = user.balance;

  user.balance -= amount;
  round.bet += amount;
  record_coin(db, user, -amount, previous, reason, round);
}

function validate_common(body) {
  const bet = body?.bet;
  const client_seed = body?.client_seed;
  const request_id = body?.request_id;

  if (!Number.isSafeInteger(bet) || bet < 1 || bet > max_bet) {
    return { error: `bet must be an integer from 1 to ${max_bet}` };
  }

  if (typeof client_seed !== 'string' || !client_seed_pattern.test(client_seed)) {
    return { error: 'client_seed must be 8-96 safe characters' };
  }

  if (typeof request_id !== 'string' || !request_id_pattern.test(request_id)) {
    return { error: 'request_id must be 12-96 safe characters' };
  }

  return { bet, client_seed, request_id };
}

function require_request_id(body) {
  const request_id = body?.request_id;

  return typeof request_id === 'string' && request_id_pattern.test(request_id) ? request_id : null;
}

function record_coin(db, user, delta, previous, reason, round) {
  db.coin_ledger.push({
    id: crypto.randomUUID(),
    user_id: user.id,
    delta,
    previous,
    balance: user.balance,
    reason,
    round_id: round ? round.id : null,
    game: round ? round.game : null,
    created_at: now_unix()
  });
}

function begin_round(db, user, game, bet, client_seed) {
  const casino = ensure_casino(user);
  if (casino.active.crash) {
    crash_settle_if_due(db, user, casino.active.crash);
  }

  const active_game = Object.keys(casino.active)[0];

  if (active_game) {
    throw conflict(`Finish the active ${active_game} round first`);
  }

  if (user.balance < bet) {
    throw conflict('Insufficient balance');
  }

  const round = {
    id: crypto.randomUUID(),
    game,
    bet,
    client_seed,
    server_seed: casino.server_seed,
    commitment: casino.commitment,
    nonce: casino.nonce,
    started_at: now_unix(),
    status: 'playing'
  };

  casino.nonce += 1;
  casino.server_seed = new_seed();
  casino.commitment = commitment(casino.server_seed);
  casino.active[game] = round;

  const previous = user.balance;
  user.balance -= bet;
  record_coin(db, user, -bet, previous, `${game} wager`, round);

  return round;
}

function proof(user, round, reveal) {
  const casino = ensure_casino(user);

  return {
    algorithm,
    commitment: round.commitment,
    server_seed: reveal ? round.server_seed : null,
    client_seed: round.client_seed,
    nonce: round.nonce,
    next_commitment: casino.commitment
  };
}

function public_round(user, round, reveal = false) {
  const base = {
    id: round.id,
    game: round.game,
    bet: round.bet,
    status: round.status,
    payout: round.payout || 0,
    coin_change: (round.payout || 0) - round.bet,
    balance: user.balance,
    message: round.message || null,
    started_at: round.started_at,
    settled_at: round.settled_at || null,
    proof: proof(user, round, reveal)
  };

  if (round.game === 'mines') {
    Object.assign(base, {
      mines: round.mines,
      revealed: [...round.revealed],
      multiplier: round.multiplier,
      mine_positions: reveal ? [...round.mine_positions] : null
    });
  }

  if (round.game === 'roulette') {
    Object.assign(base, {
      number: round.number,
      color: round.color,
      bets: round.bets,
      results: round.results,
      won: round.won
    });
  }

  if (round.game === 'blackjack') {
    const show_dealer = reveal || round.status !== 'playing';
    const current = round.hands[Math.min(round.active_hand, round.hands.length - 1)];

    Object.assign(base, {
      phase: round.phase,
      hands: round.hands.map((hand) => ({
        cards: [...hand.cards],
        bet: hand.bet,
        total: hand_value(hand.cards),
        doubled: hand.doubled,
        result: hand.result
      })),
      active_hand: round.active_hand,
      insurance: round.insurance || 0,
      can_double: round.status === 'playing' && round.phase === 'playing' && can_double(user, current),
      can_split: round.status === 'playing' && round.phase === 'playing' && can_split(user, round, current),
      player_cards: [...current.cards],
      player_total: hand_value(current.cards),
      dealer_cards: show_dealer ? [...round.dealer_cards] : [round.dealer_cards[0], '??'],
      dealer_total: show_dealer ? hand_value(round.dealer_cards) : null
    });
  }

  if (round.game === 'chicken') {
    Object.assign(base, {
      risk: round.risk,
      crossed: round.crossed,
      multiplier: round.multiplier,
      caught: round.caught
    });
  }

  if (round.game === 'crash') {
    const live = round.status === 'playing';

    Object.assign(base, {
      crash_point: live ? null : round.crash_point,
      elapsed_ms: live ? Date.now() - round.started_ms : null,
      cashed_at: round.cashed_at || null,
      won: round.won
    });
  }

  return base;
}

function settle(db, user, round, status, payout, message) {
  const casino = ensure_casino(user);
  const previous = user.balance;

  round.status = status;
  round.payout = payout;
  round.message = message;
  round.settled_at = now_unix();
  user.balance += payout;

  if (payout) {
    record_coin(db, user, payout, previous, `${round.game} payout (${status})`, round);
  }

  delete casino.active[round.game];

  const history = public_round(user, round, true);
  casino.history.push(history);
  casino.history = casino.history.slice(-50);

  return history;
}

function remember(user, request_id, payload) {
  const casino = ensure_casino(user);

  casino.requests[request_id] = payload;
  casino.request_order.push(request_id);

  while (casino.request_order.length > 100) {
    delete casino.requests[casino.request_order.shift()];
  }

  return payload;
}

function replay(user, request_id) {
  const casino = ensure_casino(user);

  return Object.prototype.hasOwnProperty.call(casino.requests, request_id)
    ? casino.requests[request_id]
    : null;
}

function roulette_color(number) {
  if (number === 0) {
    return 'green';
  }

  return roulette_red.has(number) ? 'red' : 'black';
}

function mines_multiplier(mines, reveals) {
  let multiplier = 0.99;

  for (let step = 0; step < reveals; step += 1) {
    multiplier *= (25 - step) / (25 - mines - step);
  }

  return Math.floor(multiplier * 100) / 100;
}

function crash_point(roll) {
  const raw = Math.floor((0.99 / Math.max(Number.EPSILON, 1 - roll)) * 100) / 100;

  return Math.min(1000, Math.max(1, raw));
}

function blackjack_advance(db, user, round) {
  while (round.active_hand < round.hands.length && round.hands[round.active_hand].done) {
    round.active_hand += 1;
  }

  if (round.active_hand < round.hands.length) {
    const total = hand_value(round.hands[round.active_hand].cards);

    round.message = round.hands.length > 1
      ? `Hand ${round.active_hand + 1}: ${total}`
      : `${total}, hit or stand`;

    return public_round(user, round, false);
  }

  if (round.hands.some((hand) => hand_value(hand.cards) <= 21)) {
    while (hand_value(round.dealer_cards) < 17) {
      round.dealer_cards.push(round.deck.pop());
    }
  }

  const dealer_total = hand_value(round.dealer_cards);
  let payout = round.insurance_payout || 0;

  for (const hand of round.hands) {
    const total = hand_value(hand.cards);

    if (total > 21) {
      hand.result = 'bust';
    } else if (dealer_total > 21 || total > dealer_total) {
      hand.result = 'won';
      payout += hand.bet * 2;
    } else if (total === dealer_total) {
      hand.result = 'push';
      payout += hand.bet;
    } else {
      hand.result = 'lost';
    }
  }

  const status = payout > round.bet ? 'won' : (payout === round.bet ? 'push' : 'lost');
  const message = dealer_total > 21 ? `Dealer busts with ${dealer_total}` : `Dealer ${dealer_total}`;

  return settle(db, user, round, status, payout, message);
}

function blackjack_dealer_peek(db, user, round) {
  if (hand_value(round.dealer_cards) !== 21) {
    round.phase = 'playing';
    return blackjack_advance(db, user, round);
  }

  for (const hand of round.hands) {
    hand.result = 'lost';
  }

  const payout = round.insurance_payout || 0;
  const status = payout > round.bet ? 'won' : (payout === round.bet ? 'push' : 'lost');

  return settle(db, user, round, status, payout, payout ? 'Dealer Blackjack, insurance paid' : 'Dealer has Blackjack');
}

function crash_multiplier(elapsed_ms) {
  return Math.floor(Math.exp((Math.max(0, elapsed_ms) / 1000) * 0.12) * 100) / 100;
}

function crash_settle_if_due(db, user, round) {
  if (crash_multiplier(Date.now() - round.started_ms) < round.crash_point) {
    return null;
  }

  return settle(db, user, round, 'lost', 0, `Crashed at ${round.crash_point.toFixed(2)}x`);
}

function bad_request(res, error) {
  return res.status(400).json({ success: false, error });
}

function finish(db, user, request_id, game) {
  user.updated_at = now_unix();

  return remember(user, request_id, { success: true, balance: user.balance, game });
}

function mount_casino_routes(router, { limit_actions, send_mutation, daily_bonus }) {
  router.get('/state', (req, res) => {
    return send_mutation(req, res, ({ user }) => {
      const casino = ensure_casino(user);
      const active = {};

      for (const [game, round] of Object.entries(casino.active)) {
        active[game] = public_round(user, round, false);
      }

      return {
        success: true,
        username: user.username,
        balance: user.balance,
        fairness: public_fairness(user),
        active,
        history: casino.history.slice(-20)
      };
    });
  });

  router.post('/daily', limit_actions, (req, res) => {
    return send_mutation(req, res, ({ db, user }) => {
      const now = now_unix();

      if (Number.isSafeInteger(user.daily_claimed_at) && now - user.daily_claimed_at < 86400) {
        throw conflict('Daily bonus already claimed');
      }

      const previous = user.balance;
      user.balance += daily_bonus;
      user.daily_claimed_at = now;
      record_coin(db, user, daily_bonus, previous, 'daily bonus', null);

      return { success: true, balance: user.balance, bonus: daily_bonus, next_claim_at: now + 86400 };
    });
  });

  router.post('/roulette', limit_actions, (req, res) => {
    const parsed = parse_roulette_bets(req.body?.bets);

    if (parsed.error) {
      return bad_request(res, parsed.error);
    }

    const common = validate_common({ ...req.body, bet: parsed.total });

    if (common.error) {
      return bad_request(res, common.error);
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, common.request_id);

      if (old) {
        return old;
      }

      const round = begin_round(db, user, 'roulette', common.bet, common.client_seed);

      round.bets = parsed.bets;
      round.number = fair_int(round.server_seed, round.client_seed, round.nonce, 'roulette', 37);
      round.color = roulette_color(round.number);
      round.results = round.bets.map((bet) => {
        const won = roulette_bet_wins(bet, round.number);

        return { ...bet, won, payout: won ? bet.amount * roulette_payouts[bet.type] : 0 };
      });

      const payout = round.results.reduce((sum, result) => sum + result.payout, 0);

      round.won = payout > round.bet;

      const status = payout > round.bet ? 'won' : (payout === round.bet ? 'push' : 'lost');
      const message = `${round.number} ${round.color}`;
      const game = settle(db, user, round, status, payout, message);

      return finish(db, user, common.request_id, game);
    });
  });

  router.post('/crash/start', limit_actions, (req, res) => {
    const common = validate_common(req.body);

    if (common.error) {
      return bad_request(res, common.error);
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, common.request_id);

      if (old) {
        return old;
      }

      const round = begin_round(db, user, 'crash', common.bet, common.client_seed);
      const roll = fair_int(round.server_seed, round.client_seed, round.nonce, 'crash', 100_000_000) / 100_000_000;

      round.crash_point = crash_point(roll);
      round.started_ms = Date.now();
      round.cashed_at = null;
      round.won = false;

      return finish(db, user, common.request_id, public_round(user, round, false));
    });
  });

  router.post('/crash/status', (req, res) => {
    return send_mutation(req, res, ({ db, user }) => {
      const casino = ensure_casino(user);
      const round = casino.active.crash;

      if (!round) {
        const last = casino.history.filter((entry) => entry.game === 'crash').slice(-1)[0] || null;

        return { success: true, balance: user.balance, game: last };
      }

      const settled = crash_settle_if_due(db, user, round);

      return { success: true, balance: user.balance, game: settled || public_round(user, round, false) };
    });
  });

  router.post('/crash/cashout', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);

    if (!request_id) {
      return bad_request(res, 'request_id is required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.crash;

      if (!round) {
        throw conflict('No active crash round');
      }

      const settled = crash_settle_if_due(db, user, round);

      if (settled) {
        return finish(db, user, request_id, settled);
      }

      const multiplier = crash_multiplier(Date.now() - round.started_ms);

      round.cashed_at = multiplier;
      round.won = true;

      const game = settle(db, user, round, 'cashed_out', Math.floor(round.bet * multiplier), `Cashed out at ${multiplier.toFixed(2)}x`);

      return finish(db, user, request_id, game);
    });
  });

  router.post('/mines/start', limit_actions, (req, res) => {
    const common = validate_common(req.body);
    const mines = req.body?.mines;

    if (common.error) {
      return bad_request(res, common.error);
    }

    if (!mines_counts.has(mines)) {
      return bad_request(res, 'mines must be 3, 5, or 8');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, common.request_id);

      if (old) {
        return old;
      }

      const round = begin_round(db, user, 'mines', common.bet, common.client_seed);
      const available = Array.from({ length: 25 }, (_, index) => index);

      round.mines = mines;
      round.revealed = [];
      round.multiplier = 1;
      round.mine_positions = [];

      for (let index = 0; index < mines; index += 1) {
        const pick = fair_int(round.server_seed, round.client_seed, round.nonce, 'mines', available.length, index);
        round.mine_positions.push(available.splice(pick, 1)[0]);
      }

      return finish(db, user, common.request_id, public_round(user, round, false));
    });
  });

  router.post('/mines/reveal', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);
    const tile = req.body?.tile;

    if (!request_id || !Number.isSafeInteger(tile) || tile < 0 || tile > 24) {
      return bad_request(res, 'request_id and tile 0-24 are required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.mines;

      if (!round) {
        throw conflict('No active mines round');
      }

      if (round.revealed.includes(tile)) {
        throw conflict('Tile was already revealed');
      }

      round.revealed.push(tile);

      let game;

      if (round.mine_positions.includes(tile)) {
        game = settle(db, user, round, 'lost', 0, 'Mine revealed');
      } else {
        round.multiplier = mines_multiplier(round.mines, round.revealed.length);

        if (round.revealed.length === 25 - round.mines) {
          game = settle(db, user, round, 'won', Math.floor(round.bet * round.multiplier), 'All safe tiles revealed');
        } else {
          game = public_round(user, round, false);
        }
      }

      return finish(db, user, request_id, game);
    });
  });

  router.post('/mines/cashout', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);

    if (!request_id) {
      return bad_request(res, 'request_id is required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.mines;

      if (!round || !round.revealed.length) {
        throw conflict('Reveal a safe tile before cashing out');
      }

      const game = settle(db, user, round, 'cashed_out', Math.floor(round.bet * round.multiplier), 'Mines cash-out');

      return finish(db, user, request_id, game);
    });
  });

  router.post('/chicken/start', limit_actions, (req, res) => {
    const common = validate_common(req.body);
    const risk = typeof req.body?.risk === 'string' ? req.body.risk.toLowerCase() : '';

    if (common.error) {
      return bad_request(res, common.error);
    }

    if (!Object.prototype.hasOwnProperty.call(chicken_rules, risk)) {
      return bad_request(res, 'risk must be safe, spicy, or wild');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, common.request_id);

      if (old) {
        return old;
      }

      const round = begin_round(db, user, 'chicken', common.bet, common.client_seed);

      round.risk = risk;
      round.crossed = 0;
      round.multiplier = 1;
      round.caught = false;

      return finish(db, user, common.request_id, public_round(user, round, false));
    });
  });

  router.post('/chicken/jump', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);

    if (!request_id) {
      return bad_request(res, 'request_id is required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.chicken;

      if (!round) {
        throw conflict('No active chicken round');
      }

      const rule = chicken_rules[round.risk];
      const roll = fair_int(round.server_seed, round.client_seed, round.nonce, `chicken-${round.crossed}`, 10000);

      round.crossed += 1;
      round.caught = roll >= rule.survival;
      round.multiplier = Math.floor((rule.multiplier ** round.crossed) * 100) / 100;

      let game;

      if (round.caught) {
        game = settle(db, user, round, 'lost', 0, 'Chicken was caught');
      } else if (round.crossed >= 5) {
        game = settle(db, user, round, 'won', Math.floor(round.bet * round.multiplier), 'All lanes crossed');
      } else {
        game = public_round(user, round, false);
      }

      return finish(db, user, request_id, game);
    });
  });

  router.post('/chicken/cashout', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);

    if (!request_id) {
      return bad_request(res, 'request_id is required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.chicken;

      if (!round || round.crossed < 1) {
        throw conflict('Cross a lane before cashing out');
      }

      const game = settle(db, user, round, 'cashed_out', Math.floor(round.bet * round.multiplier), 'Chicken cash-out');

      return finish(db, user, request_id, game);
    });
  });

  router.post('/blackjack/start', limit_actions, (req, res) => {
    const common = validate_common(req.body);

    if (common.error) {
      return bad_request(res, common.error);
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, common.request_id);

      if (old) {
        return old;
      }

      const round = begin_round(db, user, 'blackjack', common.bet, common.client_seed);
      const deck = shuffled_deck(round.server_seed, round.client_seed, round.nonce);

      const player_cards = [deck.pop(), deck.pop()];

      round.dealer_cards = [deck.pop(), deck.pop()];
      round.deck = deck;
      round.hands = [{ cards: player_cards, bet: round.bet, done: false, doubled: false, result: null }];
      round.active_hand = 0;
      round.phase = 'playing';
      round.insurance = 0;
      round.insurance_payout = 0;
      round.message = 'Hit or stand';

      const player_total = hand_value(player_cards);
      const dealer_total = hand_value(round.dealer_cards);
      const upcard = card_points(round.dealer_cards[0]);

      let game;

      if (player_total === 21 && dealer_total === 21) {
        round.hands[0].result = 'push';
        game = settle(db, user, round, 'push', round.bet, 'Both have Blackjack');
      } else if (player_total === 21) {
        round.hands[0].result = 'blackjack';
        game = settle(db, user, round, 'blackjack', Math.floor(round.bet * 2.5), 'Blackjack, paid 3:2');
      } else if (upcard === 11) {
        round.phase = 'insurance';
        round.message = 'Dealer shows an Ace, insurance?';
        game = public_round(user, round, false);
      } else if (dealer_total === 21) {
        game = blackjack_dealer_peek(db, user, round);
      } else {
        game = public_round(user, round, false);
      }

      return finish(db, user, common.request_id, game);
    });
  });

  router.post('/blackjack/action', limit_actions, (req, res) => {
    const request_id = require_request_id(req.body);
    const action = req.body?.action;

    if (!request_id || !['hit', 'stand', 'double', 'split', 'insurance', 'decline_insurance'].includes(action)) {
      return bad_request(res, 'request_id and action hit/stand/double/split/insurance/decline_insurance are required');
    }

    return send_mutation(req, res, ({ db, user }) => {
      const old = replay(user, request_id);

      if (old) {
        return old;
      }

      const round = ensure_casino(user).active.blackjack;

      if (!round) {
        throw conflict('No active blackjack round');
      }

      if (round.phase === 'insurance') {
        if (action !== 'insurance' && action !== 'decline_insurance') {
          throw conflict('Accept or decline insurance first');
        }

        const amount = Math.floor(round.hands[0].bet / 2);

        if (action === 'insurance' && amount > 0) {
          charge(db, user, round, amount, 'blackjack insurance');
          round.insurance = amount;

          if (hand_value(round.dealer_cards) === 21) {
            round.insurance_payout = amount * 3;
          }
        }

        return finish(db, user, request_id, blackjack_dealer_peek(db, user, round));
      }

      if (action === 'insurance' || action === 'decline_insurance') {
        throw conflict('Insurance is not available');
      }

      const hand = round.hands[round.active_hand];

      if (action === 'hit') {
        hand.cards.push(round.deck.pop());

        const total = hand_value(hand.cards);

        if (total > 21) {
          hand.done = true;
          hand.result = 'bust';
        } else if (total === 21) {
          hand.done = true;
        }
      } else if (action === 'stand') {
        hand.done = true;
      } else if (action === 'double') {
        if (!can_double(user, hand)) {
          throw conflict('Double down is not available');
        }

        charge(db, user, round, hand.bet, 'blackjack double down');
        hand.bet *= 2;
        hand.doubled = true;
        hand.cards.push(round.deck.pop());
        hand.done = true;

        if (hand_value(hand.cards) > 21) {
          hand.result = 'bust';
        }
      } else {
        if (!can_split(user, round, hand)) {
          throw conflict('Split is not available');
        }

        charge(db, user, round, hand.bet, 'blackjack split');

        const aces = card_points(hand.cards[0]) === 11;
        const second = { cards: [hand.cards.pop()], bet: hand.bet, done: false, doubled: false, result: null };

        round.hands.splice(round.active_hand + 1, 0, second);

        for (const split_hand of [hand, second]) {
          split_hand.cards.push(round.deck.pop());

          if (aces || hand_value(split_hand.cards) === 21) {
            split_hand.done = true;
          }
        }
      }

      const game = blackjack_advance(db, user, round);

      return finish(db, user, request_id, game);
    });
  });
}

module.exports = {
  chicken_rules,
  crash_point,
  ensure_casino,
  hand_value,
  max_bet,
  mines_multiplier,
  mount_casino_routes,
  public_fairness,
  roulette_color
};
