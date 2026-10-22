#!/usr/bin/env python3
"""Render bench.py reports as a README table image, a badge and a Markdown summary.

Inputs are SUCCESS reports from bench/bench.py with fd present: one run with a
single thread per tool and, optionally, one run with --defaults (each tool's own
default thread count). Outputs are bench.svg (light/dark table), badge.json
(shields.io endpoint) and summary.md. Values are the reports' own medians;
nothing is re-measured or smoothed.
"""
import argparse
import json
import math
from pathlib import Path
from xml.sax.saxutils import escape

SHAPES = ('wide', 'deep', 'mixed')
QUERIES = ('all', 'needle')
TOOLS = ('fynd', 'fd', 'find')
MODES = (('single', 'One thread'), ('default', 'Default threads'))


def first_line(text):
    return (text or '').strip().splitlines()[0] if (text or '').strip() else 'unknown'


def medians(report):
    """Milliseconds per (shape, query, tool) from one SUCCESS report."""
    if report.get('status') != 'SUCCESS':
        raise ValueError('report status is not SUCCESS')
    result = {}
    for shape in SHAPES:
        for query in QUERIES:
            for tool in TOOLS:
                label = f'{shape}-{query}-{tool}'
                if label not in report['medians']:
                    raise ValueError(f'missing median: {label}')
                ms = report['medians'][label]['elapsed_ns'] / 1e6
                if ms <= 0:
                    raise ValueError(f'non-positive median: {label}')
                result[shape, query, tool] = ms
    return result


def rows(single, defaults=None):
    """One row per shape/query; each mode holds fynd/fd/find ms and fd/fynd ratio."""
    modes = {'single': medians(single)}
    if defaults is not None:
        modes['default'] = medians(defaults)
    shapes = single['fixture']['shapes']
    result = []
    for shape in SHAPES:
        for query in QUERIES:
            info = shapes[shape]
            row = dict(shape=shape, query=query, files=info['files'],
                       directories=info['directories'],
                       selected=info['files'] if query == 'all' else info['needle_files'])
            for mode, ms in modes.items():
                cell = {tool: ms[shape, query, tool] for tool in TOOLS}
                cell['ratio'] = cell['fd'] / cell['fynd']
                row[mode] = cell
            result.append(row)
    return result


def modes_of(table):
    return [(key, title) for key, title in MODES if key in table[0]]


def geomean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values))


def count(number, noun):
    return f'{number:,} {noun}' + ('' if number == 1 else 's')


def describe(row):
    tree = f"{row['shape']}: {count(row['files'], 'file')}, {count(row['directories'], 'dir')}"
    query = 'all files' if row['query'] == 'all' else f"name has 'needle' ({row['selected']:,})"
    return tree, query


def verdict(ratio):
    if ratio >= 1:
        return f'{ratio:.2f}× faster', 'good'
    return f'{1 / ratio:.2f}× slower', 'muted'


def meta(single, commit, runner):
    tools = single['tools']
    return dict(repeats=single['requested']['repeats'],
                fd=first_line(tools['fd']['version']),
                find=first_line(tools['find']['version']),
                cpu=single.get('cpu') or 'unknown CPU',
                commit=commit[:7], runner=runner)


def badge(table):
    """Summarize defaults against defaults when measured, else one thread each."""
    key, title = modes_of(table)[-1]
    message, tone = verdict(geomean([row[key]['ratio'] for row in table]))
    label = 'fynd vs fd (defaults)' if key == 'default' else 'fynd vs fd (1 thread)'
    return {'schemaVersion': 1, 'label': label, 'message': message + ' (geomean)',
            'color': '2ea44f' if tone == 'good' else 'lightgrey'}


def markdown(table, info):
    modes = modes_of(table)
    head = ['Tree', 'Query']
    for _, title in modes:
        head += [f'{title}: fynd (ms)', 'fd (ms)', 'find (ms)', 'fynd vs fd']
    lines = ['| ' + ' | '.join(head) + ' |',
             '| --- | --- |' + ' ---: | ---: | ---: | ---: |' * len(modes)]
    for row in table:
        cells = list(describe(row))
        for key, _ in modes:
            cell = row[key]
            cells += [f"{cell['fynd']:.2f}", f"{cell['fd']:.2f}", f"{cell['find']:.2f}",
                      verdict(cell['ratio'])[0]]
        lines.append('| ' + ' | '.join(cells) + ' |')
    lines += ['', footer(info, modes), '']
    return '\n'.join(lines)


def footer(info, modes):
    text = (f"Median wall time of {info['repeats']} warm runs, process startup included. "
            f"fynd at {info['commit']}; {info['fd']}; {info['find']}. "
            'One thread: fynd -j1, fd --threads 1. ')
    if len(modes) > 1:
        text += ('Default threads: no thread option, so fynd uses the online CPU count '
                 'and fd its own automatic count. ')
    return text + (f"{info['runner']}, {info['cpu']}. "
                   'Shared CI runners are noisy: compare ratios, not absolute times.')


STYLE = """
.bg{fill:#fcfcfb}.rule{stroke:#e1e0d9}.ring{fill:none;stroke:rgba(11,11,11,0.10)}
.t1{fill:#0b0b0b}.t2{fill:#52514e}.t3{fill:#898781}.good{fill:#006300}.muted{fill:#52514e}
@media (prefers-color-scheme: dark){
.bg{fill:#1a1a19}.rule{stroke:#2c2c2a}.ring{stroke:rgba(255,255,255,0.10)}
.t1{fill:#f0efec}.t2{fill:#c3c2b7}.good{fill:#0ca30c}.muted{fill:#c3c2b7}}
text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
font-variant-numeric:tabular-nums}
.title{font-size:16px;font-weight:600}.group{font-size:12px;font-weight:600}
.head{font-size:12px;font-weight:600}
.cell{font-size:13px}.sub{font-size:12px}.note{font-size:11px}.strong{font-weight:600}
"""


def svg(table, info):
    modes = modes_of(table)
    pad, row_h, label_w, group_w = 24, 34, 400, 230
    width = label_w + group_w * len(modes) + pad
    group_y, head_y = 84, 104
    body_y = head_y + 14
    height = body_y + row_h * len(table) + 64
    # Right edges of the fynd, fd and ratio columns in each group.
    columns = []
    for i in range(len(modes)):
        ratio = label_w + (i + 1) * group_w
        columns.append((ratio - 165, ratio - 105, ratio))
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
           '<title id="title">fynd vs fd benchmark</title>',
           f'<desc id="desc">{escape(footer(info, modes))}</desc>',
           f'<style>{STYLE}</style>',
           f'<rect class="bg" width="{width}" height="{height}" rx="8"/>',
           f'<rect class="ring" x="0.5" y="0.5" width="{width - 1}" height="{height - 1}" rx="8"/>',
           f'<text class="t1 title" x="{pad}" y="36">fynd vs fd, warm median wall time in ms</text>',
           f'<text class="t2 sub" x="{pad}" y="58">Lower is better. Commit '
           f'{escape(info["commit"])}; {escape(info["fd"])}.</text>',
           f'<text class="t3 head" x="{pad}" y="{head_y}">Tree and query</text>']
    for (key, title), (fynd, fd, ratio) in zip(modes, columns):
        left = ratio - group_w + 20
        out.append(f'<text class="t2 group" x="{left}" y="{group_y}">{title}</text>')
        out.append(f'<line class="rule" x1="{left}" x2="{ratio}" y1="{group_y + 6}" '
                   f'y2="{group_y + 6}"/>')
        for x, label in ((fynd, 'fynd'), (fd, 'fd'), (ratio, 'fynd vs fd')):
            out.append(f'<text class="t3 head" x="{x}" y="{head_y}" text-anchor="end">'
                       f'{label}</text>')
    for index, row in enumerate(table):
        top = body_y + index * row_h
        base = top + 22
        tree, query = describe(row)
        out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{top}" y2="{top}"/>')
        out.append(f'<text class="cell" x="{pad}" y="{base}"><tspan class="t1">{escape(tree)}</tspan>'
                   f'<tspan class="t2"> · {escape(query)}</tspan></text>')
        for (key, _), (fynd, fd, ratio) in zip(modes, columns):
            cell = row[key]
            text, tone = verdict(cell['ratio'])
            out.append(f'<text class="t1 cell strong" x="{fynd}" y="{base}" '
                       f'text-anchor="end">{cell["fynd"]:.2f}</text>')
            out.append(f'<text class="t1 cell" x="{fd}" y="{base}" '
                       f'text-anchor="end">{cell["fd"]:.2f}</text>')
            out.append(f'<text class="{tone} cell strong" x="{ratio}" y="{base}" '
                       f'text-anchor="end">{text}</text>')
    end = body_y + row_h * len(table)
    out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{end}" y2="{end}"/>')
    notes = [f'{info["repeats"]} warm runs per cell, process startup included. '
             'One thread: fynd -j1, fd --threads 1.']
    if len(modes) > 1:
        notes[0] += ' Default: no thread option for either tool.'
    notes.append(f'{info["runner"]}, {info["cpu"]}. Shared runners are noisy: compare ratios.')
    for offset, note in enumerate(notes):
        out.append(f'<text class="t3 note" x="{pad}" y="{end + 24 + offset * 17}">{escape(note)}</text>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--report', type=Path, required=True, help='single-thread bench report')
    parser.add_argument('--defaults-report', type=Path, help='bench report run with --defaults')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--runner', default='GitHub-hosted ubuntu runner')
    args = parser.parse_args(argv)
    single = json.loads(args.report.read_text())
    defaults = json.loads(args.defaults_report.read_text()) if args.defaults_report else None
    table = rows(single, defaults)
    info = meta(single, args.commit, args.runner)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'bench.svg').write_text(svg(table, info))
    (args.out / 'badge.json').write_text(json.dumps(badge(table), indent=2) + '\n')
    (args.out / 'summary.md').write_text(markdown(table, info))


if __name__ == '__main__':
    main()
