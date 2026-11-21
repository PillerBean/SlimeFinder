#!/usr/bin/env python3
"""Build the revised, result-oriented paper on the square-seed counterexample."""

from __future__ import annotations

from pathlib import Path

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.shared import Cm, Pt

import build_ring_counterexample_paper as B

OUT = Path(__file__).resolve().parent / "ring_counterexample_solution_paper.docx"
M = B.M


def page_number(paragraph):
    B.add_page_number(paragraph)


def add_title(doc):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(24)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run("When Square-Density Seeds Miss the Annular Optimum")
    B.set_font(r, B.HEAD, 18, bold=True)
    r.font.color.rgb = B.ACCENT

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_after = Pt(10)
    r = p.add_run("A Counterexample Study of Candidate Generation")
    B.set_font(r, B.HEAD, 12.5, bold=False)
    r.font.color.rgb = B.MUTED

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("SlimeFinder Research Note · 21 November 2026")
    B.set_font(r, B.BODY, 9.2)
    r.font.color.rgb = B.MUTED
    B.border(p, "1B2A41", "7")


def abstract(doc):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.55)
    p.paragraph_format.right_indent = Cm(0.55)
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(7)
    r = p.add_run("Abstract. ")
    B.set_font(r, B.BODY, 9.2, bold=True)
    text = (
        "A common pipeline ranks chunks by a 15×15 square-density score, refines "
        "only the top K seeds, and adds a 3×3 chunk polish around the first eight. "
        "The objective, however, is block-cell coverage by a circular annulus. We show "
        "that this candidate set is incomplete. A single chunk already gives a failure "
        "at N=3,K=1; a tie-free construction at N=36,K=1 uses 337 chunks and has "
        "V=33426 against C*=43008 (a 22.3% gap). The mechanism is waste asymmetry: "
        "13 zero-contribution and 44 partial-contribution chunks inside the 15×15 "
        "window are counted fully by the square score. We prove an efficiency bound "
        "and a safety criterion, correct the block-grid geometry counts, and show that "
        "the real-parameter question is an ill-conditioned rank crossing rather than "
        "a 10⁻ˢ rare event."
    )
    r = p.add_run(text)
    B.set_font(r, B.BODY, 9.2)

    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.55)
    p.paragraph_format.space_after = Pt(10)
    r = p.add_run("Keywords: ")
    B.set_font(r, B.BODY, 9.0, bold=True)
    r = p.add_run("annular coverage; square prescan; counterexample; candidate completeness; exact enumeration")
    B.set_font(r, B.BODY, 9.0)


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
    normal.font.name = B.BODY
    normal.font.size = Pt(10.5)
    normal._element.rPr.rFonts.set(B.qn("w:eastAsia"), B.CJK)

    h = sec.header.paragraphs[0]
    h.text = "Preprint · Square-density seeds and annular coverage"
    h.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in h.runs:
        B.set_font(run, B.BODY, 8.0)
        run.font.color.rgb = B.MUTED
    page_number(sec.footer.paragraphs[0])

    add_title(doc)
    abstract(doc)

    B.add_heading(doc, "1. Introduction")
    B.add_text_paragraph(
        doc,
        "The algorithm studied here is deliberately simple and is representative of "
        "the two-stage optimizers used for AFK-center search. A cheap score is evaluated "
        "on every chunk; only a bounded number of seeds are refined at block resolution. "
        "The final score is the number of block cells in a circular annulus, not the "
        "square score. The question is whether the two-stage restriction can miss the "
        "true maximizer."
    )
    B.add_text_paragraph(
        doc,
        "The answer is yes. The failure is not a rare numerical artifact and it is not "
        "caused by an arbitrary tie-break. A tie-free construction is given in Section 5. "
        "The important structural quantity is the amount of annulus mass that lies outside "
        "the 15×15 chunk window. On the default geometry this is up to 2,546 cells, which "
        "is enough to let a slightly lower square score conceal a much larger annular "
        "coverage."
    )

    B.add_heading(doc, "2. Finite model")
    B.add_text_paragraph(
        doc,
        "Fix integers N≥1, r<R and s=16. Put G_N={0,...,N−1}² and Ω_N={0,...,sN−1}². "
        "A configuration is X∈{0,1}^{Gₙ}. For v=(i,j)∈G_N, the corresponding block "
        "rectangle is Q_v=[si,si+s−1]×[sj,sj+s−1]."
    )
    B.add_text_paragraph(doc, "For c∈Ω_N, define the annulus and the chunk weight")
    B.add_equation(
        doc,
        B.mrun("Ann(c) = { u∈Ω_N : r² ≤ ‖u−c‖² ≤ R² },")
    )
    B.add_equation(
        doc,
        B.msub(B.mrun("w"), B.mrun("c")) + B.mrun("(v) = |Ann(c) ∩ ") +
        B.msub(B.mrun("Q"), B.mrun("v")) + B.mrun("|.")
    )
    B.add_text_paragraph(doc, "The annular coverage at c is")
    B.add_equation(
        doc,
        B.msub(B.mrun("C"), B.mrun("X")) + B.mrun("(c) = ") +
        B.nary("∑", B.msub(B.mrun("v"), B.mrun("G_N")), "",
               B.msub(B.mrun("X"), B.mrun("v")) + B.msub(B.mrun("w"), B.mrun("c")) + B.mrun("(v),"))
    )
    B.add_text_paragraph(doc, "and the global optimum is")
    B.add_equation(
        doc,
        B.mrun("M(X) = ") + B.msub(B.mrun("max"), B.mrun("c∈Ω_N")) +
        B.msub(B.mrun("C"), B.mrun("X")) + B.mrun("(c).")
    )

    B.add_heading(doc, "3. Candidate rule")
    B.add_text_paragraph(doc, "Let g=15 and h=7. The square score of a chunk is")
    B.add_equation(
        doc,
        B.msub(B.mrun("S"), B.mrun("X")) + B.mrun("(v) = ") +
        B.nary("∑", B.msub(B.mrun("u"), B.msub(B.mrun("W"), B.mrun("v"))), "",
               B.msub(B.mrun("X"), B.mrun("u")) + B.mrun(","))
    )
    B.add_text_paragraph(
        doc,
        "where W_v={u∈G_N:‖u−v‖∞≤7}. Let t_K(X) be the K-th largest value of S_X. "
        "Define the threshold seed set"
    )
    B.add_equation(
        doc,
        B.msub(B.mrun("T"), B.mrun("K")) + B.mrun("(X) = { v∈G_N : ") +
        B.msub(B.mrun("S"), B.mrun("X")) + B.mrun("(v) ≥ ") + B.msub(B.mrun("t"), B.mrun("K")) +
        B.mrun("(X) }.")
    )
    B.add_text_paragraph(
        doc,
        "Including all tied chunks makes T_K unique as a set and avoids making the "
        "problem an artifact of a lexicographic tie-break. The implementation may cap "
        "the set at K; the mathematical statement below uses the uncapped threshold set. "
        "The no-tie counterexample therefore also applies to any capped rule."
    )
    B.add_text_paragraph(
        doc,
        "Let P_8(X) be the first min(8,|T_K|) chunks of T_K in decreasing S order, with "
        "lexicographic ties. For A⊆G_N write B(A)=⋃_{v∈A}B(v), where B(v) is the 256 "
        "block centers of chunk v, and N(A)=⋃_{v∈A}N(v), N(v)={u:‖u−v‖∞≤1}. Then"
    )
    B.add_equation(
        doc,
        B.msub(B.mrun("E"), B.mrun("K")) + B.mrun("(X) = B(") + B.msub(B.mrun("T"), B.mrun("K")) +
        B.mrun("(X)) ∪ B(N(") + B.msub(B.mrun("P"), B.mrun("8")) + B.mrun("(X))).")
    )

    B.add_heading(doc, "4. Failure event")
    B.add_text_paragraph(doc, "The returned value and the failure indicator are")
    B.add_equation(
        doc,
        B.msub(B.mrun("V"), B.mrun("K")) + B.mrun("(X) = ") +
        B.msub(B.mrun("max"), B.mrun("c∈E_K(X)")) + B.msub(B.mrun("C"), B.mrun("X")) + B.mrun("(c),")
    )
    B.add_equation(
        doc,
        B.msub(B.mrun("F"), B.mrun("N,K")) + B.mrun("(X) = 1[ ") +
        B.msub(B.mrun("V"), B.mrun("K")) + B.mrun("(X) < M(X) ].")
    )
    B.add_text_paragraph(
        doc,
        "This is a value comparison. Multiple global maximizers and multiple returned "
        "centers do not introduce ambiguity. For iid Bernoulli(p) chunks,"
    )
    B.add_equation(
        doc,
        B.msub(B.mrun("P"), B.mrun("N,K")) + B.mrun("(p) = ") +
        B.nary("∑", B.mrun("X"), "",
               B.msub(B.mrun("F"), B.mrun("N,K")) + B.mrun("(X) ") +
               B.msup(B.mrun("p"), B.mrun("|X|")) +
               B.msup(B.mrun("(1−p)"), B.mrun("N²−|X|")) + B.mrun(","))
    )
    B.add_text_paragraph(
        doc,
        "where the sum is over all X∈{0,1}^{Gₙ} and |X|=∑ X_v. The default is p=0.1."
    )

    B.add_heading(doc, "5. Counterexamples")
    B.add_labeled_block(
        doc,
        "5.1 Minimal example (N=3,K=1).",
        "Take N=3 and X(0,0)=1, all other chunks 0. Then T_1={(0,0)}, "
        "V=253 and M=256; F=1. The true maximizer is at block center (39,7), "
        "outside the four chunks evaluated by the corner seed. N=1 and N=2 have no "
        "failure (2 and 16 configurations, exhaustive)."
    )
    B.add_labeled_block(
        doc,
        "5.2 Natural-scale singleton (N=8,K=1).",
        "Take one chunk at (7,7). The corner tie-break evaluates only the four chunks "
        "around (0,0); V=180 while M=256. This example is useful for visualization but "
        "is not minimal and relies on the corner tie-break."
    )
    B.add_labeled_block(
        doc,
        "5.3 Tie-free example (N=36,K=1).",
        "Place 337 slime chunks: 168 fully covered chunks around the hidden center "
        "v*=(10,10), plus 169 chunks around q=(28,28) chosen to minimize the best "
        "coverage in B(q)∪B(N(q)). Then S(q)=169, S(v*)=168, so q is the unique "
        "top-1 seed. The evaluated value is V=33426; at c*=(10,10,7,7) the coverage is "
        "C(c*)=43008. Thus M≥43008 and V<C(c*)≤M, a 22.3% gap. No tie-break is involved."
    )
    B.add_text_paragraph(
        doc,
        "The three examples separate three issues: map-boundary clipping, corner "
        "tie-breaking, and a genuine waste-asymmetry gap. The last one is the relevant "
        "counterexample for the full map, since it has a strict top score."
    )

    B.add_heading(doc, "6. Structural results")
    B.add_text_paragraph(
        doc,
        "For the default geometry, the block-grid kernel has the following exact counts. "
        "These are block-cell counts, not chunk-center distance counts."
    )
    B.add_table(
        doc,
        [
            ["Quantity", "Value"],
            ["Annulus area", "49,644 cells"],
            ["Chunks with positive annulus mass", "240"],
            ["Positive-mass chunks outside the 15×15 window", "28"],
            ["Window chunks with w=256", "168"],
            ["Window chunks with 0<w<256", "44"],
            ["Window chunks with w=0 (free square score)", "13"],
            ["Maximum invisible mass β_max", "2,546 cells"],
        ],
        widths=[12.0, 3.8],
    )
    B.add_text_paragraph(
        doc,
        "This corrects the earlier chunk-center table. The earlier table counted 188 "
        "ring chunks and 4 chunks outside the square, but the objective is a block-cell "
        "convolution: 240 chunks carry positive mass, 28 of them are outside the 15×15 "
        "window, and 13 window chunks have zero contribution."
    )

    B.add_labeled_block(
        doc,
        "Lemma 1 (efficiency bound).",
        "For every center c∈B(v), C(c)≤256 S_X(v)+β(c), where "
        "β(c)=∑_{u∉W(v)}X_u w_u(c) and β(c)≤β_max=2546."
    )
    B.add_text_paragraph(
        doc,
        "Proof. Split the sum defining C(c) into chunks inside W(v) and outside it. "
        "Inside the window, each chunk contributes at most 256 cells; outside, the total "
        "is exactly β(c). The maximum over the 16×16 center offsets is 2,546."
    )
    B.add_labeled_block(
        doc,
        "Lemma 2 (safety criterion).",
        "Let t_K be the K-th largest square score under the threshold definition. If "
        "M(X)≥256 t_K+β_max, then every global maximizer is evaluated and F(X)=0."
    )
    B.add_text_paragraph(
        doc,
        "Proof. Let c* be a global maximizer in chunk v*. By Lemma 1, "
        "S_X(v*)≥(M−β_max)/256≥t_K. Hence v*∈T_K, so c* is evaluated. Therefore V=M."
    )

    B.add_heading(doc, "7. Real-parameter threshold")
    B.add_text_paragraph(
        doc,
        "For N=3,750,000, K=4000 and p=0.1, the exact binomial tail gives t_K=55. "
        "The safety threshold is"
    )
    B.add_equation(
        doc,
        B.mrun("256 t_K + β_max = 256·55 + 2546 = 16,626.")
    )
    B.add_text_paragraph(
        doc,
        "The extreme-value scale of M is about 14,718–15,700 depending on the effective "
        "number of independent centers, i.e. 57.5–61.3 chunk units. The safety criterion "
        "requires 64.95 chunk units and is not met at the real parameters. The rank of "
        "the optimal chunk is therefore not controlled by a fixed argument; it is a "
        "boundary-crossing problem around S(v*)≈55."
    )
    B.add_table(
        doc,
        [
            ["S(v*)", "Estimated rank", "Relation to K=4000"],
            ["51", "3.1×10⁵", "far outside"],
            ["53", "4.0×10⁴", "outside"],
            ["54", "1.4×10⁴", "outside"],
            ["55", "4,799", "boundary"],
            ["56", "1,600", "inside"],
            ["58", "166", "inside"],
            ["60", "16", "inside"],
        ],
        widths=[3.5, 6.5, 7.0],
    )
    B.add_text_paragraph(
        doc,
        "Consequently P(F) is not usefully described as a 10⁻ˢ rare event. It is closer "
        "to P(S(v*)≤55), and is sensitive to a shift of 2–3 square-score points. The "
        "correct probability study is the distribution of the rank of the optimal chunk "
        "and the resulting boundary-crossing probability."
    )

    B.add_heading(doc, "8. What remains open")
    B.add_labeled_block(
        doc,
        "Problem 2 (exact small-grid count).",
        "Q1 is resolved; the remaining exact question is the count of failures for "
        "tie-free configurations in a reduced geometry that preserves R/s=8 and g=15. "
        "The earlier N=4,g=3,R=32 experiments are not isomorphic to the real geometry "
        "and should not be extrapolated."
    )
    B.add_labeled_block(
        doc,
        "Problem 3 (rank distribution).",
        "For actual parameters, determine or bound P(S(v*)≤t_K) and the conditional "
        "gap distribution, rather than asking for a single 10⁻ˢ bound on P(F)."
    )
    B.add_labeled_block(
        doc,
        "Problem 4 (design).",
        "Find a geometric prefilter whose evaluated set contains every chunk with "
        "S_X(v)≥t_K−Δ for a provable Δ; equivalently, replace the square score by a "
        "coarse annulus-kernel score. Expanding 3×3 polish to the 17×17 support is not "
        "a practical fix."
    )

    B.add_heading(doc, "9. Engineering implication")
    B.add_text_paragraph(
        doc,
        "The square prescan cannot be made sound by increasing K alone: the failing "
        "competitor can have a strictly larger square score. A geometric candidate "
        "kernel, or a second-stage screen that accounts for the 28 outside-window chunks "
        "and the 13 zero-contribution window chunks, is required. The 3×3 polish is not "
        "the missing component; its support is only 1 chunk, while the annulus spans "
        "8 chunks."
    )

    B.add_heading(doc, "10. Reproducibility")
    B.add_text_paragraph(
        doc,
        "The counterexample construction, independent direct-sum verification, minimal-N "
        "search and the real-scale calculations are in the supplied ring_problem "
        "directory: ring_model.py, verify_final.py, minimal_n.py, build_counterexample.py, "
        "extreme_values.py and FINDINGS.md. The synthetic trap harness in this repository "
        "is separate and is not evidence for the full geometry."
    )

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(14)
    r = p.add_run("Version: 2026-11-21 · Result note · Counterexamples are exact integer certificates")
    B.set_font(r, B.BODY, 8.2)
    r.font.color.rgb = B.MUTED

    doc.core_properties.title = "When Square-Density Seeds Miss the Annular Optimum"
    doc.core_properties.subject = "Counterexample study of square-density candidate generation"
    doc.core_properties.author = "SlimeFinder Research Note"
    doc.save(OUT)
    print(f"saved {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    build()
