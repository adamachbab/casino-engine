# Security

## Scope

casino-engine uses virtual coins only. The server is the authority for balances and outcomes; the client only displays results.

## Known limitations

- The server speaks plain HTTP. Put it behind an HTTPS reverse proxy before exposing it beyond localhost.
- Data is stored in a single JSON file and is intended for a single process.
- Rate limits are in memory and reset on restart.

## Reporting

Report vulnerabilities privately through GitHub security advisories on this repository rather than public issues.
