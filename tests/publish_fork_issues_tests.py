import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('publisher', Path(__file__).resolve().parents[1] / 'tools/publish_fork_issues.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class Publication(unittest.TestCase):
    def row(self):
        return dict(key='owned', ticket='BUG-1025', title='Reviewed title', body='Reviewed body')

    def test_foreign_repository_duplicate_and_incomplete_input_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'issues.json'
            for value in [dict(repository='WoodyWizard/SporeCoop', issues=[self.row()]),
                          dict(repository=p.REPOSITORY, issues=[self.row(), self.row()]),
                          dict(repository=p.REPOSITORY, issues=[dict(self.row(), body='')])]:
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    p.entries(path)

    def test_creation_then_idempotent_retry(self):
        issues, calls = [], []
        def request(method, suffix, data=None):
            calls.append((method, suffix))
            if method == 'GET':
                return issues
            self.assertEqual((method, suffix), ('POST', 'issues'))
            issues.append(dict(data, number=1, html_url='https://github.com/'+p.REPOSITORY+'/issues/1', state='open'))
            return issues[-1]
        first = p.publish([self.row()], request)
        self.assertEqual(first, p.publish([self.row()], request))
        self.assertEqual(sum(method == 'POST' for method, _ in calls), 1)

    def test_update_preserves_closed_state(self):
        old = dict(number=7, title='Old', body='<!-- sporecoopga:owned -->', state='closed', html_url='owned')
        def request(method, suffix, data=None):
            if method == 'GET':
                return [old]
            self.assertEqual((method, suffix), ('PATCH', 'issues/7'))
            self.assertNotIn('state', data)
            return dict(old, **data)
        self.assertEqual(p.publish([self.row()], request)[0]['state'], 'closed')

    def test_duplicate_remote_marker_stops_before_writing(self):
        def request(method, suffix, data=None):
            self.assertEqual(method, 'GET')
            return [dict(body='<!-- sporecoopga:owned -->')] * 2
        with self.assertRaises(RuntimeError):
            p.publish([self.row()], request)


if __name__ == '__main__':
    unittest.main()
