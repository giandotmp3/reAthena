import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MIGRATE = os.path.dirname(HERE)
sys.path.insert(0, MIGRATE)
sys.path.insert(0, os.path.join(MIGRATE, "tools"))
REPO = os.path.abspath(os.path.join(MIGRATE, "..", "..", "..", ".."))

from rmig import consts, pipeline  # noqa: E402
from rmig.base import World, load_chain  # noqa: E402


def stub(db_type, version, body="", imports=(), clear=False):
    s = "Header:\n  Type: %s\n  Version: %d\n" % (db_type, version)
    if clear:
        s += "  Clear: true\n"
    if body:
        s += "\nBody:\n" + body
    if imports:
        s += "\nFooter:\n  Imports:\n"
        for imp in imports:
            if isinstance(imp, str):
                s += "  - Path: %s\n" % imp
            else:
                path, extra = imp[0], imp[1:]
                s += "  - Path: %s\n" % path
                for kv in extra:
                    s += "    %s\n" % kv
    return s


class Tree(object):
    """A throw-away repository root with db/ files."""

    def __init__(self, files=None):
        self.root = tempfile.mkdtemp(prefix="rmig-test-")
        for rel, text in (files or {}).items():
            self.write(rel, text)

    def write(self, rel, text):
        p = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write(text)

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


def world(root, mode="prerenewal", **kw):
    return World(root, mode, consts.load(mode), **kw)


def load_app(cls, root, mode="prerenewal", args=(), world_kw=None, finish=True):
    w = world(root, mode, **(world_kw or {}))
    app = cls(w, *args)
    load_chain(app, app.STUB)
    if finish:
        app.finish()
    return app


def items_app(tree_root, mode="prerenewal"):
    from rmig.db_items import ItemDb
    return load_app(ItemDb, tree_root, mode)
