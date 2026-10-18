# Golden traces

Cases in `../cases/*.luau` with `expected = nil` await a recording from the legacy
golden-trace harness (`RATHENA_GOLDEN_TRACE`, BR-RULESET-119). Workflow:

1. Run the legacy server build with the trace env var set on a scenario matching the
   case `given` table (use fake characters and a throwaway database).
2. Convert each recorded input/output pair to a Luau table and put the output in the
   case's `expected`. Keep float32 steps and RNG draw counts (BR-RULESET-115).
3. Never copy account names, passwords, tokens or connection strings into a case.

Inputs here are synthetic. No legacy data or credentials are stored in this package.
