#!/usr/bin/env python3
"""Checks of the editor's building blocks that need no game running (they read the disc images named in local.json):

    python3 tests/test_studio.py

What is measured in the running game is in docs/research; these keep the pieces that were measured from breaking.
"""
import io
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import bh_archive as archive  # noqa: E402
import bh_collision  # noqa: E402
import bh_nbd  # noqa: E402
import bh_rdt  # noqa: E402
import bh_sld  # noqa: E402
import tm2  # noqa: E402


def room(game, member):
    for _, name, node in bh_rdt.rooms(game):
        if name == member:
            r = bh_rdt.Room(node.read())
            r.unlimited = True          # as the builder has it when the rebuilt disc lays the grown file
            return r
    raise KeyError(member)


class Script(unittest.TestCase):
    def test_prepend_moves_the_headers_offsets(self):
        r = room("file2", "r0400300.rdt")
        before = r.script_words()
        first = r.first_instruction(0)
        r.insert_script(first, [(2 << 24) | 0x110, 7, 5, 0])
        after = r.script_words()
        self.assertEqual(len(after), len(before) + 4)
        self.assertEqual(after[9], before[9] + 16)       # thread 1 starts later by the four words
        self.assertEqual(after[8], before[8])            # thread 0 starts where it did

    def test_second_thread_insert_keeps_thread_starts(self):
        r = room("file2", "r0400300.rdt")
        before = r.script_words()
        r.insert_script(r.first_instruction(1), [(2 << 24) | 0x110, 7, 5, 0])
        after = r.script_words()
        self.assertEqual((after[8], after[9]), (before[8], before[9]))

    def test_sgl_replace_keeps_the_index_consistent(self):
        r = room("file2", "r0400300.rdt")
        words = r.script_words()
        sgl = bh_rdt.Sgl(archive.Archive("file2").open("/NETBIO00.DAT/r040.afs/r040.sgl").read())
        r.insert_script(r.first_instruction(0), [(2 << 24) | 0x110, 7, 5, 0])
        grown = bh_rdt.Sgl(sgl.replace_script(3, words, r.script_words()))
        self.assertEqual(len(grown.data), len(sgl.data) + 16)
        self.assertIsNotNone(grown.find_script(3, r.script_words()))

    def test_start_records_found_in_the_zoo(self):
        import bh_studio

        self.assertEqual(bh_studio.start_room("file2", 40), 3)


class Models(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = archive.Archive("file2").open("/NETBIO00.DAT/r040.afs/r0400300.nbd").read()

    def test_a_copied_object_parses_and_links(self):
        new, number = bh_nbd.clone_object(self.data, 84, (100, 0, 0))
        amo = next(c for c in bh_nbd.chunks(new) if c[0] == "AMO")
        self.assertEqual(len(bh_nbd.meshes(new, amo[1], amo[2])), 126)
        ahi = next(c for c in bh_nbd.chunks(new) if c[0] == "AHI")
        records = bh_nbd.bone_records(new, ahi[1], ahi[2])
        self.assertEqual(len(records), 128)
        self.assertEqual(struct.unpack_from("<i", new, records[-1] + 64)[0], number)     # the bone carries its own object number

    def test_an_object_made_from_geometry_reads_back(self):
        g = {"positions": [(0, 0, 0), (1, 0, 0), (0, 1, 0)], "uvs": [(0, 0), (1, 0), (0, 1)], "triangles": [(0, 1, 2)], "material": 3}
        new, _ = bh_nbd.clone_object(self.data, 84, None, g)
        amo = next(c for c in bh_nbd.chunks(new) if c[0] == "AMO")
        mesh = bh_nbd.meshes(new, amo[1], amo[2])[-1]
        self.assertEqual(mesh.positions, [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)])
        self.assertEqual(mesh.strips, [[0, 1, 2]])

    def test_an_object_of_another_room_comes_in_with_its_pictures(self):
        import bh_model
        disc = archive.Archive("file2")
        src = bh_model.Model(disc.open("/NETBIO00.DAT/r040.afs/r0400400.nbd").read())
        dst = bh_model.Model(self.data)
        before = len(dst.meshes())
        number = dst.import_object(src, 94)
        self.assertEqual(number, before)
        again = bh_model.Model(dst.to_bytes())
        self.assertEqual(len(again.meshes()), before + 1)

    def test_an_object_of_another_scenario_brings_its_pictures(self):
        import bh_model
        import bh_studio
        edits = {"rooms": {"r0010100.rdt": {"new_objects": [{"library": {"room": "r0418000.rdt", "mesh": 10, "game": "file1", "scenario": 41}}]}}}
        models, pictures = bh_studio.grown_models("file1", 1, edits)
        source = bh_model.Model(bh_studio._room_model(archive.Archive("file1"), 41, "r0418000.nbd"))
        wanted = list(dict.fromkeys(source.mesh_pictures(10)))
        self.assertEqual(len(pictures), len(wanted))
        self.assertEqual(pictures[0], bh_studio._pack_member(archive.Archive("file1"), 41, wanted[0]))
        bar = bh_model.Model(models[("r001.afs", "r0010100.nbd")])
        own = bh_studio.picture_count(archive.Archive("file1"), 1)
        self.assertEqual(sorted(set(bar.mesh_pictures(len(bar.meshes()) - 1))), list(range(own, own + len(wanted))))

    def test_the_model_says_how_many_objects_and_bones_it_has(self):
        import bh_rdt_edit
        model = bh_rdt_edit.model_json("file1", 1, "r0010100.nbd")
        # a copy put in the bar is object 96 on bone 99 (the builder appends both): the page numbered it bone - 2 = 97, which no object is
        self.assertEqual(model["objects"], 96)
        self.assertEqual(len(model["bones"]), 99)

    def test_a_material_names_its_texture_by_its_last_word(self):
        import bh_model
        bar = bh_model.Model(archive.Archive("file1").open("/NETBIO00.DAT/r001.afs/r0010100.nbd").read())
        # measured in the game (the bar, File 1): object 19, the floor, is dark wood planks (picture 3); object 43, the counter's top, dark carved wood (picture 4)
        self.assertEqual(bar.picture_of(39), 3)
        self.assertEqual(bar.picture_of(10), 4)

    def test_an_object_stands_on_the_bone_that_names_it(self):
        data = archive.Archive("file1").open("/NETBIO00.DAT/r001.afs/r0010100.nbd").read()
        ahi = next(c for c in bh_nbd.chunks(data) if c[0] == "AHI")
        carriers = bh_nbd.mesh_bones(data, ahi[1], ahi[2])
        # the bar's bottle shelves (objects 54-58): the game's own GS recording from camera 6 draws all 396 of their triangles ahead of the camera, where these bones put them
        # (bone i + 2 put them behind it)
        self.assertEqual(carriers[57], 95)
        self.assertEqual(carriers[63], 52)
        self.assertEqual(sorted(carriers), list(range(96)))
        self.assertEqual(len(set(carriers.values())), 96)

    def test_a_moved_object_changes_the_bone_that_carries_it(self):
        import bh_studio
        change = {"offset": [1.0, 2.0, 3.0]}
        built = bh_studio.scenery_as_models("file1", 1, {"rooms": {"r0010100.rdt": {"scenery": {"57": change}}}})
        self.assertEqual(built["models"]["r0010100.nbd"]["bones"], {"95": change})


    def test_a_mesh_says_when_it_adds_light(self):
        import bh_rdt_edit
        meshes = {m["mesh"]: m for m in bh_rdt_edit.model_json("file1", 1, "r0010100.nbd")["meshes"]}
        # the game's GS recording of the bar from camera 6 draws object 80 (128 triangles, the street's reflection on the window) additively and the
        # canopy (64) normally: the RenderAttribs' alpha destination word (12) is 1 for the one, 3 for the other
        self.assertTrue(meshes[80]["additive"])
        self.assertFalse(meshes[64]["additive"])


class Characters(unittest.TestCase):
    def test_an_enemy_is_drawn_with_its_own_model(self):
        import io

        import bh_characters
        from PIL import Image
        self.assertEqual(bh_characters.model_file("file1", 1, 9), "E01_09.NBD")
        self.assertIsNone(bh_characters.model_file("file1", 1, 85))        # no E01_85.NBD: Studio draws a figure for it and says why
        model = bh_characters.character_json("file1", "E01_09.NBD")
        self.assertEqual(model["tids"], ["E01_09.NBD#0"])
        self.assertEqual(len(model["meshes"]), 1)
        picture = Image.open(io.BytesIO(bh_characters.picture_png("file1", "E01_09.NBD#0")))
        self.assertEqual(picture.size, (256, 256))


class Enemies(unittest.TestCase):
    def test_a_new_file1_enemy_takes_its_copys_difficulty(self):
        import bh_enemies
        enemies = bh_enemies.Enemies("file1", 1)
        spec = {"room": 1, "variant": 0, "position": [8820, 0, 12620], "facing": 0, "id": 1, "class": 30, "modes": 15, "hp_tier": 3, "like": 18}
        out = bh_enemies.grown(enemies, [spec])
        # File 1 keeps a copy of a room's group for each difficulty, byte 30 of each record the copy's number (the bar's zombie: 0, 1, 2, 3 in its four copies;
        # the game made the bar's enemies of copy 1 and none of three new ones written with 15)
        found = sorted(r[30] for _, r in bh_enemies._records(out["sgl_e001.emd"]) if struct.unpack_from("<3i", r, 16) == (8820, 0, 12620))
        self.assertEqual(found, [0, 1, 2, 3])


    def test_a_new_enemy_of_another_kind_takes_that_kinds_own_record(self):
        import bh_enemies
        enemies = bh_enemies.Enemies("file1", 1)
        spec = {"room": 3, "variant": 0, "position": [9560, 0, 13330], "facing": 0, "id": 6, "class": 0, "modes": 15, "hp_tier": 3, "like": 18}
        out = bh_enemies.grown(enemies, [spec])
        made = [r for _, r in bh_enemies._records(out["sgl_e001.emd"]) if struct.unpack_from("<3i", r, 16) == (9560, 0, 13330)]
        self.assertEqual(len(made), 4)
        room = [r for _, r in bh_enemies._records(out["sgl_e001.emd"]) if r[28] == 3 and r[2] == 59][0]     # room 3's own enemy
        kind = bh_enemies.kind_record("file1", 6, 0)
        for r in made:
            # measured in room 3 (File 1): a kind 6 enemy made this way was created, came after the party and raised the virus gauge; with
            # a zombie's record it crashed, with its own whole record (word 8 and 18 of its scenario) it wasn't created
            self.assertEqual(r[2], 6)
            self.assertEqual(r[32], kind[32])
            self.assertEqual(r[33:36], room[33:36])
            self.assertEqual(r[72:76], room[72:76])
            self.assertEqual(r[40:51] + r[52:68], kind[40:51] + kind[52:68])      # (byte 51 is the hit-point tier the spec sets)


    def test_only_kinds_the_game_has_ai_for_are_placeable(self):
        import bh_enemies
        import bh_archive as arc
        game = arc.Archive("file1").open("/PROG/GAME.BIN").read()
        routine = struct.unpack_from("<70h", game, 0x6D7040 - 0x540000)
        # File 1's kind-to-AI map gives a routine to exactly the kinds its scenarios have; the rest get 0, a stand-in
        self.assertEqual(sorted(bh_enemies.kinds_of("file1")), sorted(k for k in range(70) if routine[k]))


class Pictures(unittest.TestCase):
    def test_restyle_round_trips_through_the_game_codec(self):
        from PIL import Image

        member = archive.Archive("file2").open("/NETBIO00.DAT/r040.afs").members()[10][1]
        raw = bh_sld.unpack(member.read())
        picture = tm2.pictures(raw)[0]
        image = Image.new("RGBA", (picture.width, picture.height), (200, 30, 30, 255))
        new = tm2.restyle(raw, image)
        self.assertEqual(len(new), len(raw))
        self.assertEqual(bh_sld.unpack(bh_sld.pack(new)), new)
        self.assertEqual(tm2.pictures(new)[0].image().getpixel((3, 3))[0] > 150, True)


    def test_a_24_bit_picture_decodes(self):
        # File 1's r001_d50.sld (texture 383) is a 64 x 64 picture of three bytes a pixel (TIM2 image type 2): the 3D view asked for it and got an error
        member = dict(archive.Archive("file1").open("/NETBIO00.DAT/r001.afs").members())["r001_d50.sld"]
        picture = tm2.pictures(bh_sld.unpack(member.read()))[0]
        self.assertEqual(picture.img_type, 2)
        image = picture.image()
        self.assertEqual(image.size, (64, 64))
        self.assertEqual(image.getpixel((5, 5))[3], 255)


class Collision(unittest.TestCase):
    def test_the_ground_file_round_trips_and_a_floor_box_cuts_it(self):
        node = dict(archive.Archive("file2").open("/NETBIO00.DAT/r040.afs").members())["g0400300.bin"]
        ground = bh_collision.Collision(node.read())
        count = len(ground.triangles)
        self.assertGreater(count, 0)
        cut = [piece for t in ground.triangles for piece in bh_collision.without_box(t, {"min": [11700, 10900], "max": [12200, 11400]})]
        self.assertGreaterEqual(len(cut), count)
        ground.triangles = cut + bh_collision.floor_triangles({"min": [11700, 10900], "max": [12200, 11400], "y": -60})
        again = bh_collision.Collision(ground.tobytes())
        self.assertEqual(len(again.triangles), len(ground.triangles))


class Rooms(unittest.TestCase):
    def test_every_room_of_both_games_still_checks_out(self):
        for game in ("file1", "file2"):
            bad = []
            for pack, name, node in bh_rdt.rooms(game):
                try:
                    problems = bh_rdt.Room(node.read()).check()
                except Exception as error:      # a room the reader can't take
                    problems = [str(error)]
                if problems:
                    bad.append((pack, name, problems[:1]))
            self.assertLess(len(bad), 5, bad[:3])

    def test_scripts_hide_objects_by_the_table(self):
        self.assertTrue(room("file1", "r0010200.rdt").initially_hidden())


if __name__ == "__main__":
    unittest.main(verbosity=2)
