#!/usr/bin/env python3
"""Publish the reviewed fork issue manifest; dry-run unless --publish is given."""
import argparse
import json
import os
from pathlib import Path
import urllib.request

REPOSITORY = 'jwhans0n/SporeCoopGA'


def entries(path):
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('repository') != REPOSITORY:
        raise ValueError('Issue publication is restricted to the user fork')
    rows = value['issues']
    keys = set()
    for row in rows:
        if not row.get('key') or row['key'] in keys or any(not isinstance(row.get(k), str) or not row[k].strip() for k in ('title', 'body', 'ticket')):
            raise ValueError('Incomplete or duplicate reviewed issue')
        keys.add(row['key'])
    return rows


def publish(rows, request):
    existing = []
    for page in range(1, 101):
        batch = request('GET', f'issues?state=all&per_page=100&page={page}')
        existing.extend(row for row in batch if 'pull_request' not in row)
        if len(batch) < 100:
            break
    else:
        raise RuntimeError('Issue inventory exceeds the reviewed pagination bound')
    results = []
    for row in rows:
        marker = '<!-- sporecoopga:' + row['key'] + ' -->'
        matches = [issue for issue in existing if marker in (issue.get('body') or '')]
        if len(matches) > 1:
            raise RuntimeError('Duplicate tracking marker: ' + row['key'])
        body = marker + '\n\n' + row['body']
        data = dict(title=row['title'], body=body)
        if matches:
            issue = matches[0]
            if issue['title'] != data['title'] or issue.get('body') != body:
                issue = request('PATCH', f"issues/{issue['number']}", data)
            # Preserve a maintainer's closed state. Never automatically reopen.
        else:
            issue = request('POST', 'issues', data)
        results.append(dict(ticket=row['ticket'], number=issue['number'], url=issue['html_url'], state=issue['state']))
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifest', type=Path)
    p.add_argument('--publish', action='store_true')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    rows = entries(args.manifest)
    if not args.publish:
        print(json.dumps(dict(repository=REPOSITORY, prepared=len(rows), network_used=False)))
        return
    token = os.environ.get('GH_TOKEN')
    if not token or os.environ.get('GITHUB_REPOSITORY', REPOSITORY) != REPOSITORY:
        raise RuntimeError('A fork-scoped issue-write token is required')

    def request(method, suffix, data=None):
        req = urllib.request.Request('https://api.github.com/repos/' + REPOSITORY + '/' + suffix,
            data=None if data is None else json.dumps(data).encode('utf-8'), method=method,
            headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
                     'Content-Type': 'application/json', 'User-Agent': 'SporeCoopGA-reviewed-issue-publisher'})
        with urllib.request.urlopen(req, timeout=30) as response:
            return json.load(response)

    result = publish(rows, request)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
