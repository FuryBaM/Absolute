#!/usr/bin/env python3
"""Generate one deterministic Absolute program containing many Vector properties."""

from __future__ import annotations

import argparse
import random
from pathlib import Path


def generate_case(rng: random.Random, case_id: int, operations: int) -> str:
    model: list[int] = []
    statements: list[str] = []

    for _ in range(operations):
        choices = ["push", "clear"]
        if model:
            choices.extend(["pop", "remove", "set"])
        operation = rng.choice(choices)

        if operation == "push":
            value = rng.randint(-20000, 20000)
            model.append(value)
            statements.append(f"    vector.push({value});")
        elif operation == "clear":
            if rng.random() < 0.2:
                model.clear()
                statements.append("    vector.clear();")
        elif operation == "pop":
            expected = model.pop()
            statements.append(
                f"    assert(vector.pop() == {expected}, \"case {case_id} pop value\");"
            )
        elif operation == "remove":
            index = rng.randrange(len(model))
            model.pop(index)
            statements.append(f"    vector.removeAt({index});")
        elif operation == "set":
            index = rng.randrange(len(model))
            value = rng.randint(-20000, 20000)
            model[index] = value
            statements.append(f"    vector[{index}] = {value};")

    expected_sum = sum((index + 1) * value for index, value in enumerate(model))
    body = "\n".join(statements)
    return f'''void propertyCase{case_id}() {{
    Vector<int32>* vector = new Vector<int32>();
{body}
    assert(vector.count == {len(model)}, "case {case_id} final count");
    int64 checksum = 0;
    int32 index = 0;
    while (index < vector.count) {{
        checksum += (index + 1) * vector[index];
        index += 1;
    }}
    assert(checksum == {expected_sum}, "case {case_id} final checksum");

    int64 snapshotChecksum = 0;
    int32 snapshotCount = 0;
    foreach (int32 value in vector) {{
        snapshotChecksum += (snapshotCount + 1) * value;
        snapshotCount += 1;
        if (snapshotCount == 1) {{
            vector.clear();
            vector.push({case_id + 100000});
        }}
    }}
    assert(snapshotCount == {len(model)}, "case {case_id} snapshot count");
    assert(snapshotChecksum == {expected_sum}, "case {case_id} snapshot checksum");
    delete vector;
}}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=12648430)
    parser.add_argument("--cases", type=int, default=40)
    parser.add_argument("--operations", type=int, default=120)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    functions = [generate_case(rng, case_id, args.operations) for case_id in range(args.cases)]
    calls = "\n".join(f"    propertyCase{case_id}();" for case_id in range(args.cases))
    source = (
        'import "../../std/collections/vector.abs";\n\n'
        + "\n".join(functions)
        + f'''\nint32 main() {{
{calls}
    println("collection-properties=ok seed={args.seed} cases={args.cases}");
    return 0;
}}
'''
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source, encoding="utf-8")
    print(f"generated {args.cases} cases with seed {args.seed}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
