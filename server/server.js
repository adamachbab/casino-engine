const path = require('path');
const { create_app } = require('./src/app');
const { create_store } = require('./src/lib/data-store');

function integer_env(name, fallback) {
  const value = Number(process.env[name]);

  return Number.isSafeInteger(value) && value >= 0 ? value : fallback;
}

const host = process.env.CASINO_HOST || '127.0.0.1';
const port = integer_env('CASINO_PORT', 8420);
const database_path = process.env.CASINO_DATABASE_PATH || path.join(__dirname, 'data', 'casino.json');

const app = create_app({
  store: create_store(database_path),
  starting_balance: integer_env('CASINO_STARTING_BALANCE', 10000),
  daily_bonus: integer_env('CASINO_DAILY_BONUS', 1000)
});

app.listen(port, host, () => {
  console.log(`[CASINO] Listening on http://${host}:${port}`);
  console.log(`[CASINO] Database ${path.resolve(database_path)}`);
});
