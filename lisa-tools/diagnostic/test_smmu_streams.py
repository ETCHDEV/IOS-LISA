import unittest
from smmu_streams import classify


class StreamTests(unittest.TestCase):
    def test_masked_target_and_shared_bank(self):
        entries = [
            {'smr': 0x80000080, 's2cr': 2},
            {'smr': 0x80000090, 's2cr': 2},
            {'smr': 0x80030080, 's2cr': 0x10000},
        ]
        out = classify({'id0': 3, 'id1': 4, 'entries': entries})
        self.assertEqual([x['index'] for x in out['matches']], [0, 2])
        self.assertEqual([x['index'] for x in out['other_entries_on_target_contexts']], [1])
        self.assertEqual(out['matches'][1]['mode'], 'bypass')

    def test_missing_and_invalid_entries_rejected(self):
        with self.assertRaises(ValueError):
            classify({'id0': 1, 'id1': 1, 'entries': []})
        with self.assertRaises(ValueError):
            classify({'id0': 1, 'id1': 1, 'entries': [{'smr': -1, 's2cr': 0}]})

    def test_unmatched_and_invalid_bank_are_visible(self):
        out = classify({'id0': 1, 'id1': 1,
                        'entries': [{'smr': 0x80000080, 's2cr': 7}]})
        self.assertFalse(out['matches'][0]['bank_in_range'])
        self.assertEqual(classify({'id0': 1, 'id1': 1,
                         'entries': [{'smr': 0x80, 's2cr': 0}]})['matches'], [])


if __name__ == '__main__':
    unittest.main()
