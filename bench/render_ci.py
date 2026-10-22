#!/usr/bin/env python3
"""Render a bench.py report as a README table image, a badge and a Markdown summary.

Inputs are a SUCCESS report from bench/bench.py run with fd present. Outputs are
bench.svg (light/dark table), badge.json (shields.io endpoint) and summary.md.
Values are the report's own medians; nothing is re-measured or smoothed.
"""
import argparse
import json
import math
from pathlib import Path
from xml.sax.saxutils import escape

SHAPES = ('wide', 'deep', 'mixed')
QUERIES = ('all', 'needle')
TOOLS = ('fynd', 'fd', 'find')


def first_line(text):
    return (text or '').strip().splitlines()[0] if (text or '').strip() else 'unknown'


def rows(report):
    """One row per shape/query: medians in ms and fd/fynd elapsed ratio."""
    if report.get('status') != 'SUCCESS':
        raise ValueError('report status is not SUCCESS')
    medians = report['medians']
    shapes = report['fixture']['shapes']
    result = []
    for shape in SHAPES:
        for query in QUERIES:
            ms = {}
            for tool in TOOLS:
                label = f'{shape}-{query}-{tool}'
                if label not in medians:
                    raise ValueError(f'missing median: {label}')
                ms[tool] = medians[label]['elapsed_ns'] / 1e6
            if ms['fynd'] <= 0 or ms['fd'] <= 0:
                raise ValueError(f'non-positive median: {shape}-{query}')
            info = shapes[shape]
            selected = info['files'] if query == 'all' else info['needle_files']
            result.append(dict(shape=shape, query=query, files=info['files'],
                               directories=info['directories'], selected=selected,
                               ms=ms, ratio=ms['fd'] / ms['fynd']))
    return result


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


def meta(report, commit, runner):
    tools = report['tools']
    return dict(repeats=report['requested']['repeats'],
                fynd_threads=report['requested']['fynd_threads'],
                fd=first_line(tools['fd']['version']),
                find=first_line(tools['find']['version']),
                cpu=report.get('cpu') or 'unknown CPU',
                commit=commit[:7], runner=runner)


def badge(table):
    ratio = geomean([row['ratio'] for row in table])
    message, tone = verdict(ratio)
    return {'schemaVersion': 1, 'label': 'fynd vs fd (1 thread)',
            'message': message + ' (geomean)',
            'color': '2ea44f' if tone == 'good' else 'lightgrey'}


def markdown(table, info):
    lines = ['| Tree | Query | fynd (ms) | fd (ms) | find (ms) | fynd vs fd |',
             '| --- | --- | ---: | ---: | ---: | ---: |']
    for row in table:
        tree, query = describe(row)
        lines.append(f"| {tree} | {query} | {row['ms']['fynd']:.2f} | {row['ms']['fd']:.2f} "
                     f"| {row['ms']['find']:.2f} | {verdict(row['ratio'])[0]} |")
    lines += ['', footer(info), '']
    return '\n'.join(lines)


def footer(info):
    return (f"Median wall time of {info['repeats']} warm runs, process startup included. "
            f"fynd -j{info['fynd_threads']} at {info['commit']}; {info['fd']} with one thread; "
            f"{info['find']}. {info['runner']}, {info['cpu']}. "
            'Shared CI runners are noisy: compare ratios, not absolute times.')


STYLE = """
.bg{fill:#fcfcfb}.rule{stroke:#e1e0d9}.ring{fill:none;stroke:rgba(11,11,11,0.10)}
.t1{fill:#0b0b0b}.t2{fill:#52514e}.t3{fill:#898781}.good{fill:#006300}.muted{fill:#52514e}
@media (prefers-color-scheme: dark){
.bg{fill:#1a1a19}.rule{stroke:#2c2c2a}.ring{stroke:rgba(255,255,255,0.10)}
.t1{fill:#f0efec}.t2{fill:#c3c2b7}.good{fill:#0ca30c}.muted{fill:#c3c2b7}}
text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
font-variant-numeric:tabular-nums}
.title{font-size:16px;font-weight:600}.head{font-size:12px;font-weight:600}
.cell{font-size:13px}.sub{font-size:12px}.note{font-size:11px}.strong{font-weight:600}
"""


def svg(table, info):
    width, pad, row_h = 760, 24, 34
    head_y = 84
    body_y = head_y + 14
    height = body_y + row_h * len(table) + 64
    right = {'fynd': 430, 'fd': 520, 'find': 610, 'ratio': width - pad}
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
           '<title id="title">fynd vs fd benchmark</title>',
           f'<desc id="desc">{escape(footer(info))}</desc>',
           f'<style>{STYLE}</style>',
           f'<rect class="bg" width="{width}" height="{height}" rx="8"/>',
           f'<rect class="ring" x="0.5" y="0.5" width="{width - 1}" height="{height - 1}" rx="8"/>',
           f'<text class="t1 title" x="{pad}" y="36">fynd vs fd, warm median wall time</text>',
           f'<text class="t2 sub" x="{pad}" y="58">Lower is better. fynd -j{info["fynd_threads"]} and fd '
           f'with one thread; GNU find for reference. Commit {escape(info["commit"])}.</text>']
    out.append(f'<text class="t3 head" x="{pad}" y="{head_y}">Tree and query</text>')
    for key, label in (('fynd', 'fynd ms'), ('fd', 'fd ms'), ('find', 'find ms'),
                       ('ratio', 'fynd vs fd')):
        out.append(f'<text class="t3 head" x="{right[key]}" y="{head_y}" '
                   f'text-anchor="end">{label}</text>')
    for index, row in enumerate(table):
        top = body_y + index * row_h
        base = top + 22
        tree, query = describe(row)
        text, tone = verdict(row['ratio'])
        out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{top}" y2="{top}"/>')
        out.append(f'<text class="cell" x="{pad}" y="{base}"><tspan class="t1">{escape(tree)}</tspan>'
                   f'<tspan class="t2"> · {escape(query)}</tspan></text>')
        for tool in TOOLS:
            weight = ' strong' if tool == 'fynd' else ''
            out.append(f'<text class="t1 cell{weight}" x="{right[tool]}" y="{base}" '
                       f'text-anchor="end">{row["ms"][tool]:.2f}</text>')
        out.append(f'<text class="{tone} cell strong" x="{right["ratio"]}" y="{base}" '
                   f'text-anchor="end">{text}</text>')
    end = body_y + row_h * len(table)
    out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{end}" y2="{end}"/>')
    notes = (f'{info["repeats"]} warm runs per cell, process startup included. {info["fd"]}; '
             f'{info["find"]}.',
             f'{info["runner"]}, {info["cpu"]}. Shared runners are noisy: compare ratios.')
    for offset, note in enumerate(notes):
        out.append(f'<text class="t3 note" x="{pad}" y="{end + 24 + offset * 17}">{escape(note)}</text>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--runner', default='GitHub-hosted ubuntu runner')
    args = parser.parse_args(argv)
    report = json.loads(args.report.read_text())
    table = rows(report)
    info = meta(report, args.commit, args.runner)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'bench.svg').write_text(svg(table, info))
    (args.out / 'badge.json').write_text(json.dumps(badge(table), indent=2) + '\n')
    (args.out / 'summary.md').write_text(markdown(table, info))


if __name__ == '__main__':
    main()
