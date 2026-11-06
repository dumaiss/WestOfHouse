import unittest
from pathlib import Path

from extract_scenes import SceneExtractor, validate_document


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = PROJECT_ROOT / "vendor" / "infocom-zork1"
SCHEMA = PROJECT_ROOT / "schemas" / "scene-facts-v1.schema.json"


class SceneExtractorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.extractor = SceneExtractor(SOURCE_ROOT)

    def test_discovers_all_rooms(self):
        rooms = self.extractor.room_names()
        self.assertEqual(110, len(rooms))
        self.assertIn("SANDY-CAVE", rooms)
        self.assertIn("MAINTENANCE-ROOM", rooms)

    def test_sandy_cave_counter_states_and_dependency(self):
        document = self.extractor.extract("SANDY-CAVE")
        scene = document["scene"]
        self.assertEqual({"SAND", "SCARAB"}, {obj["id"] for obj in scene["objects"]})
        self.assertIn("SHOVEL", {item["id"] for item in scene["dependencies"]})
        state_ids = {state["id"] for state in scene["states"]}
        self.assertTrue(
            {
                "BEACH-DIG-STATE-0",
                "BEACH-DIG-STATE-1",
                "BEACH-DIG-STATE-2",
            }.issubset(state_ids)
        )
        self.assertTrue(
            any(
                transition["terminal"]
                and "The hole collapses, smothering you."
                in transition["output_text"]
                for transition in scene["transitions"]
            )
        )

    def test_maintenance_room_follows_flood_event(self):
        scene = self.extractor.extract("MAINTENANCE-ROOM")["scene"]
        object_ids = {obj["id"] for obj in scene["objects"]}
        self.assertIn("SCREWDRIVER", object_ids)
        self.assertIn("LEAK", object_ids)
        self.assertIn("I-MAINT-ROOM", {item["id"] for item in scene["dependencies"]})
        self.assertTrue(
            any(
                transition["trigger"].get("event") == "I-MAINT-ROOM"
                for transition in scene["transitions"]
            )
        )

    def test_dam_look_states_are_expanded(self):
        scene = self.extractor.extract("DAM-ROOM")["scene"]
        self.assertEqual(8, len(scene["descriptions"]))
        description_text = " ".join(
            fragment
            for description in scene["descriptions"]
            for fragment in description["text"]
        )
        self.assertIn("sluice gates are open", description_text)
        self.assertIn("glowing serenely", description_text)

    def test_every_room_matches_schema(self):
        for room_id in self.extractor.room_names():
            with self.subTest(room_id=room_id):
                validate_document(self.extractor.extract(room_id), SCHEMA)


if __name__ == "__main__":
    unittest.main()
