"""Shared pieces: the load chain (db/<x>.yml stub -> pre-re/re -> import), the world context and the applier base."""
import os
from . import yamlio
from .yamlio import NULL, Reject, Reader, has

U64 = 0xFFFFFFFFFFFFFFFF


class World(object):
    """What the legacy loaders can see while they run: constants/limits of the mode, and earlier-loaded databases."""

    def __init__(self, root, mode, consts, renewal_aspd=True, generator=False, import_dir="db/import"):
        self.root = root
        self.mode = mode                     # 'prerenewal' | 'renewal'
        self.renewal = (mode == "renewal")
        self.c = consts
        self.renewal_aspd = renewal_aspd     # RENEWAL_ASPD (only meaningful in renewal mode)
        self.generator = generator
        self.import_dir = import_dir
        self.items = None                    # ItemDb applier (loaded first, like do_init_itemdb)
        self.skills = None
        self.randomopt_groups = {}           # name(lower) -> id, filled lazily by mobs


class Applier(object):
    NAME = None
    TYPE = None
    VERSION = None
    MIN_VERSION = None
    STUB = None

    def __init__(self, world):
        self.w = world
        self.c = world.c
        self.r = Reader()
        self.rows = {}
        self.parsed = 0
        self.rejected = []                   # (file, reason)
        self.files = []                      # files loaded, in order
        self.missing = []                    # optional files that do not exist (db/import/*)
        self.file_errors = []                # (file, message): unreadable / malformed / incompatible files (fatal for the run)
        self.clears = 0
        self.touched = {}                    # key -> list of files that patched it (provenance)
        self._cur_key = None

    # --- hooks the DBs override ---
    def clear(self):
        self.rows.clear()
        self.clears += 1

    def parse_body_node(self, node):
        raise NotImplementedError

    def finish(self):
        pass

    # --- driver ---
    def apply(self, node):
        self._cur_key = None
        try:
            ok = self.parse_body_node(node)
        except Reject as e:
            ok = False
            self.rejected.append((self.r.file, str(e)))
        if ok:
            self.parsed += 1
            if self._cur_key is not None:
                self.touched.setdefault(self._cur_key, []).append(self.r.file)
        elif ok is False:
            pass
        return ok


def load_chain(app, stub_rel, root=None):
    """Mirror of YamlDatabase::load(path) including Header checks, Clear, Body and Footer.Imports (Mode, Generator)."""
    w = app.w
    root = root or w.root

    def mode_compiled(import_file):
        if w.renewal:
            c = "Renewal"
            if not w.renewal_aspd and "job_aspd.yml" in import_file:
                c = "Prerenewal"
            return c
        return "Prerenewal"

    def load(rel):
        mapped = rel
        if rel.startswith("db/import/") and w.import_dir != "db/import":
            mapped = os.path.join(w.import_dir, rel[len("db/import/"):])
        full = os.path.join(root, mapped)
        app.r.file = rel
        if not os.path.isfile(full):
            app.r.warn("Failed to open %s database file from '%s'." % (app.TYPE, rel))
            if rel.startswith("db/import/"):
                app.missing.append(rel)       # the overlay is optional (created at install from db/import-tmpl)
            else:
                app.file_errors.append((rel, "file not found"))
            return False
        try:
            doc, dups = yamlio.load_file(full)
        except yamlio.YamlSyntaxError as e:
            app.r.warn("There is likely a syntax error in the file: %s" % e)
            app.file_errors.append((rel, "YAML syntax error: %s" % e))
            return False
        if dups:
            app.r.warn("duplicate keys ignored (first wins): %s" % ",".join(sorted(set(dups))[:5]))
        if not isinstance(doc, dict) or "Header" not in doc:
            app.r.warn('No database "Header" was found.')
            app.file_errors.append((rel, 'no "Header"'))
            return False
        header = doc["Header"]
        try:
            htype = app.r.s(header, "Type")
            hver = app.r.u16(header, "Version")
        except Reject:
            app.file_errors.append((rel, "Header.Type/Version missing or invalid"))
            return False
        if app.TYPE != htype:
            app.r.warn("Database type mismatch: %s != %s." % (app.TYPE, htype))
            app.file_errors.append((rel, "Header.Type mismatch: %s != %s" % (app.TYPE, htype)))
            return False
        if hver != app.VERSION:
            if hver > app.VERSION:
                app.r.warn("Database version %d is not supported. Maximum version is: %d" % (hver, app.VERSION))
                app.file_errors.append((rel, "Header.Version %d > supported %d" % (hver, app.VERSION)))
                return False
            elif hver >= app.MIN_VERSION:
                app.r.warn("Database version %d is outdated (current %d)." % (hver, app.VERSION))
            else:
                app.r.warn("Database version %d is not supported anymore. Minimum version is: %d" % (hver, app.MIN_VERSION))
                app.file_errors.append((rel, "Header.Version %d < minimum %d" % (hver, app.MIN_VERSION)))
                return False
        app.files.append(rel)
        if has(header, "Clear"):
            try:
                if app.r.b(header, "Clear"):
                    app.clear()
            except Reject:
                pass
        if "Body" in doc:
            body = doc["Body"]
            if isinstance(body, list):
                for node in body:
                    app.r.file = rel
                    app.apply(node)
            elif isinstance(body, dict):
                for k, node in body.items():
                    app.r.file = rel
                    app.apply(node)
        footer = doc.get("Footer")
        if has(footer, "Imports"):
            imps = footer["Imports"]
            if isinstance(imps, list):
                for node in imps:
                    app.r.file = rel
                    try:
                        path = app.r.s(node, "Path")
                    except Reject:
                        continue
                    if has(node, "Mode"):
                        try:
                            m = app.r.s(node, "Mode")
                        except Reject:
                            continue
                        if mode_compiled(path) != m:
                            continue
                    if has(node, "Generator"):
                        try:
                            g = app.r.b(node, "Generator")
                        except Reject:
                            continue
                        if not (w.generator and g):
                            continue
                    load(path)
        return True

    ok = load(stub_rel)
    return ok
