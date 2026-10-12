const fs = require('fs');
const path = require('path');

const current_schema_version = 1;

function empty_database() {
  return {
    schema_version: current_schema_version,
    users: {},
    usernames: {},
    sessions: {},
    coin_ledger: []
  };
}

function migrate(database) {
  const db = database && typeof database === 'object' && !Array.isArray(database)
    ? database
    : empty_database();

  const defaults = empty_database();

  for (const [key, value] of Object.entries(defaults)) {
    if (db[key] === undefined) {
      db[key] = value;
    }
  }

  if (db.schema_version !== current_schema_version) {
    throw new Error(`Unsupported database schema ${db.schema_version}`);
  }

  return db;
}

function create_store(filename) {
  const database_path = path.resolve(filename);
  let committing = false;

  function read() {
    try {
      return migrate(JSON.parse(fs.readFileSync(database_path, 'utf8')));
    } catch (error) {
      if (error.code === 'ENOENT') {
        return empty_database();
      }

      throw error;
    }
  }

  function write(database) {
    fs.mkdirSync(path.dirname(database_path), { recursive: true });

    const payload = `${JSON.stringify(migrate(database), null, 2)}\n`;
    const temporary = `${database_path}.${process.pid}.${Date.now()}.tmp`;

    try {
      fs.writeFileSync(temporary, payload, {
        encoding: 'utf8',
        mode: 0o600,
        flag: 'wx'
      });
      fs.renameSync(temporary, database_path);

      return true;
    } catch (error) {
      try {
        fs.unlinkSync(temporary);
      } catch {}

      console.error('[DB] Atomic commit failed:', error.message);
      return false;
    }
  }

  function transaction(callback) {
    if (committing) {
      throw new Error('Nested database transactions are not supported');
    }

    committing = true;

    try {
      const database = read();
      const result = callback(database);

      if (result && typeof result.then === 'function') {
        throw new Error('Database transaction callbacks must be synchronous');
      }

      if (!write(database)) {
        throw new Error('Database commit failed');
      }

      return result;
    } finally {
      committing = false;
    }
  }

  return {
    database_path,
    read,
    transaction,
    write
  };
}

function create_memory_store(initial = empty_database()) {
  let database = migrate(initial);

  return {
    database_path: ':memory:',
    read: () => structuredClone(database),
    transaction(callback) {
      const draft = structuredClone(database);
      const result = callback(draft);
      database = migrate(draft);
      return result;
    },
    write(next) {
      database = migrate(structuredClone(next));
      return true;
    }
  };
}

module.exports = {
  create_memory_store,
  create_store,
  current_schema_version,
  empty_database,
  migrate
};
