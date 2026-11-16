import re
import sys

usage = """Usage: python3 stack-usage.py LIMIT ENTRY CALLGRAPH.ci...

Reads GCC -fcallgraph-info=su output and prints the deepest stack path from ENTRY.
Indirect calls are charged the largest frame of any function. Fails on recursion, dynamic stack use or a path over LIMIT bytes."""

node_pattern = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
edge_pattern = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
frame_pattern = re.compile(r'(\d+) bytes \((static|dynamic|dynamic,bounded)\)')


def main(arguments):
    if len(arguments) < 3:
        print(usage)
        return 2
    limit = int(arguments[0])
    entry = arguments[1]
    frames = {}
    calls = {}
    dynamic = []
    for path in arguments[2:]:
        with open(path) as file:
            text = file.read()
        for title, label in node_pattern.findall(text):
            name = title.split(':')[-1]
            frame = frame_pattern.search(label.replace('\\n', ' '))
            if frame:
                frames[name] = int(frame.group(1))
                if frame.group(2) != 'static':
                    dynamic.append(name)
            else:
                frames.setdefault(name, 0)
        for source, target in edge_pattern.findall(text):
            calls.setdefault(source.split(':')[-1], set()).add(target.split(':')[-1])
    if dynamic:
        print(f'dynamic stack use in {", ".join(sorted(dynamic))}')
        return 1
    indirect = max(frames.values())
    depths = {}
    paths = {}
    active = set()

    def depth(name):
        if name in depths:
            return depths[name]
        if name in active:
            raise RecursionError(name)
        active.add(name)
        frame = indirect if name == '__indirect_call' else frames.get(name, 0)
        deepest = 0
        deepest_path = []
        for callee in sorted(calls.get(name, ())):
            callee_depth = depth(callee)
            if callee_depth > deepest:
                deepest = callee_depth
                deepest_path = paths[callee]
        active.discard(name)
        depths[name] = frame + deepest
        paths[name] = [name] + deepest_path
        return depths[name]

    try:
        total = depth(entry)
    except RecursionError as error:
        print(f'recursion through {error}')
        return 1
    print(f'stack {total} bytes (limit {limit}): {" > ".join(paths[entry])}')
    return 0 if total <= limit else 1


sys.exit(main(sys.argv[1:]))
