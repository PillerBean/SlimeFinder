#!/usr/bin/env python3
"""Build a readable DOCX from the formal ring counterexample problem."""

from __future__ import annotations

import re
from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Inches, Pt, RGBColor

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "ring_counterexample_problem.md"
OUT = ROOT / "ring_counterexample_problem.docx"

BODY_FONT = "Calibri"
CJK_FONT = "Microsoft YaHei"
CODE_FONT = "Consolas"
ACCENT = RGBColor(0x1F, 0x3A, 0x5F)
MUTED = RGBColor(0x55, 0x5F, 0x6D)


def set_run_font(run, name: str, size: float | None = None, bold: bool | None = None):
    run.font.name = name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), CJK_FONT if name == BODY_FONT else name)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.bold = bold


def shade_paragraph(paragraph, fill: str):
    p_pr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    p_pr.append(shd)


def add_bottom_border(paragraph, color="B8C2CC", size="6"):
    p_pr = paragraph._p.get_or_add_pPr()
    p_bdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), size)
    bottom.set(qn("w:space"), "4")
    bottom.set(qn("w:color"), color)
    p_bdr.append(bottom)
    p_pr.append(p_bdr)


def add_page_number(paragraph):
    paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = paragraph.add_run("第 ")
    set_run_font(run, BODY_FONT, 8.5)
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = " PAGE "
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    r = paragraph.add_run()
    r._r.append(fld_begin)
    r._r.append(instr)
    r._r.append(fld_end)
    run2 = paragraph.add_run(" 页")
    set_run_font(run2, BODY_FONT, 8.5)


def add_inline(paragraph, text: str, size: float = 10.5):
    """Render `code` and plain text, without destroying surrounding runs."""
    parts = re.split(r"(`[^`]*`)", text)
    for part in parts:
        if not part:
            continue
        if part.startswith("`") and part.endswith("`") and len(part) >= 2:
            run = paragraph.add_run(part[1:-1])
            set_run_font(run, CODE_FONT, size - 0.5)
            run.font.color.rgb = RGBColor(0x1B, 0x3A, 0x5E)
        else:
            run = paragraph.add_run(part)
            set_run_font(run, BODY_FONT, size)


def add_table(document: Document, rows: list[list[str]]):
    if not rows:
        return
    cols = max(len(r) for r in rows)
    table = document.add_table(rows=0, cols=cols)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True
    for ri, row in enumerate(rows):
        cells = table.add_row().cells
        for ci in range(cols):
            text = row[ci] if ci < len(row) else ""
            cell = cells[ci]
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(1)
            p.paragraph_format.space_after = Pt(1)
            add_inline(p, text, 9.5 if ri else 9.8)
            if ri == 0:
                for run in p.runs:
                    run.bold = True
                shade_paragraph(p, "DCE6F1")
    document.add_paragraph().paragraph_format.space_after = Pt(2)


def add_code_block(document: Document, lines: list[str]):
    if not lines:
        return
    for i, line in enumerate(lines):
        p = document.add_paragraph()
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.left_indent = Cm(0.35)
        p.paragraph_format.right_indent = Cm(0.2)
        p.paragraph_format.line_spacing = 1.0
        p.paragraph_format.keep_together = True
        p.paragraph_format.keep_with_next = i < len(lines) - 1
        run = p.add_run(line if line else " ")
        set_run_font(run, CODE_FONT, 8.2)
        run.font.color.rgb = RGBColor(0x22, 0x2A, 0x33)
        shade_paragraph(p, "F2F4F7")
    document.add_paragraph().paragraph_format.space_after = Pt(2)


def parse_markdown(text: str):
    blocks = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            blocks.append(("code", code))
            continue
        if line.startswith("|"):
            table = []
            while i < len(lines) and lines[i].startswith("|"):
                raw = lines[i].strip()
                if not re.match(r"^\|?[\s:|-]+\|?$", raw):
                    cells = [c.strip() for c in raw.strip("|").split("|")]
                    table.append(cells)
                i += 1
            blocks.append(("table", table))
            continue
        m = re.match(r"^(#{1,3})\s+(.*)$", line)
        if m:
            blocks.append(("heading", len(m.group(1)), m.group(2)))
            i += 1
            continue
        if line.strip() == "---":
            blocks.append(("rule",))
            i += 1
            continue
        if line.strip() == "":
            i += 1
            continue
        paragraph = [line.rstrip()]
        i += 1
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "```", "|")):
            if lines[i].strip() == "---":
                break
            paragraph.append(lines[i].rstrip())
            i += 1
        blocks.append(("paragraph", " ".join(paragraph)))
    return blocks


def build():
    text = SRC.read_text(encoding="utf-8")
    blocks = parse_markdown(text)

    doc = Document()
    section = doc.sections[0]
    section.top_margin = Cm(1.8)
    section.bottom_margin = Cm(1.8)
    section.left_margin = Cm(2.0)
    section.right_margin = Cm(2.0)

    normal = doc.styles["Normal"]
    normal.font.name = BODY_FONT
    normal.font.size = Pt(10.5)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), CJK_FONT)
    normal.paragraph_format.space_after = Pt(5)
    normal.paragraph_format.line_spacing = 1.25

    header = section.header.paragraphs[0]
    header.text = "Ring Coverage Counterexample Problem"
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in header.runs:
        set_run_font(run, BODY_FONT, 8.5)
        run.font.color.rgb = MUTED

    footer = section.footer.paragraphs[0]
    add_page_number(footer)

    title_done = False
    first_numbered_section = True
    for block in blocks:
        kind = block[0]
        if kind == "heading":
            level, title = block[1], block[2]
            if level == 1:
                p = doc.add_paragraph()
                p.paragraph_format.space_before = Pt(6)
                p.paragraph_format.space_after = Pt(4)
                run = p.add_run(title)
                set_run_font(run, BODY_FONT, 20, bold=True)
                run.font.color.rgb = ACCENT
                add_bottom_border(p)
                title_done = True
            elif level == 2:
                p = doc.add_paragraph()
                p.paragraph_format.space_before = Pt(10)
                p.paragraph_format.space_after = Pt(3)
                p.paragraph_format.keep_with_next = True
                run = p.add_run(title)
                set_run_font(run, BODY_FONT, 13.5, bold=True)
                run.font.color.rgb = ACCENT
            else:
                p = doc.add_paragraph()
                p.paragraph_format.space_before = Pt(7)
                p.paragraph_format.space_after = Pt(2)
                run = p.add_run(title)
                set_run_font(run, BODY_FONT, 11.5, bold=True)
                run.font.color.rgb = RGBColor(0x2E, 0x4A, 0x6B)
            continue

        if kind == "paragraph":
            p = doc.add_paragraph()
            p.paragraph_format.first_line_indent = Cm(0)
            add_inline(p, block[1])
            continue

        if kind == "code":
            add_code_block(doc, block[1])
            continue

        if kind == "table":
            add_table(doc, block[1])
            continue

        if kind == "rule":
            p = doc.add_paragraph()
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            add_bottom_border(p, color="D0D7DE", size="4")

    # Document properties and a compact note.
    doc.core_properties.title = "Ring Coverage Counterexample Problem"
    doc.core_properties.subject = "Formal counterexample and probability problem for square-pre-filter ring optimization"
    doc.core_properties.author = "SlimeFinder"
    doc.save(OUT)
    print(f"saved {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    build()
