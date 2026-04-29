import sys

from z3 import And, BoolVal, Solver, is_and, parse_smt2_file


def flatten_and(expr, memo=None):
    """
    Flatten nested AND expressions in Z3 with memoization.

    Args:
        expr: A Z3 expression that may contain nested ANDs
        memo: Dictionary for memoization (internal use)

    Returns:
        A flattened AND expression (or the original if not an AND)
    """
    if memo is None:
        memo = {}

    # Check if we've already processed this expression
    expr_id = expr.get_id()
    if expr_id in memo:
        return memo[expr_id]

    if not is_and(expr):
        memo[expr_id] = expr
        return expr

    flattened = []

    def collect_ands(e):
        e_id = e.get_id()

        # Check memo before processing
        if e_id in memo:
            result = memo[e_id]
            if is_and(result):
                # If memoized result is still an AND, collect its children
                for child in result.children():
                    collect_ands(child)
            else:
                flattened.append(result)
            return

        if is_and(e):
            # Recursively collect from all children
            for child in e.children():
                collect_ands(child)
        else:
            flattened.append(e)

    collect_ands(expr)

    # Build result
    if len(flattened) == 0:
        result = BoolVal(True)
    elif len(flattened) == 1:
        result = flattened[0]
    else:
        result = And(flattened)

    memo[expr_id] = result
    return result


def flatten_assertions(assertions, memo=None):
    """
    Flatten all assertions in a list.

    Args:
        assertions: List of Z3 assertions
        memo: Memoization dictionary

    Returns:
        List of flattened assertions
    """
    if memo is None:
        memo = {}

    flattened_assertions = []
    for assertion in assertions:
        t = flatten_and(assertion, memo)
        if is_and(t):
            for c in t.children():
                flattened_assertions.append(c)
        else:
            flattened_assertions.append(t)

    return flattened_assertions


def main():
    if len(sys.argv) < 2:
        print("Usage: python flatten_and.py <input.smt2> [output.smt2]")
        print("\nIf no output file is specified, result is printed to stdout")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    # Parse the SMT file
    print(f"Reading {input_file}...", file=sys.stderr)
    assertions = parse_smt2_file(input_file)

    print(f"Parsed {len(assertions)} assertions", file=sys.stderr)

    # Flatten all assertions
    print("Flattening AND expressions...", file=sys.stderr)
    memo = {}
    flattened = flatten_assertions(assertions, memo)

    print(f"Memoization cache size: {len(memo)}", file=sys.stderr)

    # Create a new solver with flattened assertions
    s = Solver()
    for assertion in flattened:
        s.add(assertion)

    # Output the result
    smt2_output = s.to_smt2()

    if output_file:
        print(f"Writing to {output_file}...", file=sys.stderr)
        with open(output_file, "w") as f:
            f.write(smt2_output)
        print("Done!", file=sys.stderr)
    else:
        print(smt2_output)


if __name__ == "__main__":
    main()
