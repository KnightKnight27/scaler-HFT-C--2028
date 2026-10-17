#!/usr/bin/env python3
"""Render the two-page Markdown source using optional ReportLab (pip install reportlab)."""
from pathlib import Path
from xml.sax.saxutils import escape
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak

ROOT = Path(__file__).resolve().parents[1]
styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name='ReportBody', fontName='Helvetica', fontSize=9.3, leading=12,
                          spaceAfter=7, alignment=TA_LEFT))
styles['Heading1'].fontSize = 17
styles['Heading1'].leading = 20
styles['Heading2'].fontSize = 11
styles['Heading2'].leading = 14
styles['Heading2'].spaceBefore = 7
styles['Heading2'].spaceAfter = 5

def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont('Helvetica', 8)
    canvas.setFillColor(colors.HexColor('#555555'))
    canvas.drawString(0.65*inch, 0.4*inch, 'Rushi Jadhav | 10144 | Resting order book')
    canvas.drawRightString(7.85*inch, 0.4*inch, str(doc.page))
    canvas.restoreState()

story = []
text = (ROOT / 'docs/report.md').read_text()
blocks = text.split('\n\n')
for block in blocks:
    block = block.strip()
    if not block:
        continue
    if block == '<!-- PAGE BREAK -->':
        story.append(PageBreak())
    elif block.startswith('|'):
        rows = [[cell.strip() for cell in line.strip('|').split('|')]
                for line in block.splitlines() if not set(line.replace('|', '').strip()) <= set('-: ')]
        table = Table(rows, colWidths=[2.75*inch, 1.35*inch, 1.35*inch, 1.35*inch])
        table.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,0), colors.HexColor('#16324f')),
            ('TEXTCOLOR', (0,0), (-1,0), colors.white),
            ('FONTNAME', (0,0), (-1,0), 'Helvetica-Bold'),
            ('FONTSIZE', (0,0), (-1,-1), 8.5),
            ('LEADING', (0,0), (-1,-1), 11),
            ('BOTTOMPADDING', (0,0), (-1,-1), 4),
            ('TOPPADDING', (0,0), (-1,-1), 4),
            ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.HexColor('#f1f5f9'), colors.white]),
            ('ALIGN', (1,1), (-1,-1), 'RIGHT'),
            ('LINEBELOW', (0,0), (-1,0), 0.5, colors.HexColor('#16324f'))]))
        story.extend([table, Spacer(1, 8)])
    else:
        style = 'ReportBody'
        if block.startswith('# '):
            block, style = block[2:], 'Heading1'
        elif block.startswith('## '):
            block, style = block[3:], 'Heading2'
        story.append(Paragraph(escape(block).replace('\n', ' '), styles[style]))
doc = SimpleDocTemplate(str(ROOT / 'docs/report.pdf'), pagesize=(8.5*inch,11*inch),
                        rightMargin=0.65*inch, leftMargin=0.65*inch,
                        topMargin=0.55*inch, bottomMargin=0.65*inch,
                        title='Resting order book performance analysis', author='Rushi Jadhav')
doc.build(story, onFirstPage=footer, onLaterPages=footer)
print(ROOT / 'docs/report.pdf')
