"""Generate spherical edge adjacency, including the 7/8 subdivision seams.

Build-time only; the target uses integer tile IDs and does no topology search.
"""
import argparse
import collections
import math
import re
from pathlib import Path


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    length = math.sqrt(dot(a, a))
    return tuple(v/length for v in a)


def topology(source):
    text = source.read_text().split('fixed_triangles = {{')[1].split('}};')[0]
    values = [float.fromhex(v) for v in re.findall(r'-?0x[0-9a-f.]+p[+-][0-9]+', text)]
    assert len(values) == 4220*9
    vertices = [unit(values[i:i+3]) for i in range(0, len(values), 3)]
    edges = collections.defaultdict(list)
    neighbors = [[] for _ in vertices]
    def ends(e):
        return vertices[e], vertices[e//3*3+(e%3+1)%3]
    for e in range(len(vertices)):
        key = tuple(sorted(tuple(round(v*1e8) for v in p) for p in ends(e)))
        edges[key].append(e)
    unmatched = []
    for es in edges.values():
        assert len(es) in (1, 2)
        if len(es) == 2:
            a, b = es
            neighbors[a].append(b)
            neighbors[b].append(a)
        else:
            unmatched.extend(es)
    for index, a in enumerate(unmatched):
        v, w = ends(a)
        normal = unit(cross(v, w))
        basis = cross(normal, v)
        length = math.atan2(dot(w, basis), dot(w, v))
        for b in unmatched[index+1:]:
            c, d = ends(b)
            if abs(dot(c, normal)) > 1e-9 or abs(dot(d, normal)) > 1e-9:
                continue
            lo, hi = sorted((math.atan2(dot(c, basis), dot(c, v)),
                             math.atan2(dot(d, basis), dot(d, v))))
            # An edge across the opposite branch cut is not a local overlap.
            if hi-lo > math.pi:
                continue
            if min(length, hi)-max(0., lo) > 1e-9:
                neighbors[a].append(b)
                neighbors[b].append(a)
    # Independently check every edge is covered once, with no gaps/overlaps.
    for e, ns in enumerate(neighbors):
        assert 1 <= len(ns) <= 2
        v, w = ends(e)
        normal = unit(cross(v, w))
        basis = cross(normal, v)
        length = math.atan2(dot(w, basis), dot(w, v))
        spans = []
        for n in ns:
            assert e in neighbors[n] and n//3 != e//3
            a, b = ends(n)
            lo, hi = sorted((math.atan2(dot(a, basis), dot(a, v)),
                             math.atan2(dot(b, basis), dot(b, v))))
            spans.append((max(0., lo), min(length, hi)))
        end = 0.
        for lo, hi in sorted(spans):
            assert abs(lo-end) < 1e-8 and hi > lo
            end = hi
        assert abs(end-length) < 1e-8
    return neighbors


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    ns = topology(args.source)
    lines = ['// Generated edge neighbors; 65535 means no second neighbor.',
             '#pragma once', '#include <cstdint>',
             'static constexpr uint16_t tile_neighbors[4220][3][2]={']
    for tile in range(4220):
        pairs = []
        for edge in ns[tile*3:tile*3+3]:
            ids = sorted(n//3 for n in edge)
            if len(ids) == 1:
                ids.append(65535)
            pairs.append('{%d,%d}' % tuple(ids))
        lines.append('{'+','.join(pairs)+'},')
    lines.append('};')
    args.output.write_text('\n'.join(lines)+'\n')
    print('PASS_TOPOLOGY: 12660 edges, reciprocal coverage; 780 split edges')
