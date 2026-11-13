"""Transport and restore tests through the narrow mock control boundary."""

import unittest

from tools.timeline_orchestrator.core import DesiredState, Timeline
from tools.timeline_orchestrator.control import (
    Baseline,
    ControlError,
    MockControlAPI,
    Orchestrator,
    StaleSceneError,
    TransportFrame,
)


def fixture():
    return {
        "version": 1,
        "duration": 12,
        "groups": {"set": ["prop.a", "prop.b"]},
        "tracks": [
            {
                "kind": "visibility", "group": "set",
                "keys": [
                    {"time": 0, "value": True},
                    {"time": 2, "value": False, "stagger": 1},
                    {"time": 8, "value": True, "stagger": 1},
                ],
            },
            {
                "kind": "background", "interpolation": "linear",
                "keys": [{"time": 0, "value": [0, 0, 0, 1]}, {"time": 6, "value": [1, 1, 1, 1]}],
            },
            {"kind": "camera_mode", "keys": [{"time": 0, "value": "Track"}, {"time": 5, "value": "Hold"}]},
        ],
    }


def baseline():
    return DesiredState(
        visibility={"prop.a": False, "prop.b": True, "prop.unowned": False},
        background=(0.2, 0.3, 0.4, 0.5),
        camera_mode="Follow",
    )


class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.timeline = Timeline.from_dict(fixture())
        self.original = baseline()
        self.control = MockControlAPI(self.original)
        self.runner = Orchestrator(self.timeline, self.control)

    def frame(self, time, status="playing", epoch="scene-1", loop=False, ready=True):
        return TransportFrame(time, status, epoch, loop=loop, scene_ready=ready)

    def assert_applied(self, desired):
        for object_id, visible in desired.visibility.items():
            self.assertEqual(self.control.state.visibility[object_id], visible)
        self.assertEqual(self.control.state.background, desired.background)
        self.assertEqual(self.control.state.camera_mode, desired.camera_mode)
        self.assertFalse(self.control.state.visibility["prop.unowned"])

    def test_start_at_arbitrary_time_captures_and_applies_directly(self):
        desired = self.runner.update(self.frame(4))
        self.assertEqual(desired, self.timeline.evaluate(4))
        self.assert_applied(desired)

    def test_pause_does_not_advance_time(self):
        self.runner.update(self.frame(2.5))
        expected = self.control.state
        for _ in range(20):
            self.runner.update(self.frame(2.5, "paused"))
        self.assertEqual(self.control.state, expected)

    def test_seek_while_paused_is_honored(self):
        self.runner.update(self.frame(7, "paused"))
        desired = self.runner.update(self.frame(2, "paused"))
        self.assertEqual(desired, self.timeline.evaluate(2))
        self.assert_applied(desired)

    def test_preview_is_absolute_and_stop_restores(self):
        self.runner.update(self.frame(7, "preview"))
        self.assert_applied(self.timeline.evaluate(7))
        self.runner.update(self.frame(0, "stopped"))
        self.assertEqual(self.control.state, self.original)

    def test_loop_wrap_and_backwards_seek_recompute_all_owned_states(self):
        self.runner.update(self.frame(11.99, loop=True))
        self.runner.update(self.frame(12, loop=True))
        self.assert_applied(self.timeline.evaluate(0))
        self.runner.update(self.frame(27, loop=True))
        self.assert_applied(self.timeline.evaluate(3))
        self.runner.update(self.frame(1, loop=True))
        self.assert_applied(self.timeline.evaluate(1))

    def test_stop_restores_captured_values_not_json_initials(self):
        self.runner.update(self.frame(4))
        self.runner.stop()
        self.assertEqual(self.control.state, self.original)

    def test_stop_restores_adapter_owned_background_mode(self):
        self.assertEqual(self.control.clear_flags, "Skybox")
        self.runner.update(self.frame(4))
        self.assertEqual(self.control.clear_flags, "SolidColor")
        self.runner.stop()
        self.assertEqual(self.control.clear_flags, "Skybox")

    def test_repeated_stop_and_initial_stopped_are_noops(self):
        self.runner.update(self.frame(0, "stopped"))
        self.assertEqual(self.control.state, self.original)
        self.assertEqual(self.control.history, [])
        self.runner.update(self.frame(4))
        self.runner.stop()
        history_size = len(self.control.history)
        self.runner.stop()
        self.runner.update(self.frame(0, "stopped"))
        self.assertEqual(len(self.control.history), history_size)

    def test_restart_captures_the_new_user_baseline(self):
        self.runner.update(self.frame(4))
        self.runner.stop()
        changed = DesiredState(
            visibility={"prop.a": True, "prop.b": False, "prop.unowned": False},
            background=(0.9, 0.2, 0.1, 1), camera_mode="Hold",
        )
        self.control.apply(changed, "scene-1")
        self.runner.update(self.frame(4))
        self.runner.stop()
        self.assertEqual(self.control.state, changed)

    def test_explicit_suspend_restores_then_resumes_without_history_replay(self):
        self.runner.update(self.frame(4))
        self.runner.suspend()
        self.assertEqual(self.control.state, self.original)
        self.runner.update(self.frame(7))
        self.assert_applied(self.timeline.evaluate(7))
        self.runner.stop()
        self.assertEqual(self.control.state, self.original)

    def test_loading_frame_restores_and_writes_no_timeline_state(self):
        self.runner.update(self.frame(4))
        self.assertIsNone(self.runner.update(self.frame(7, ready=False)))
        self.assertEqual(self.control.state, self.original)
        history_size = len(self.control.history)
        self.runner.update(self.frame(9, ready=False))
        self.assertEqual(len(self.control.history), history_size)
        self.runner.update(self.frame(9))
        self.assert_applied(self.timeline.evaluate(9))

    def test_scene_rebind_captures_its_own_baseline_at_current_time(self):
        self.runner.update(self.frame(4))
        new_scene = DesiredState(
            visibility={"prop.a": True, "prop.b": False, "prop.unowned": False},
            background=(0.8, 0.7, 0.6, 1), camera_mode="Track",
        )
        self.control.rebind(new_scene, "scene-2")
        self.runner.update(self.frame(8.5, epoch="scene-2"))
        self.assert_applied(self.timeline.evaluate(8.5))
        self.runner.stop()
        self.assertEqual(self.control.state, new_scene)

    def test_stop_after_unannounced_epoch_change_never_restores_old_objects(self):
        self.runner.update(self.frame(4))
        new_scene = DesiredState(
            visibility={"prop.a": True, "prop.b": False, "prop.unowned": False},
            background=(0.8, 0.7, 0.6, 1), camera_mode="Track",
        )
        self.control.rebind(new_scene, "scene-2")
        self.runner.stop()
        self.assertEqual(self.control.state, new_scene)

    def test_visibility_only_timeline_does_not_own_other_controls(self):
        document = fixture()
        document["tracks"] = document["tracks"][:1]
        runner = Orchestrator(Timeline.from_dict(document), self.control)
        runner.update(self.frame(4))
        self.assertEqual(self.control.state.background, self.original.background)
        self.assertEqual(self.control.state.camera_mode, self.original.camera_mode)
        runner.stop()
        self.assertEqual(self.control.state, self.original)

    def test_invalid_transport_does_not_touch_control(self):
        bad_frames = [
            lambda: self.frame(float("nan")), lambda: self.frame(float("inf")), lambda: self.frame(True),
            lambda: self.frame(2, "typo"), lambda: self.frame(2, epoch=""),
            lambda: self.frame(2, loop=1), lambda: self.frame(2, ready=1),
        ]
        for index, create_frame in enumerate(bad_frames):
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.runner.update(create_frame())
        self.assertEqual(self.control.state, self.original)
        self.assertEqual(self.control.history, [])

    def test_apply_failure_retains_restore_token(self):
        class FailApply(MockControlAPI):
            def apply(self, state, scene_epoch):
                raise ControlError("simulated host write refusal")

        control = FailApply(self.original)
        runner = Orchestrator(self.timeline, control)
        with self.assertRaises(ControlError):
            runner.update(self.frame(4))
        runner.stop()
        self.assertEqual(control.state, self.original)
        self.assertEqual([entry["operation"] for entry in control.history], ["capture", "restore"])

    def test_restore_failure_keeps_token_for_retry(self):
        class FailOnceRestore(MockControlAPI):
            fail_next_restore = True

            def restore(self, captured):
                if self.fail_next_restore:
                    self.fail_next_restore = False
                    raise ControlError("simulated transient restore refusal")
                super().restore(captured)

        control = FailOnceRestore(self.original)
        runner = Orchestrator(self.timeline, control)
        runner.update(self.frame(4))
        with self.assertRaises(ControlError):
            runner.stop()
        self.assertNotEqual(control.state, self.original)
        runner.stop()
        self.assertEqual(control.state, self.original)
        self.assertEqual(control.clear_flags, "Skybox")
        self.assertEqual(sum(entry["operation"] == "restore" for entry in control.history), 1)

    def test_epoch_change_between_capture_and_apply_cannot_write_stale_objects(self):
        new_scene = DesiredState(
            visibility={"prop.a": True, "prop.b": False, "prop.unowned": False},
            background=(0.8, 0.7, 0.6, 1), camera_mode="Track",
        )

        class SwitchDuringCapture(MockControlAPI):
            def capture(self, scope, scene_epoch):
                captured = super().capture(scope, scene_epoch)
                if scene_epoch == "scene-1":
                    self.rebind(new_scene, "scene-2", clear_flags="DepthOnly")
                return captured

        control = SwitchDuringCapture(self.original)
        runner = Orchestrator(self.timeline, control)
        with self.assertRaises(StaleSceneError):
            runner.update(self.frame(4))
        self.assertEqual(control.state, new_scene)
        runner.stop()
        self.assertEqual(control.state, new_scene)
        runner.update(self.frame(7, epoch="scene-2"))
        runner.stop()
        self.assertEqual(control.state, new_scene)
        self.assertEqual(control.clear_flags, "DepthOnly")

    def test_capture_returning_the_wrong_epoch_never_reaches_apply(self):
        class WrongEpochCapture(MockControlAPI):
            def capture(self, scope, scene_epoch):
                captured = super().capture(scope, scene_epoch)
                return Baseline("scene-wrong", captured.state, captured.opaque)

        control = WrongEpochCapture(self.original)
        runner = Orchestrator(self.timeline, control)
        with self.assertRaises(StaleSceneError):
            runner.update(self.frame(4))
        self.assertEqual(control.state, self.original)
        self.assertEqual([entry["operation"] for entry in control.history], ["capture"])


class ControlBoundaryTests(unittest.TestCase):
    def test_unknown_timeline_object_fails_before_any_write(self):
        document = fixture()
        document["groups"]["set"].append("prop.unknown")
        control = MockControlAPI(baseline())
        runner = Orchestrator(Timeline.from_dict(document), control)
        with self.assertRaises(ControlError):
            runner.update(TransportFrame(4, "playing", "scene-1"))
        self.assertEqual(control.state, baseline())
        self.assertEqual(control.history, [])

    def test_unknown_object_cannot_partially_apply_known_changes(self):
        control = MockControlAPI(baseline())
        invalid = DesiredState(
            visibility={"prop.a": True, "prop.unknown": False},
            background=(1, 1, 1, 1), camera_mode="Track",
        )
        with self.assertRaises(ControlError):
            control.apply(invalid, "scene-1")
        self.assertEqual(control.state, baseline())
        self.assertEqual(control.history, [])

    def test_stale_capture_apply_restore_are_rejected_without_writes(self):
        control = MockControlAPI(baseline())
        scope = Timeline.from_dict(fixture()).scope
        captured = control.capture(scope, "scene-1")
        control.rebind(baseline(), "scene-2")
        history_size = len(control.history)
        for callback in [
            lambda: control.capture(scope, "scene-1"),
            lambda: control.apply(baseline(), "scene-1"),
            lambda: control.restore(captured),
        ]:
            with self.assertRaises(StaleSceneError):
                callback()
        self.assertEqual(control.state, baseline())
        self.assertEqual(len(control.history), history_size)

    def test_unsupported_camera_scope_is_rejected_during_capture(self):
        original = DesiredState(visibility={"prop.a": True, "prop.b": True}, background=(0, 0, 0, 1), camera_mode=None)
        control = MockControlAPI(original)
        runner = Orchestrator(Timeline.from_dict(fixture()), control)
        with self.assertRaises(ControlError):
            runner.update(TransportFrame(4, "playing", "scene-1"))
        self.assertEqual(control.state, original)
        self.assertEqual(control.history, [])

    def test_unsupported_background_scope_is_rejected_during_capture(self):
        original = DesiredState(visibility={"prop.a": True, "prop.b": True}, background=None, camera_mode="Track")
        control = MockControlAPI(original)
        runner = Orchestrator(Timeline.from_dict(fixture()), control)
        with self.assertRaises(ControlError):
            runner.update(TransportFrame(4, "playing", "scene-1"))
        self.assertEqual(control.state, original)
        self.assertEqual(control.history, [])

    def test_rebind_requires_fresh_epoch_before_replacing_state(self):
        control = MockControlAPI(baseline())
        with self.assertRaises(ControlError):
            control.rebind(DesiredState({"prop.new": True}), "scene-1")
        self.assertEqual(control.state, baseline())
        self.assertEqual(control.history, [])

    def test_rebind_cannot_reuse_an_older_epoch(self):
        control = MockControlAPI(baseline())
        control.rebind(baseline(), "scene-2")
        control.rebind(baseline(), "scene-3")
        for old_epoch in ["scene-1", "scene-2"]:
            with self.subTest(epoch=old_epoch), self.assertRaises(ControlError):
                control.rebind(DesiredState({"prop.new": True}), old_epoch)
        self.assertEqual(control.scene_epoch, "scene-3")
        self.assertEqual(control.state, baseline())

    def test_corrupt_restore_token_cannot_partially_change_state(self):
        control = MockControlAPI(baseline())
        timeline = Timeline.from_dict(fixture())
        captured = control.capture(timeline.scope, "scene-1")
        control.apply(timeline.evaluate(4), "scene-1")
        before = control.state
        history_size = len(control.history)
        for opaque in [None, {}, {"clear_flags": 1}]:
            with self.subTest(opaque=opaque), self.assertRaises(ControlError):
                control.restore(Baseline("scene-1", captured.state, opaque))
            self.assertEqual(control.state, before)
            self.assertEqual(control.clear_flags, "SolidColor")
            self.assertEqual(len(control.history), history_size)
        control.restore(captured)
        self.assertEqual(control.state, baseline())

    def test_invalid_state_camera_mode_has_explicit_validation_error(self):
        for mode in [[], {}, 1, "game"]:
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                DesiredState({}, camera_mode=mode)

    def test_capture_contains_only_owned_properties(self):
        document = fixture()
        document["tracks"] = document["tracks"][:1]
        control = MockControlAPI(baseline())
        captured = control.capture(Timeline.from_dict(document).scope, "scene-1")
        self.assertEqual(captured.scene_epoch, "scene-1")
        self.assertEqual(captured.state.visibility, {"prop.a": False, "prop.b": True})
        self.assertIsNone(captured.state.background)
        self.assertIsNone(captured.state.camera_mode)


if __name__ == "__main__":
    unittest.main()
