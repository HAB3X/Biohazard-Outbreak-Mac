#!/usr/bin/env python3
"""The game's character models (romdata.afs) for Studio's 3D view: an enemy is drawn as its own model, where it stands.

An enemy record's kind (its id) and class byte name the model E<kind>_<class>.NBD: every kind and class placed in File 1's first scenario has that file
but zombie classes 80 and 85 and kind 17 class 2 (those have no model by this rule: Studio says so and draws a figure). The class byte is the one the
enemy sweep found to change "model and animation parts" (docs/research/rdt-format.md). The rule is read from the file names, not yet seen in the game.

A character model is one or more skinned meshes in their rest pose (standing at the origin, 176 tall for a zombie, facing +z), with its pictures in
its own TEX chunk (a four byte size, then the .sld codec's stream of a TIM2 file). Its vertex colours are black: the game lights characters as they
move, so they are drawn at full colour under the room's light.

    model_file(game, kind, cls)       -> "E01_09.NBD", or None
    enemy_models(game)                -> [{"id", "cls", "file", "placeable"}] of every enemy model on the disc
    character_json(game, name)        -> {"bones", "meshes", "tids"} as bh_rdt_edit.model_json's; tids are "NAME#n", picture n of the model's own
    picture_png(game, key)            -> PNG bytes of "NAME#n"
"""
import io

import bh_archive as archive
import bh_model
import bh_nbd as nbd
import bh_sld
import tm2

PACK = "/NETBIO00.DAT/romdata.afs"
FULL_LIGHT = (255.0, 255.0, 255.0, 128.0)       # the vertex colour that leaves the picture as it is under the game's light (/128 in the 3D view)


def _members(game):
    return {name.upper(): node for name, node in archive.Archive(game).open(PACK).members()}


def model_file(game, kind, cls):
    """The model an enemy of `kind` and class `cls` is drawn with, or None when the pack has no E<kind>_<class>.NBD"""
    name = f"E{int(kind):02d}_{int(cls):02d}.NBD"
    return name if name in _members(game) else None


def enemy_models(game):
    """[{"id": kind, "cls": class, "file": name, "placeable": bool}] of every enemy model the game's pack holds (E<kind>_<class>.NBD), by kind and class.
    Placeable: some scenario of either game has an enemy of that kind, whose record a new one takes its settings from (bh_enemies.kind_record)"""
    import re

    import bh_enemies

    out = []
    for name in sorted(_members(game)):
        m = re.fullmatch(r"E(\d\d)_(\d\d)\.NBD", name)
        if m:
            kind = int(m.group(1))
            out.append({"id": kind, "cls": int(m.group(2)), "file": name, "placeable": bh_enemies.kind_record(game, kind) is not None})
    return out


def _read(game, name):
    members = _members(game)
    if name.upper() not in members:
        raise FileNotFoundError(f"{name}: not in {PACK}")
    return members[name.upper()].read()


def character_json(game, name):
    """A character model for the 3D view: its meshes on one plain bone at the origin, each material's picture as "NAME#n" """
    import bh_rdt_edit

    data = _read(game, name)
    amo = next(c for c in nbd.chunks(data) if c[0] == "AMO")
    meshes = [m for m in (bh_rdt_edit.mesh_json(index, mesh, 0, FULL_LIGHT) for index, mesh in enumerate(nbd.meshes(data, amo[1], amo[2]))) if m]
    model = bh_model.Model(data)
    tids = [f"{name}#{model.picture_number(m)}" for m in range(len(model.materials()[0]))]
    return {"bones": [[-1, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]], "meshes": meshes, "tids": tids}


def pictures(data):
    """The pictures (tm2.Picture) of a character model's TEX chunk, in the order its texture references number them"""
    tex = next((c for c in nbd.chunks(data) if c[0] == "TEX"), None)
    if tex is None:
        raise ValueError("the model has no TEX chunk")
    raw = bh_sld.unpack(data[tex[1] + 4:tex[1] + tex[2]])
    return [p for _, ps in tm2.files(raw) for p in ps]


def picture_png(game, key):
    """PNG bytes of a character's picture named "NAME#n" """
    name, _, n = key.rpartition("#")
    found = pictures(_read(game, name))
    if not 0 <= int(n) < len(found):
        raise IndexError(f"{key}: the model has {len(found)} pictures")
    buffer = io.BytesIO()
    found[int(n)].image().convert("RGBA").save(buffer, "PNG")
    return buffer.getvalue()
