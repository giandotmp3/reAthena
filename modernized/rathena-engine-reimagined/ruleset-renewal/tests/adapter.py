"""Seam to the Luau script host. Until script-host exposes a runner, calls are pending."""


class HostUnavailable(NotImplementedError):
    pass


def call(fn: str, args: list):
    # Future: invoke script-host with scripts/formulas.luau; stubs there raise NotImplemented.
    raise HostUnavailable(f"script-host binding not wired for {fn}")
