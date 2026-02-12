import re
import sys

START_RE = re.compile(r"<<<<<<<<<< ExprEval::reduceExpr")
END_RE = re.compile(r"ExprEval::reduceExpr >>>>>>>>>>")
TYPE_ID_RE = re.compile(r"^\s*([^:]+):.*?\bid:(\d+)")
LOC_RE = re.compile(r"line:(\d+):(\d+),\s*endln:(\d+):(\d+)")
OPTYPE_RE = re.compile(r"\|vpiOpType:(\d+)")
DETAIL_RE = re.compile(r"\(([^)]+)\)")

def parse_block(lines):
    input_type = input_id = None
    sl = sc = el = ec = None
    output_type = output_id = None
    optype = None
    idetail = None

    expect_input = False
    expect_output = False

    for line in lines:

        if ">> object: decompile:" in line:
            expect_input = True
            expect_output = False
            continue

        if ">> result: decompile:" in line:
            expect_output = True
            expect_input = False
            continue

        m = TYPE_ID_RE.search(line)
        if m:
            t, i = m.group(1), m.group(2)

            dm = DETAIL_RE.search(line)
            detail = dm.group(1) if dm else None

            if expect_input and input_type is None:
                input_type, input_id = t, i
                idetail = detail
                expect_input = False

                lm = LOC_RE.search(line)
                if lm:
                    sl, sc, el, ec = lm.groups()

                continue

            if expect_output and output_type is None:
                output_type, output_id = t, i
                expect_output = False
                continue

        om = OPTYPE_RE.search(line)
        if om:
            optype = om.group(1)

    return [
        input_type, input_id, sl, sc, el, ec,
        output_type, output_id,
        optype,
        idetail
    ]

def main(logfile, outfile):
    stack = []

    with open(logfile, "r", errors="ignore") as f, open(outfile, "w") as out:

        for line in f:

            if START_RE.search(line):
                stack.append([])
                continue

            if END_RE.search(line):
                if stack:
                    block = stack.pop()
                    row = parse_block(block)
                    out.write("\t".join(x or "-" for x in row) + "\n")
                continue

            if stack:
                stack[-1].append(line)

    print("Saved compact file:", outfile)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
