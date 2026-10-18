-- Globals the script host injects into every mod environment that are NOT generated from api/engine.yaml
-- (checked with: luau-analyze --defs=<this file>). The generated api/engine.d.luau declares the typed globals (pipeline, num, config, ...)
-- and `type Engine`, the table a package's register(engine) receives. A global `engine` of that type exists only when the loader asks
-- for it (ModEnvironmentSpec::expose_engine).
declare function import(path: string): any
declare engine: any
