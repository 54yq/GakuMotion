"""Behavioral checks for the history-independent timeline evaluator."""

from copy import deepcopy
import json
from pathlib import Path
import random
import tempfile
import unittest

from tools.timeline_orchestrator.core import Timeline


def fixture():
    return {
        "version": 1,
        "duration": 16,
        "groups": {"set": ["prop.a", "prop.b", "prop.c"]},
        "tracks": [
            {
                "kind": "visibility",
                "group": "set",
                "keys": [
                    {"time": 0, "value": True},
                    {"time": 2, "value": False, "stagger": 0.5},
                    {"time": 10, "value": True, "stagger": 0.5, "order": "reverse"},
                ],
            },
            {
                "kind": "background",
                "interpolation": "linear",
                "keys": [
                    {"time": 0, "value": [0, 0.2, 0.4, 1]},
                    {"time": 8, "value": [1, 0.6, 0, 0.5]},
                ],
            },
            {
                "kind": "camera_mode",
                "keys": [{"time": 0, "value": "Track"}, {"time": 6, "value": "Hold"}],
            },
        ],
    }


class StateEvaluationTests(unittest.TestCase):
    def setUp(self):
        self.timeline = Timeline.from_dict(fixture())

    def visibility_at(self, time, expected, *, loop=False):
        actual = self.timeline.evaluate(time, loop=loop).visibility
        self.assertEqual(actual, dict(zip(["prop.a", "prop.b", "prop.c"], expected)))

    def test_staggered_hide_has_exact_boundaries(self):
        for time, expected in [
            (0, [True, True, True]),
            (1.999999, [True, True, True]),
            (2, [False, True, True]),
            (2.499999, [False, True, True]),
            (2.5, [False, False, True]),
            (3, [False, False, False]),
        ]:
            with self.subTest(time=time):
                self.visibility_at(time, expected)

    def test_reverse_staggered_restore_has_exact_boundaries(self):
        for time, expected in [
            (9.999999, [False, False, False]),
            (10, [False, False, True]),
            (10.5, [False, True, True]),
            (11, [True, True, True]),
        ]:
            with self.subTest(time=time):
                self.visibility_at(time, expected)

    def test_direct_seek_does_not_need_initial_evaluation(self):
        direct = Timeline.from_dict(fixture()).evaluate(2.75)
        self.assertEqual(direct.visibility, {"prop.a": False, "prop.b": False, "prop.c": True})
        self.assertEqual(direct.camera_mode, "Track")
        self.assertAlmostEqual(direct.background[0], 2.75 / 8)

    def test_backwards_seek_restores_state(self):
        self.timeline.evaluate(15)
        self.timeline.evaluate(10.5)
        self.visibility_at(2, [False, True, True])
        self.visibility_at(0, [True, True, True])

    def test_random_seek_matches_an_independent_expected_formula(self):
        rng = random.Random(20261104)
        times = [0, 2, 2.5, 3, 6, 8, 10, 10.5, 11, 16]
        times += [rng.uniform(-4, 40) for _ in range(200)]
        rng.shuffle(times)
        for raw_time in times:
            time = min(16, max(0, raw_time))
            expected = {}
            for index, object_id in enumerate(["prop.a", "prop.b", "prop.c"]):
                expected[object_id] = time < 2 + index * 0.5 or time >= 10 + (2 - index) * 0.5
            state = self.timeline.evaluate(raw_time)
            with self.subTest(time=raw_time):
                self.assertEqual(state.visibility, expected)
                self.assertEqual(state.camera_mode, "Hold" if time >= 6 else "Track")
                fraction = min(1, time / 8)
                for actual, start, end in zip(state.background, [0, 0.2, 0.4, 1], [1, 0.6, 0, 0.5]):
                    self.assertAlmostEqual(actual, start + (end - start) * fraction)

    def test_linear_color_interpolation_and_postroll(self):
        self.assertEqual(self.timeline.evaluate(0).background, (0, 0.2, 0.4, 1))
        for actual, expected in zip(self.timeline.evaluate(4).background, (0.5, 0.4, 0.2, 0.75)):
            self.assertAlmostEqual(actual, expected)
        self.assertEqual(self.timeline.evaluate(8).background, (1, 0.6, 0, 0.5))
        self.assertEqual(self.timeline.evaluate(16).background, (1, 0.6, 0, 0.5))

    def test_color_defaults_to_step(self):
        document = fixture()
        del document["tracks"][1]["interpolation"]
        timeline = Timeline.from_dict(document)
        self.assertEqual(timeline.evaluate(7.99).background, (0, 0.2, 0.4, 1))
        self.assertEqual(timeline.evaluate(8).background, (1, 0.6, 0, 0.5))

    def test_camera_mode_is_state(self):
        self.assertEqual(self.timeline.evaluate(6).camera_mode, "Hold")
        self.assertEqual(self.timeline.evaluate(5.99).camera_mode, "Track")

    def test_loop_boundary_and_large_jumps_are_absolute(self):
        self.assertEqual(self.timeline.evaluate(16, loop=True), self.timeline.evaluate(0))
        self.assertEqual(self.timeline.evaluate(32, loop=True), self.timeline.evaluate(0))
        self.assertEqual(self.timeline.evaluate(34.75, loop=True), self.timeline.evaluate(2.75))
        self.assertEqual(self.timeline.evaluate(1600002.75, loop=True), self.timeline.evaluate(2.75))
        self.assertEqual(self.timeline.evaluate(16), self.timeline.evaluate(100000))

    def test_negative_time_clamps_before_looping(self):
        for loop in (False, True):
            self.assertEqual(self.timeline.evaluate(-1, loop=loop), self.timeline.evaluate(0))

    def test_single_object_visibility_and_optional_tracks(self):
        timeline = Timeline.from_dict({
            "version": 1, "duration": 3,
            "tracks": [{"kind": "visibility", "object": "prop.a", "keys": [{"time": 0, "value": False}]}],
        })
        state = timeline.evaluate(2)
        self.assertEqual(state.visibility, {"prop.a": False})
        self.assertIsNone(state.background)
        self.assertIsNone(state.camera_mode)

    def test_compiled_timeline_is_independent_of_mutable_input(self):
        document = fixture()
        timeline = Timeline.from_dict(document)
        document["tracks"][0]["keys"][1]["value"] = True
        document["groups"]["set"].append("prop.injected")
        self.assertEqual(timeline.evaluate(3).visibility, {"prop.a": False, "prop.b": False, "prop.c": False})

    def test_evaluation_results_cannot_mutate_the_compiled_timeline(self):
        state = self.timeline.evaluate(3)
        try:
            state.visibility["prop.a"] = True
        except TypeError:
            pass
        self.assertFalse(self.timeline.evaluate(3).visibility["prop.a"])

    def test_json_load_matches_from_dict(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "timeline.json"
            path.write_text(json.dumps(fixture()), encoding="utf-8")
            loaded = Timeline.load(path)
            self.assertEqual(loaded.evaluate(10.5), self.timeline.evaluate(10.5))

    def test_invalid_evaluation_time_is_rejected(self):
        for time in [float("nan"), float("inf"), -float("inf"), True, "2", None]:
            with self.subTest(time=time), self.assertRaises(ValueError):
                self.timeline.evaluate(time)


class FormatValidationTests(unittest.TestCase):
    def reject(self, mutate):
        document = fixture()
        mutate(document)
        with self.assertRaises(ValueError):
            Timeline.from_dict(document)

    def test_version_is_exactly_supported_integer(self):
        for value in [0, 2, "1", True, 1.5]:
            with self.subTest(value=value):
                self.reject(lambda doc: doc.update(version=value))

    def test_duration_is_positive_finite_number(self):
        for value in [0, -1, float("nan"), float("inf"), True, "16", None]:
            with self.subTest(value=value):
                self.reject(lambda doc: doc.update(duration=value))

    def test_unknown_fields_are_rejected(self):
        mutations = [
            lambda doc: doc.update(typo=True),
            lambda doc: doc["tracks"][0].update(typo=True),
            lambda doc: doc["tracks"][0]["keys"][1].update(typo=True),
            lambda doc: doc["tracks"][1]["keys"][1].update(stagger=1),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.reject(mutate)

    def test_unknown_track_kind_is_rejected(self):
        self.reject(lambda doc: doc["tracks"][0].update(kind="event"))

    def test_group_has_unique_nonempty_object_ids(self):
        for members in [[], [""], ["prop.a", "prop.a"], "prop.a", [1], [None]]:
            with self.subTest(members=members):
                self.reject(lambda doc: doc["groups"].update(set=members))

    def test_visibility_requires_exactly_one_valid_target(self):
        mutations = [
            lambda doc: doc["tracks"][0].update(object="prop.other"),
            lambda doc: doc["tracks"][0].pop("group"),
            lambda doc: doc["tracks"][0].update(group="missing"),
            lambda doc: doc["tracks"][0].update(group=""),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.reject(mutate)

    def test_duplicate_owned_property_is_rejected(self):
        for index in (0, 1, 2):
            with self.subTest(index=index):
                self.reject(lambda doc: doc["tracks"].append(deepcopy(doc["tracks"][index])))
        self.reject(lambda doc: doc["tracks"].append({
            "kind": "visibility", "object": "prop.a", "keys": [{"time": 0, "value": False}],
        }))

    def test_every_track_needs_a_zero_time_initial_state(self):
        for index in (0, 1, 2):
            with self.subTest(index=index):
                self.reject(lambda doc: doc["tracks"][index]["keys"].pop(0))
                self.reject(lambda doc: doc["tracks"][index].update(keys=[]))
        self.reject(lambda doc: doc["tracks"][0]["keys"][0].update(stagger=1))

    def test_time_keys_are_finite_sorted_unique_and_in_duration(self):
        for value in [-1, 0, 12, 17, float("nan"), float("inf"), True, "2"]:
            with self.subTest(value=value):
                self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(time=value))

    def test_expanded_stagger_must_fit_duration_and_remain_ordered(self):
        self.reject(lambda doc: doc["tracks"][0]["keys"][2].update(time=15.5))
        self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(stagger=9))
        for value in [-0.1, True, "0.5", float("nan"), float("inf")]:
            with self.subTest(value=value):
                self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(stagger=value))

    def test_expanded_stagger_cannot_collide_or_cross_next_instruction(self):
        # c hides at 10 (collision) or 11 (crossing), while the later reverse
        # restore instruction would already show c at 10. Both fit duration.
        for stagger in [4, 4.5]:
            with self.subTest(stagger=stagger):
                self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(stagger=stagger))

    def test_visibility_values_are_boolean(self):
        for value in [0, 1, "false", None, []]:
            with self.subTest(value=value):
                self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(value=value))

    def test_order_has_only_two_spellings(self):
        self.reject(lambda doc: doc["tracks"][0]["keys"][1].update(order="backwards"))

    def test_color_is_normalized_rgba(self):
        for color in [[1, 1, 1], [1, 1, 1, 1, 1], [-0.1, 0, 0, 1], [1.1, 0, 0, 1],
                      [True, 0, 0, 1], [float("nan"), 0, 0, 1], "#ffffff", None]:
            with self.subTest(color=color):
                self.reject(lambda doc: doc["tracks"][1]["keys"][1].update(value=color))

    def test_interpolation_is_supported_only_on_color(self):
        self.reject(lambda doc: doc["tracks"][1].update(interpolation="cubic"))
        self.reject(lambda doc: doc["tracks"][0].update(interpolation="linear"))

    def test_missing_required_root_fields(self):
        for field in ["version", "duration", "tracks"]:
            with self.subTest(field=field):
                self.reject(lambda doc: doc.pop(field))

    def test_json_duplicate_fields_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"version":1,"version":1,"duration":1,"tracks":[]}', encoding="utf-8")
            with self.assertRaises(ValueError):
                Timeline.load(path)


if __name__ == "__main__":
    unittest.main()
