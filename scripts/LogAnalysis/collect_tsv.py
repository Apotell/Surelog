import re
import sys

START_RE = re.compile(r"<<<<<<<<<< ExprEval::reduceExpr")
END_RE = re.compile(r"ExprEval::reduceExpr >>>>>>>>>>")
TYPE_ID_RE = re.compile(r"^\s*([^:]+):.*?\bid:(\d+)")
LOC_RE = re.compile(r"line:(\d+):(\d+),\s*endln:(\d+):(\d+)")
OPTYPE_RE = re.compile(r"\|vpiOpType:(\d+)")

def parse_block(lines):
    input_type = input_id = None
    sl = sc = el = ec = None
    output_type = output_id = None
    optype = None

    expect_input = False
    expect_output = False

    for line in lines:

        # trigger zones
        if ">> object: decompile:" in line:
            expect_input = True
            expect_output = False
            continue

        if ">> result: decompile:" in line:
            expect_output = True
            expect_input = False
            continue

        # capture TYPE/ID only immediately after markers
        m = TYPE_ID_RE.search(line)
        if m:

            t, i = m.group(1), m.group(2)

            if expect_input and input_type is None:
                input_type, input_id = t, i
                expect_input = False

                lm = LOC_RE.search(line)
                if lm:
                    sl, sc, el, ec = lm.groups()

                continue

            if expect_output and output_type is None:
                output_type, output_id = t, i
                expect_output = False
                continue

        # capture op type anywhere
        om = OPTYPE_RE.search(line)
        if om:
            optype = om.group(1)

    return [input_type, input_id, sl, sc, el, ec, output_type, output_id, optype]

def main(logfile, outfile):
    depth = 0
    block = []

    with open(logfile, "r", errors="ignore") as f, open(outfile, "w") as out:
        for line in f:
            if START_RE.search(line):
                if depth == 0:
                    block = []
                depth += 1
                continue

            if END_RE.search(line):
                depth -= 1
                if depth == 0:
                    row = parse_block(block)
                    out.write("\t".join(x or "-" for x in row) + "\n")
                continue

            if depth > 0:
                block.append(line)

    print("Saved compact file:", outfile)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
