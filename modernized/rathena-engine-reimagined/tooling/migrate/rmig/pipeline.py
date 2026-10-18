"""Load all databases of one mode in the order the legacy map-server loads them."""
import os
import time
from . import consts as _consts
from . import yamlio
from .base import World, load_chain
from .db_items import ItemDb
from .db_mobs import MobDb, RandOptGroupNames
from .db_skills import SkillDb
from .db_misc import StatusDb, SizeFixDb, AttrFixDb, RefineDb, StatPointDb, JobDb
from .db_raw import RawDb, RAW_DBS

CORE = ("item_db", "skill_db", "mob_db", "job_stats", "statpoint", "size_fix", "refine", "status", "attr_fix")


class Loaded(object):
    def __init__(self, world):
        self.w = world
        self.dbs = {}
        self.timing = {}


def _stub_header(root, stub):
    path = os.path.join(root, stub)
    if not os.path.isfile(path):
        return None
    doc, _ = yamlio.load_file(path)
    if not isinstance(doc, dict) or "Header" not in doc:
        return None
    h = doc["Header"]
    try:
        return h["Type"], int(h["Version"])
    except Exception:
        return None


DEPS = {"mob_db": ("item_db",), "skill_db": ("item_db",), "status": ("skill_db", "item_db"), "refine": ("item_db",)}


def _closure(only):
    if only is None:
        return None
    need = set()
    todo = list(only)
    while todo:
        n = todo.pop()
        if n in need:
            continue
        need.add(n)
        todo.extend(DEPS.get(n, ()))
    return need


def load_mode(root, mode, import_dir="db/import", renewal_aspd=True, raw=True, only=None, generator=False,
              consts_path=None):
    c = _consts.load(mode, consts_path)
    w = World(root, mode, c, renewal_aspd=renewal_aspd, generator=generator, import_dir=import_dir)
    out = Loaded(w)

    def run(app, name):
        t = time.time()
        load_chain(app, app.STUB)
        app.finish()
        out.dbs[name] = app
        out.timing[name] = time.time() - t
        return app

    need = _closure(only)
    want = lambda n: need is None or n in need
    items = run(ItemDb(w), "item_db")
    w.items = items
    randopt = run(RandOptGroupNames(w), "_randomopt_names")
    skills = run(SkillDb(w, items), "skill_db") if want("skill_db") else None
    w.skills = skills
    if want("mob_db"):
        run(MobDb(w, items, randopt), "mob_db")
    if want("job_stats"):
        run(JobDb(w), "job_stats")
    if want("statpoint"):
        run(StatPointDb(w), "statpoint")
    if want("size_fix"):
        run(SizeFixDb(w), "size_fix")
    if want("refine"):
        run(RefineDb(w, items), "refine")
    if want("status"):
        run(StatusDb(w, skills), "status")
    if want("attr_fix"):
        run(AttrFixDb(w), "attr_fix")
    if raw and only is None:
        for name, (stub, key, scripts) in RAW_DBS.items():
            hdr = _stub_header(root, stub)
            if hdr is None:
                continue
            if not os.path.isfile(os.path.join(root, stub)):
                continue
            app = RawDb(w, name, stub, key, scripts, hdr[0], hdr[1])
            run(app, name)
    return out
