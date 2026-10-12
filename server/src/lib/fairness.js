const crypto = require('crypto');

const algorithm = 'HMAC-SHA256/v1';

function new_seed() {
  return crypto.randomBytes(32).toString('hex');
}

function commitment(seed) {
  return crypto.createHash('sha256').update(seed, 'utf8').digest('hex');
}

function digest(seed, client_seed, nonce, game, cursor = 0) {
  return crypto
    .createHmac('sha256', Buffer.from(seed, 'hex'))
    .update(`${client_seed}:${nonce}:${game}:${cursor}`, 'utf8')
    .digest();
}

function fair_int(seed, client_seed, nonce, game, max, cursor = 0) {
  if (!Number.isSafeInteger(max) || max < 1 || max > 0x100000000) {
    throw new RangeError('invalid fair range');
  }

  const limit = Math.floor(0x100000000 / max) * max;

  for (let attempt = 0; attempt < 1024; attempt += 1) {
    const value = digest(seed, client_seed, nonce, game, cursor + attempt).readUInt32BE(0);

    if (value < limit) {
      return value % max;
    }
  }

  throw new Error('Unable to draw an unbiased result');
}

function shuffled_deck(seed, client_seed, nonce) {
  const suits = ['S', 'H', 'D', 'C'];
  const ranks = ['A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];
  const deck = [];

  for (const suit of suits) {
    for (const rank of ranks) {
      deck.push(`${rank}${suit}`);
    }
  }

  let cursor = 0;

  for (let index = deck.length - 1; index > 0; index -= 1) {
    const swap = fair_int(seed, client_seed, nonce, 'blackjack', index + 1, cursor);
    cursor += 1;
    [deck[index], deck[swap]] = [deck[swap], deck[index]];
  }

  return deck;
}

module.exports = {
  algorithm,
  commitment,
  digest,
  fair_int,
  new_seed,
  shuffled_deck
};
