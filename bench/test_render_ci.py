import json
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

from bench import render_ci

SHAPES = {'wide': dict(files=20000, directories=1, needle_files=1819),
          'deep': dict(files=768, directories=97, needle_files=96),
          'mixed': dict(files=1452, directories=364, needle_files=242)}


def report(fd_ms=4.0, fynd_ms=2.0, cpu='CPU <A&B>'):
    medians = {}
    for shape in render_ci.SHAPES:
        for query in render_ci.QUERIES:
            for tool, ms in (('fynd', fynd_ms), ('fd', fd_ms), ('find', 3.0)):
                medians[f'{shape}-{query}-{tool}'] = {'elapsed_ns': ms * 1e6, 'peak_rss_kib': 1}
    return {'status': 'SUCCESS', 'medians': medians, 'fixture': {'shapes': SHAPES},
            'requested': {'repeats': 21, 'fynd_threads': 1}, 'cpu': cpu,
            'tools': {'fd': {'version': 'fd 9.0.0\n'},
                      'find': {'version': 'find (GNU findutils) 4.9.0\nCopyright'}}}


class RenderTest(unittest.TestCase):
    def test_rows_use_report_medians_and_fd_over_fynd_ratio(self):
        table = render_ci.rows(report(fd_ms=6.0, fynd_ms=2.0))
        self.assertEqual(len(table), 6)
        self.assertEqual([(r['shape'], r['query']) for r in table][:2],
                         [('wide', 'all'), ('wide', 'needle')])
        self.assertAlmostEqual(table[0]['ms']['fynd'], 2.0)
        self.assertAlmostEqual(table[0]['ratio'], 3.0)
        self.assertEqual(table[1]['selected'], 1819)

    def test_rejects_failed_report_and_missing_fd(self):
        failed = report()
        failed['status'] = 'FAILED'
        with self.assertRaises(ValueError):
            render_ci.rows(failed)
        missing = report()
        del missing['medians']['deep-needle-fd']
        with self.assertRaisesRegex(ValueError, 'deep-needle-fd'):
            render_ci.rows(missing)

    def test_verdict_names_direction_in_words(self):
        self.assertEqual(render_ci.verdict(2.0), ('2.00× faster', 'good'))
        self.assertEqual(render_ci.verdict(0.5), ('2.00× slower', 'muted'))

    def test_badge_uses_geometric_mean(self):
        table = render_ci.rows(report())
        table[0]['ratio'] = 8.0
        table[1]['ratio'] = 0.5
        for row in table[2:]:
            row['ratio'] = 1.0
        self.assertEqual(render_ci.badge(table)['message'], '1.26× faster (geomean)')
        for row in table:
            row['ratio'] = 0.8
        slower = render_ci.badge(table)
        self.assertEqual((slower['message'], slower['color']),
                         ('1.25× slower (geomean)', 'lightgrey'))
        self.assertEqual(slower['schemaVersion'], 1)

    def test_svg_is_wellformed_escaped_and_has_dark_mode(self):
        data = report()
        image = render_ci.svg(render_ci.rows(data), render_ci.meta(data, 'abcdef123456', 'Runner'))
        root = ElementTree.fromstring(image)
        texts = ''.join(root.itertext())
        self.assertIn('CPU <A&B>', texts)
        self.assertIn('abcdef1', texts)
        self.assertNotIn('Copyright', texts)
        self.assertIn('prefers-color-scheme: dark', image)
        self.assertEqual(texts.count('2.00× faster'), 6)

    def test_main_writes_three_outputs(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'report.json'
            source.write_text(json.dumps(report()))
            out = Path(scratch) / 'site'
            render_ci.main(['--report', str(source), '--out', str(out), '--commit', 'abc'])
            self.assertEqual(sorted(p.name for p in out.iterdir()),
                             ['badge.json', 'bench.svg', 'summary.md'])
            summary = (out / 'summary.md').read_text()
            self.assertIn('| wide: 20,000 files, 1 dir | all files | 2.00 | 4.00 | 3.00 |', summary)
            self.assertEqual(json.loads((out / 'badge.json').read_text())['color'], '2ea44f')


if __name__ == '__main__':
    unittest.main()
