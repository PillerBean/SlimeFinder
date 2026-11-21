#!/usr/bin/env python3
"""Build a paper-style DOCX for the annular-coverage counterexample problem."""

from __future__ import annotations

import re
from xml.sax.saxutils import escape

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement, parse_xml
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor
from pathlib import Path

OUT = Path(__file__).resolve().parent / "ring_counterexample_problem_paper.docx"

M = "http://schemas.openxmlformats.org/officeDocument/2006/math"
BODY = "Times New Roman"
CJK = "SimSun"
HEAD = "Arial"
ACCENT = RGBColor(0x1B, 0x2A, 0x41)
MUTED = RGBColor(0x55, 0x5F, 0x6D)

# Only known math bases; this avoids treating file paths such as
# tools/bench/ring_counterexample.cpp as subscripts.
MATH_TOKEN = re.compile(r"(\b[CFSMTVEPQXwG]|Ω)(_(\{[^{}]+\}|[A-Za-z0-9]+))|(\^(\{[^{}]+\}|[A-Za-z0-9]+))")


def add_rich_runs(paragraph, text: str, size=10.5):
    pos = 0
    for match in MATH_TOKEN.finditer(text):
        if match.start() > pos:
            run = paragraph.add_run(text[pos:match.start()])
            set_font(run, BODY, size)
        if match.group(2):  # base_subscript
            base = paragraph.add_run(match.group(1))
            set_font(base, "Cambria Math", size, italic=True)
            sub = paragraph.add_run(match.group(3).strip("{}"))
            set_font(sub, "Cambria Math", size, italic=True)
            sub.font.subscript = True
        else:  # superscript
            sup = paragraph.add_run(match.group(5).strip("{}"))
            set_font(sup, "Cambria Math", size, italic=True)
            sup.font.superscript = True
        pos = match.end()
    if pos < len(text):
        run = paragraph.add_run(text[pos:])
        set_font(run, BODY, size)


def set_font(run, name=BODY, size=None, bold=None, italic=None):
    run.font.name = name
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), CJK if name == BODY else name)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.bold = bold
    if italic is not None:
        run.italic = italic


def border(paragraph, color="AEB8C2", size="5"):
    p_pr = paragraph._p.get_or_add_pPr()
    p_bdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), size)
    bottom.set(qn("w:space"), "4")
    bottom.set(qn("w:color"), color)
    p_bdr.append(bottom)
    p_pr.append(p_bdr)


def shade(paragraph, fill):
    p_pr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    p_pr.append(shd)


def mrun(text: str) -> str:
    return f'<m:r><m:t xml:space="preserve">{escape(text)}</m:t></m:r>'


def msub(base: str, sub: str) -> str:
    return f'<m:sSub><m:e>{base}</m:e><m:sub>{sub}</m:sub></m:sSub>'


def msup(base: str, sup: str) -> str:
    return f'<m:sSup><m:e>{base}</m:e><m:sup>{sup}</m:sup></m:sSup>'


def mfrac(num: str, den: str) -> str:
    return f'<m:f><m:num>{num}</m:num><m:den>{den}</m:den></m:f>'


def nary(chr_: str, sub: str, sup: str, body: str) -> str:
    return (
        '<m:nary><m:naryPr>'
        f'<m:chr m:val="{chr_}"/><m:limLoc m:val="undOvr"/>'
        '</m:naryPr>'
        f'<m:sub>{sub}</m:sub><m:sup>{sup}</m:sup><m:e>{body}</m:e></m:nary>'
    )


def d(content: str) -> str:
    return f'<m:d><m:e>{content}</m:e></m:d>'


def add_equation(document: Document, xml: str):
    p = document.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(5)
    p.paragraph_format.space_after = Pt(5)
    omath = parse_xml(f'<m:oMathPara xmlns:m="{M}"><m:oMath>{xml}</m:oMath></m:oMathPara>')
    p._p.append(omath)


def add_text_paragraph(document: Document, text: str, first_indent=True, size=10.5):
    p = document.add_paragraph()
    p.paragraph_format.first_line_indent = Cm(0.75) if first_indent else Cm(0)
    p.paragraph_format.line_spacing = 1.18
    p.paragraph_format.space_after = Pt(5)
    add_rich_runs(p, text, size)
    return p


def add_inline_paragraph(document: Document, chunks: list[tuple[str, bool]]):
    """chunks: (text, is_math); math chunks use Cambria Math."""
    p = document.add_paragraph()
    p.paragraph_format.first_line_indent = Cm(0.75)
    p.paragraph_format.line_spacing = 1.18
    p.paragraph_format.space_after = Pt(5)
    for text, is_math in chunks:
        run = p.add_run(text)
        if is_math:
            set_font(run, "Cambria Math", 10.5)
        else:
            set_font(run, BODY, 10.5)
    return p


def add_heading(document: Document, text: str, level=1):
    p = document.add_paragraph()
    p.paragraph_format.keep_with_next = True
    p.paragraph_format.space_before = Pt(13 if level == 1 else 9)
    p.paragraph_format.space_after = Pt(4)
    run = p.add_run(text)
    set_font(run, HEAD, 12.5 if level == 1 else 10.5, bold=True)
    run.font.color.rgb = ACCENT
    if level == 1:
        border(p, "1B2A41", "7")
    return p


def add_table(document: Document, rows: list[list[str]], widths=None):
    table = document.add_table(rows=0, cols=len(rows[0]))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True
    for ri, row in enumerate(rows):
        cells = table.add_row().cells
        for ci, text in enumerate(row):
            cell = cells[ci]
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            if widths:
                cell.width = Cm(widths[ci])
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(1)
            p.paragraph_format.space_after = Pt(1)
            add_rich_runs(p, text, 9.2)
            if ri == 0:
                for run in p.runs:
                    run.bold = True
                shade(p, "DCE6F1")
    p = document.add_paragraph()
    p.paragraph_format.space_after = Pt(2)
    return table


def add_labeled_block(document: Document, label: str, body: str):
    p = document.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.45)
    p.paragraph_format.space_before = Pt(5)
    p.paragraph_format.space_after = Pt(5)
    r = p.add_run(label + " ")
    set_font(r, HEAD, 9.8, bold=True)
    add_rich_runs(p, body, 10.2)


def add_page_number(paragraph):
    paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = paragraph.add_run("Page ")
    set_font(r, BODY, 8.5)
    fld1 = OxmlElement("w:fldChar")
    fld1.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = " PAGE "
    fld2 = OxmlElement("w:fldChar")
    fld2.set(qn("w:fldCharType"), "end")
    run = paragraph.add_run()
    run._r.append(fld1)
    run._r.append(instr)
    run._r.append(fld2)


def build():
    doc = Document()
    sec = doc.sections[0]
    sec.page_width = Cm(21.0)
    sec.page_height = Cm(29.7)
    sec.top_margin = Cm(2.0)
    sec.bottom_margin = Cm(2.0)
    sec.left_margin = Cm(2.2)
    sec.right_margin = Cm(2.2)

    normal = doc.styles["Normal"]
    normal.font.name = BODY
    normal.font.size = Pt(10.5)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), CJK)

    header = sec.header.paragraphs[0]
    header.text = "Preprint · Square-density seeds and annular coverage"
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in header.runs:
        set_font(run, BODY, 8.0)
        run.font.color.rgb = MUTED

    add_page_number(sec.footer.paragraphs[0])

    # Title block
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(24)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run("Square-Density Seeds and Annular Coverage")
    set_font(r, HEAD, 19, bold=True)
    r.font.color.rgb = ACCENT

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(10)
    r = p.add_run("A Counterexample Problem for Candidate Generation")
    set_font(r, HEAD, 13, bold=False)
    r.font.color.rgb = MUTED

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("SlimeFinder Research Note · 21 November 2026")
    set_font(r, BODY, 9.2)
    r.font.color.rgb = MUTED
    border(p, "1B2A41", "7")

    # Abstract
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.55)
    p.paragraph_format.right_indent = Cm(0.55)
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(7)
    r = p.add_run("Abstract. ")
    set_font(r, BODY, 9.2, bold=True)
    r = p.add_run(
        "We isolate a candidate-generation problem that arises when a square-density "
        "prescan is used to restrict the search for a center of maximum circular annular "
        "coverage. The objective is defined on a binary chunk grid; the candidate set is "
        "produced by a deterministic top-K rule and limited local refinement. We ask "
        "whether this candidate set can omit a true maximizer, and how to count or bound "
        "the probability of that event under independent Bernoulli(p) chunks. The problem "
        "is stated entirely in finite, deterministic terms; no statistical estimate is "
        "used in the definition of failure."
    )
    set_font(r, BODY, 9.2)

    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.55)
    p.paragraph_format.space_after = Pt(10)
    r = p.add_run("Keywords: ")
    set_font(r, BODY, 9.0, bold=True)
    r = p.add_run("annular coverage; candidate generation; counterexample; exact enumeration; rare event")
    set_font(r, BODY, 9.0)

    add_heading(doc, "1. Introduction")
    add_text_paragraph(
        doc,
        "A common optimization pipeline evaluates a cheap square-window score on every "
        "chunk, retains a bounded number of seeds, and refines only those seeds at block "
        "resolution. The final objective considered here is not the square count: it is "
        "the number of block cells covered by a circular annulus centered at the candidate. "
        "The pipeline is therefore a two-stage relaxation, and its correctness is a "
        "candidate-set question rather than a numerical-integration question."
    )
    add_text_paragraph(
        doc,
        "The central issue is deliberately separated from any particular Minecraft seed. "
        "Let X be an arbitrary binary chunk configuration. The algorithm either returns a "
        "value equal to the global maximum or it does not. This makes failure a unique "
        "event, even when several centers attain the same maximum. The problem below asks "
        "for a counterexample, an exact small-grid count, or a rigorous probability bound."
    )

    add_heading(doc, "2. Finite model")
    add_text_paragraph(
        doc,
        "Fix integers N≥1, r<R, and a chunk side length s=16. Let G_N={0,...,N−1}² and "
        "let Ω_N={0,...,sN−1}². A configuration is a vector X∈{0,1}^{Gₙ}. For v=(i,j)∈G_N, "
        "the corresponding block rectangle is Q_v=[si,si+s−1]×[sj,sj+s−1]."
    )
    add_text_paragraph(doc, "For c∈Ω_N, define the annulus and the chunk weight:")
    add_equation(
        doc,
        mrun("Ann(c) = { u∈Ω_N : r² ≤ ‖u−c‖² ≤ R² },")
    )
    add_equation(
        doc,
        msub(mrun("w"), mrun("c")) + mrun("(v) = |Ann(c) ∩ ") + msub(mrun("Q"), mrun("v")) + mrun("|.")
    )
    add_text_paragraph(doc, "The annular coverage of X at c is the integer")
    add_equation(
        doc,
        msub(mrun("C"), mrun("X")) + mrun("(c) = ") +
        nary("∑", msub(mrun("v"), mrun("G_N")), "", msub(mrun("X"), mrun("v")) + msub(mrun("w"), mrun("c")) + mrun("(v)."))
    )
    add_text_paragraph(doc, "The global optimum is the value")
    add_equation(
        doc,
        mrun("M(X) = ") + msub(mrun("max"), mrun("c∈Ω_N")) + msub(mrun("C"), mrun("X")) + mrun("(c).")
    )

    add_heading(doc, "3. Square-density seeds")
    add_text_paragraph(
        doc,
        "Let g=15, h=(g−1)/2=7. For v∈G_N, the square window and its binary score are"
    )
    add_equation(
        doc,
        msub(mrun("W"), mrun("v")) + mrun(" = { u∈G_N : ‖u−v‖∞ ≤ h },")
    )
    add_equation(
        doc,
        msub(mrun("S"), mrun("X")) + mrun("(v) = ") +
        nary("∑", msub(mrun("u"), msub(mrun("W"), mrun("v"))), "",
             msub(mrun("X"), mrun("u")) + mrun("."))
    )
    add_text_paragraph(
        doc,
        "Order the chunks by decreasing S_X(v), breaking ties by the lexicographic order of "
        "(j,i). Let T_K(X) be the first K chunks in this total order, and let P_8(X) be the "
        "first min(8,K) chunks of T_K(X). For a chunk v, let B(v)={sv+u:0≤u<s}² be its 256 "
        "block centers, and let N(v)={u∈G_N:‖u−v‖∞≤1}."
    )
    add_text_paragraph(
        doc,
        "For A⊆G_N, write B(A)=⋃_{v∈A}B(v) and N(A)=⋃_{v∈A}N(v), with the convention that "
        "the union over the empty set is empty. Then"
    )
    add_equation(
        doc,
        msub(mrun("E"), mrun("K")) + mrun("(X) = B(") + msub(mrun("T"), mrun("K")) +
        mrun("(X)) ∪ B(") + mrun("N(") + msub(mrun("P"), mrun("8")) + mrun("(X))).")
    )
    add_text_paragraph(
        doc,
        "All unions are over block-center sets; duplicate centers are identified. The seed "
        "stage retains only T_K(X), and the refinement stage evaluates exactly E_K(X)."
    )

    add_heading(doc, "4. Failure event")
    add_text_paragraph(doc, "The algorithm's returned value is")
    add_equation(
        doc,
        msub(mrun("V"), mrun("K")) + mrun("(X) = ") + msub(mrun("max"), mrun("c∈E_K(X)")) +
        msub(mrun("C"), mrun("X")) + mrun("(c).")
    )
    add_text_paragraph(doc, "Failure is a value comparison, not a coordinate comparison:")
    add_equation(
        doc,
        msub(mrun("F"), mrun("N,K")) + mrun("(X) = 1[ ") + msub(mrun("V"), mrun("K")) +
        mrun("(X) < M(X) ].")
    )
    add_text_paragraph(
        doc,
        "Thus multiple global maximizers or multiple algorithm outputs do not introduce "
        "ambiguity. Success means V_K(X)=M(X); it does not require a unique center. For "
        "iid Bernoulli(p) chunks, define"
    )
    add_equation(
        doc,
        msub(mrun("P"), mrun("N,K")) + mrun("(p) = ") +
        nary("∑", mrun("X"), "", msub(mrun("F"), mrun("N,K")) + mrun("(X) ") +
             msup(mrun("p"), mrun("|X|")) + msup(mrun("(1−p)"), mrun("N²−|X|")) + mrun(","))
    )
    add_text_paragraph(
        doc,
        "where the sum is over X∈{0,1}^{Gₙ} and |X| = ∑ X_v. The default is p=0.1."
    )

    add_heading(doc, "5. Structural observations")
    add_text_paragraph(
        doc,
        "For the default geometry r=24, R=128, s=16, g=15, the chunk-center lattice has "
        "the following exact partition. This is a numerical observation, not a theorem."
    )
    add_table(
        doc,
        [
            ["Region", "Count"],
            ["Ring chunks (center distance in [r,R])", "188"],
            ["Ring chunks inside the 15×15 square", "184"],
            ["Ring chunks outside the 15×15 square", "4"],
            ["Square chunks total", "225"],
            ["Square chunks outside the ring (inner hole)", "9"],
            ["Square chunks outside the annulus", "32"],
        ],
        widths=[11.0, 4.0],
    )
    add_text_paragraph(
        doc,
        "Consequently a high C_X(c) generally forces a high S_X at the corresponding chunk. "
        "A counterexample must therefore create a center whose annular coverage is large "
        "while its square score is below the K-th seed cutoff, and which is not recovered "
        "by the 3×3 polish neighborhood of the top eight seeds."
    )

    add_heading(doc, "6. Known computational status")
    add_text_paragraph(
        doc,
        "The following checks are exploratory. They do not constitute a proof and are not "
        "part of the formal statement."
    )
    add_table(
        doc,
        [
            ["Model", "Search", "Failures"],
            ["N=4, K=1, (r,R,g)=(8,32,3)", "all 2^16 configurations", "0"],
            ["N=4, K=2, (r,R,g)=(8,32,3)", "all 2^16 configurations", "0"],
            ["N=8, K=1, (r,R,g)=(8,32,7)", "1,900 iid p=0.1 samples", "0"],
        ],
        widths=[8.4, 7.0, 3.2],
    )
    add_text_paragraph(
        doc,
        "The N=4 result is exact for that reduced model. The N=8 result is a random sample; "
        "a zero count does not bound the failure probability tightly."
    )

    add_heading(doc, "7. Problems")
    add_labeled_block(
        doc,
        "Problem 1 (Existence).",
        "Do there exist N≥1, K≥1, and X∈{0,1}^{Gₙ} such that F_{N,K}(X)=1? If yes, give "
        "the smallest N, the smallest K, and an explicit X. If no for a specified parameter "
        "range, give a proof that every global maximizer is represented by E_K(X)."
    )
    add_labeled_block(
        doc,
        "Problem 2 (Exact small-grid count).",
        "For fixed N,K and the reduced geometry, compute the integer count of failing "
        "configurations. Verify or refute the exploratory counts in Section 6."
    )
    add_labeled_block(
        doc,
        "Problem 3 (Bernoulli mass).",
        "For p=0.1, determine P_{N,K}(p), or give non-trivial lower and upper bounds. If a "
        "counterexample exists, compute its Bernoulli mass separately from the event count."
    )
    add_labeled_block(
        doc,
        "Problem 4 (Full parameter regime).",
        "For N=3,750,000 and K=4000, determine whether P_{N,K}(0.1)>0 and give a rigorous "
        "bound of the form P_{N,K}(0.1)≤10⁻ˢ. Distinguish a proof of non-existence from "
        "a failure to find a counterexample."
    )

    add_heading(doc, "8. What would count as an answer")
    add_text_paragraph(
        doc,
        "The most useful responses are, in order: (i) an explicit binary matrix X with "
        "F_{N,K}(X)=1; (ii) a SAT/CP-SAT encoding and an UNSAT certificate for a stated "
        "finite range; (iii) an exact count for a small grid; (iv) a proved upper bound for "
        "the full parameter regime. Reference-point tail estimates and unenumerated "
        "“almost impossible” claims are not answers to the stated problem."
    )

    add_heading(doc, "Data and code")
    add_text_paragraph(
        doc,
        "The reduced-model exhaustive checker and the random sampler are in "
        "tools/bench/ring_counterexample.cpp. The synthetic trap tests are in "
        "tests/integration/ring_synthetic_test.cpp. The repository is SlimeFinder "
        "(PillerBean/SlimeFinder)."
    )

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(16)
    r = p.add_run("Version: 2026-11-21 · Formal problem statement · Not a claim of optimality")
    set_font(r, BODY, 8.2)
    r.font.color.rgb = MUTED

    doc.core_properties.title = "Square-Density Seeds and Annular Coverage: A Counterexample Problem"
    doc.core_properties.subject = "Formal finite counterexample and probability problem"
    doc.core_properties.author = "SlimeFinder Research Note"
    doc.save(OUT)
    print(f"saved {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    build()
