import unittest
from check_evidence import compare, Rejected

def sample(mode, movement, press):
    end_x = -20321275 if movement else -27262976
    end_y = 16779695 if movement else 16777216
    last_hash = "e85ea77b9b601676" if movement else "f9f5bbe0d9d331c3"
    line = (f"DOOM_EVIDENCE_V1 mode={mode} frames=10 level_frames=10 "
            f"player_frames=10 start_x=-27262976 start_y=16777216 "
            f"end_x={end_x} end_y={end_y} pressed={press} released={press} "
            f"consumed_down={press} consumed_up={press} "
            f"first_hash=6aa1d0936967b953 last_hash={last_hash}")
    return line + "\n"

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.a = sample("control",False,0)
        self.b = sample("intervention",True,1)
    def test_actual_pair(self):
        r=compare(self.a,self.b)
        self.assertEqual(r["player_displacement_fixed_units"]["dx"],6941701)
    def test_motion_only_in_control_rejected(self):
        with self.assertRaises(Rejected):compare(sample("control",True,0),self.b)
    def test_no_input_player_displacement_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,sample("intervention",False,1))
    def test_wrong_control_events_rejected(self):
        with self.assertRaises(Rejected):compare(sample("control",False,1),self.b)
    def test_no_input_events_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,sample("intervention",True,0))
    def test_extra_unknown_field_rejected(self):
        with self.assertRaises(Rejected):compare(self.a+"",self.b.replace(" frames=10"," extra=1 frames=10"))
    def test_duplicate_line_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b+self.b)
    def test_wrong_game_level_frames_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("level_frames=10","level_frames=9"))
    def test_wrong_start_position_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("start_x=-27262976","start_x=-27262975"))
    def test_wrong_start_image_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("6aa1d0936967b953","6aa1d0936967b954"))
    def test_static_intervention_image_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("e85ea77b9b601676","6aa1d0936967b953"))
    def test_malformed_hash_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("e85ea77b9b601676","not-hex"))
    def test_failed_adapter_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b+"WARDEN ADAPTER EXIT code 4\n")
    def test_duplicate_keys_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace(" pressed=1"," pressed=1 pressed=1"))
    def test_negative_counters_rejected(self):
        with self.assertRaises(Rejected):compare(self.a,self.b.replace("released=1","released=-1"))


    def reverse_sample(self):
        return (self.b.replace("mode=intervention", "mode=reverse")
                .replace("end_x=-20321275", "end_x=-32497008")
                .replace("end_y=16779695", "end_y=16775042")
                .replace("e85ea77b9b601676", "100bb942a9496fd4"))
    def test_three_directional_arms(self):
        r = compare(self.a, self.b, self.reverse_sample())
        self.assertTrue(r["opposite_direction_control_verified"])
        self.assertLess(r["reverse_displacement_fixed_units"]["dx"], 0)
    def test_reverse_missing_consumption_rejected(self):
        with self.assertRaises(Rejected):
            compare(self.a, self.b, self.reverse_sample().replace("consumed_up=1", "consumed_up=0"))
    def test_reverse_wrong_start_rejected(self):
        with self.assertRaises(Rejected):
            compare(self.a, self.b, self.reverse_sample().replace("start_y=16777216", "start_y=16777217"))
    def test_reverse_static_image_rejected(self):
        with self.assertRaises(Rejected):
            compare(self.a, self.b, self.reverse_sample().replace("100bb942a9496fd4", "6aa1d0936967b953"))
    def test_reverse_same_direction_rejected(self):
        with self.assertRaises(Rejected):
            compare(self.a, self.b, self.b.replace("mode=intervention", "mode=reverse"))
    def test_reverse_wrong_mode_rejected(self):
        with self.assertRaises(Rejected):
            compare(self.a, self.b, self.reverse_sample().replace("mode=reverse", "mode=control"))

if __name__ == "__main__":
    unittest.main()
