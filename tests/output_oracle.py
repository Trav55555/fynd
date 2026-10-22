"""Byte-exact unordered path checks; root groups remain sequential, never deduped."""
from collections import Counter


def _raw_matches(data, paths):
    """Parse known LF-terminated path tokens, backtracking on embedded LF ambiguity."""
    if all(b'\n' not in path for path in paths):
        return (not data or data.endswith(b'\n')) and Counter(
            data[:-1].split(b'\n') if data else []) == Counter(paths)
    remaining = Counter(path + b'\n' for path in paths)
    tokens = tuple(remaining)
    # Iterative backtracking avoids recursion limits and greedy prefix mistakes.
    frames = [(0, iter(tokens))]
    chosen = []
    while frames:
        offset, choices = frames[-1]
        if offset == len(data):
            return not any(remaining.values())
        for token in choices:
            if remaining[token] and data.startswith(token, offset):
                remaining[token] -= 1
                chosen.append(token)
                frames.append((offset + len(token), iter(tokens)))
                break
        else:
            frames.pop()
            if chosen:
                remaining[chosen.pop()] += 1
    return False


def matches_output(data, groups, delimiter=b'\0'):
    """Check each independent expected root group, allowing only within-root reordering.

    LF is not a filename boundary: raw mode consumes known complete path tokens.
    No decoding, newline normalization, set conversion, or output-derived oracle.
    """
    groups = [list(group) for group in groups]
    if delimiter == b'\0':
        if data and not data.endswith(b'\0'):
            return False
        records = data[:-1].split(b'\0') if data else []
        offset = 0
        for group in groups:
            end = offset + len(group)
            if Counter(records[offset:end]) != Counter(group):
                return False
            offset = end
        return offset == len(records)
    if delimiter != b'\n':
        raise ValueError('expected NUL or LF delimiter')
    offset = 0
    for group in groups:
        # Expected byte lengths define boundaries even with embedded newlines.
        end = offset + sum(len(path) + 1 for path in group)
        if not _raw_matches(data[offset:end], group):
            return False
        offset = end
    return offset == len(data)
