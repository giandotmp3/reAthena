"""Shared, lazily computed (and cached) runs so the slow full-database loads happen once per test process."""
import argparse
import os
import sys
import tempfile

from helpers import MIGRATE, REPO, pipeline  # noqa

sys.path.insert(0, MIGRATE)
import rathena_migrate  # noqa: E402

FIXTURES = os.path.join(MIGRATE, "tests", "fixtures")
_cache = {}


def cli_args(out, **kw):
    ns = argparse.Namespace(root=REPO, out=out, import_dir="db/import", no_raw=False, no_renewal_aspd=False, generator=False,
                            strict=False, only=None, mode="prerenewal", cmd="migrate")
    for k, v in kw.items():
        setattr(ns, k, v)
    return ns


def cli_run(mode):
    """the migrate command for one mode into a temp dir: (loaded, manifest, outdir)"""
    key = ("cli", mode)
    if key not in _cache:
        out = tempfile.mkdtemp(prefix="rmig-out-")
        loaded, manifest, _, _ = rathena_migrate.migrate_mode(cli_args(out), mode)
        _cache[key] = (loaded, manifest, out)
    return _cache[key]


def overlay(mode, name):
    key = ("overlay", mode, name)
    if key not in _cache:
        _cache[key] = pipeline.load_mode(REPO, mode, import_dir=os.path.join(FIXTURES, "overlay_" + name), raw=False)
    return _cache[key]
