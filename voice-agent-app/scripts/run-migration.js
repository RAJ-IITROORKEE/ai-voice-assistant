// One-shot migration runner: applies db/migrations/0001_init.sql via pg.
const fs = require("fs");
const path = require("path");
const { Client } = require("pg");

const cs =
  "postgresql://postgres:61c35a1e0f58215f2b6d1fab24560a4e@ts4hxi45.us-east.database.insforge.app:5432/insforge?sslmode=require";

async function main() {
  const sql = fs.readFileSync(
    path.join(__dirname, "..", "db", "migrations", "0001_init.sql"),
    "utf8"
  );
  const client = new Client({ connectionString: cs });
  await client.connect();
  try {
    await client.query(sql);
    console.log("MIGRATION_OK");
  } catch (e) {
    console.error("MIGRATION_ERR:", e.message);
    process.exitCode = 1;
  } finally {
    await client.end();
  }
}
main();
