"""Helpers that write the documents the engine reads: prefabs (.ykprefab) and scenes (.ykscene).

They produce exactly the JSON the editor saves (the same component and property names, taken from
docs/components.md), only sparser: properties left out take the component's defaults. Running
`yk format` afterwards rewrites every file in the editor's canonical, complete form.

Entity ids are derived from names, so regenerating a file changes nothing unless its content changed
and diffs stay small.
"""
import hashlib
import json
import os


def make_id(*parts):
    digest = hashlib.sha1("\x00".join(str(p) for p in parts).encode("utf-8")).hexdigest()
    return "%016x" % (int(digest[:16], 16) or 1)


class Ref:
    """A reference to another entity of the same scene, by that entity's (unique) name."""

    def __init__(self, name):
        self.name = name


def ref(name):
    return Ref(name)


class Node:
    """An entity under construction: a name, a local transform, tags, components and children."""

    def __init__(self, name, at=(0, 0), rotation=0, scale=(1, 1), tags=(), active=True, locked=False):
        self.locked = locked
        self.name = name
        self.position = [at[0], at[1]]
        self.rotation = rotation
        self.scale = [scale[0], scale[1]]
        self.tags = list(tags)
        self.active = active
        self.components = []
        self.children = []

    def add(self, type_name, **properties):
        self.components.append({"type": type_name, "properties": properties})
        return self

    def child(self, node):
        self.children.append(node)
        return node


def _encode(value, resolve):
    if isinstance(value, Ref):
        return resolve(value)
    if isinstance(value, (list, tuple)):
        return [_encode(v, resolve) for v in value]
    return value


def _record(node_id, name, parent_id, transform, components, tags, active, prefab, resolve, locked=False):
    record = {"id": node_id, "name": name}
    if parent_id:
        record["parent"] = parent_id
    if not active:
        record["active"] = False
    if locked:
        record["locked"] = True
    if prefab:
        record["prefab"] = prefab
    if tags:
        record["tags"] = list(tags)
    record["transform"] = {"position": transform[0], "rotation": transform[1], "scale": transform[2]}
    record["components"] = [
        {"type": c["type"], "properties": {k: _encode(v, resolve) for k, v in c["properties"].items()}}
        for c in components
    ]
    return record


def flatten(node, id_scope, parent_id=None, prefab=None, resolve=None, path=""):
    """Depth-first list of entity records for `node` and everything below it."""
    resolve = resolve or (lambda r: r.name)
    here = path + "/" + node.name if path else node.name
    node_id = make_id(id_scope, here)
    out = [_record(node_id, node.name, parent_id, (node.position, node.rotation, node.scale),
                   node.components, node.tags, node.active, prefab, resolve, node.locked)]
    for child in node.children:
        out.extend(flatten(child, id_scope, node_id, None, resolve, here))
    return out


def write_json(path, document):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as f:
        json.dump(document, f, indent=2)
        f.write("\n")


def write_prefab(project_dir, relative_path, root):
    """Writes `root` (a Node) as a prefab file below the project directory."""
    records = flatten(root, "prefab:" + relative_path)
    records[0].pop("parent", None)
    write_json(os.path.join(project_dir, relative_path),
               {"format": "yk.prefab", "version": 1, "root": records[0]["id"], "entities": records})


class SceneBuilder:
    """Collects entities into a scene document. Prefab instances are placed by prefab path."""

    def __init__(self, project_dir, name, gravity=(0, 9.81), background="#0b171bff"):
        self.project_dir = project_dir
        self.name = name
        self.gravity = list(gravity)
        self.background = background
        self.records = []
        self.ids_by_name = {}
        self._prefab_cache = {}
        self._pending = []  # (record, property-holder) pairs needing reference resolution

    # ---- naming and references -------------------------------------------------------------
    def _register(self, record):
        name = record["name"]
        self.ids_by_name.setdefault(name, []).append(record["id"])

    def _resolve(self, reference):
        ids = self.ids_by_name.get(reference.name, [])
        if len(ids) != 1:
            raise KeyError("reference to '%s' matches %d entities" % (reference.name, len(ids)))
        return ids[0]

    def id_of(self, name):
        return self._resolve(Ref(name))

    # ---- adding entities -------------------------------------------------------------------
    def add(self, node, parent=None):
        """Adds a Node tree; `parent` is the name of an entity already added (or None)."""
        parent_id = self.id_of(parent) if parent else None
        scope = "scene:" + self.name
        # References are resolved at the end, so entities can point at ones defined later.
        records = flatten(node, scope, parent_id, None, lambda r: r)
        for record in records:
            self.records.append(record)
            self._register(record)
        return records[0]["id"]

    def group(self, name, at=(0, 0), parent=None):
        return self.add(Node(name, at), parent)

    def place(self, prefab_path, name, at, parent=None, rotation=None, scale=None, tags=None,
              overrides=None, child_overrides=None, active=True):
        """Instantiates a prefab file (project-relative) as an entity called `name` at world `at`.

        overrides:       {"SpriteRenderer": {"size": [3, 1]}}   properties of the root's components
        child_overrides: {"Glow": {"Light2D": {"radius": 3}}}   the same for a named child entity
        """
        document = self._prefab(prefab_path)
        entities = document["entities"]
        root_old = document["root"]
        scope = "scene:%s/%s" % (self.name, name)
        new_ids = {e["id"]: make_id(scope, e["id"]) for e in entities}
        parent_id = self.id_of(parent) if parent else None
        overrides = overrides or {}
        child_overrides = child_overrides or {}
        placed = []
        for entity in entities:
            is_root = entity["id"] == root_old
            components = json.loads(json.dumps(entity.get("components", [])))
            wanted = overrides if is_root else child_overrides.get(entity["name"], {})
            for type_name, properties in wanted.items():
                target = next((c for c in components if c["type"] == type_name), None)
                if target is None:
                    raise KeyError("prefab %s has no %s on '%s'" % (prefab_path, type_name, entity["name"]))
                target.setdefault("properties", {}).update(properties)
            transform = entity.get("transform", {})
            if is_root:
                position = [at[0], at[1]]
                rot = transform.get("rotation", 0) if rotation is None else rotation
                scl = transform.get("scale", [1, 1]) if scale is None else list(scale)
                tags_here = list(entity.get("tags", [])) if tags is None else list(tags)
            else:
                position = transform.get("position", [0, 0])
                rot = transform.get("rotation", 0)
                scl = transform.get("scale", [1, 1])
                tags_here = list(entity.get("tags", []))
            # Ids inside the prefab (a reference from one part to another) follow the copies.
            for component in components:
                component["properties"] = {
                    k: self._remap(v, new_ids) for k, v in component.get("properties", {}).items()}
            old_parent = entity.get("parent")
            record = _record(new_ids[entity["id"]], name if is_root else entity["name"],
                             parent_id if is_root else new_ids[old_parent], (position, rot, scl),
                             components, tags_here, active if is_root else entity.get("active", True),
                             prefab_path if is_root else None, lambda r: r, bool(entity.get("locked")))
            placed.append(record)
        for record in placed:
            self.records.append(record)
            self._register(record)
        return placed[0]["id"]

    @staticmethod
    def _remap(value, ids):
        if isinstance(value, str) and value in ids:
            return ids[value]
        if isinstance(value, list):
            return [SceneBuilder._remap(v, ids) for v in value]
        return value

    def _prefab(self, path):
        if path not in self._prefab_cache:
            with open(os.path.join(self.project_dir, path)) as f:
                self._prefab_cache[path] = json.load(f)
        return self._prefab_cache[path]

    def set_property(self, name, type_name, **properties):
        """Sets properties of a component on an entity that was already added."""
        entity_id = self.id_of(name)
        for record in self.records:
            if record["id"] == entity_id:
                for component in record["components"]:
                    if component["type"] == type_name:
                        component["properties"].update(properties)
                        return
        raise KeyError("no %s on '%s'" % (type_name, name))

    # ---- writing ---------------------------------------------------------------------------
    def document(self):
        for record in self.records:
            for component in record["components"]:
                component["properties"] = {
                    k: _encode(v, self._resolve) for k, v in component["properties"].items()}
        return {"format": "yk.scene", "version": 1,
                "settings": {"name": self.name, "gravity": self.gravity, "background": self.background},
                "entities": self.records}

    def write(self, relative_path):
        write_json(os.path.join(self.project_dir, relative_path), self.document())
