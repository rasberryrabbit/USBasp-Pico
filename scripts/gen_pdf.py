#!/usr/bin/env python3
"""
USBasp-Pico 종합 문서 PDF 생성 스크립트
- ReportLab 사용
- 한국어 (NotoSerifKR) 지원
- 회로도/배선도/아키텍처 이미지 포함
- TOC 포함

출력: /home/z/my-project/usbasp-pico/download/USBasp-Pico_문서.pdf
"""

import os
import sys
import hashlib
import re

# ── ReportLab imports ───────────────────────────────────────────────────
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm, cm
from reportlab.lib.enums import TA_LEFT, TA_CENTER, TA_JUSTIFY
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase.pdfmetrics import registerFontFamily
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, PageBreak, Image, Table, TableStyle,
    KeepTogether, HRFlowable, Flowable
)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.platypus.doctemplate import PageTemplate, BaseDocTemplate
from reportlab.platypus.frames import Frame

# ── Font registration ──────────────────────────────────────────────────
FONT_DIR_KR = '/usr/share/fonts/truetype/wqy'
FONT_DIR_SC = '/usr/share/fonts/truetype/noto-serif-sc'
FONT_DIR_CHN = '/usr/share/fonts/truetype/chinese'

# Korean serif/sans font (WQY Zen Hei covers Hangul; used as NotoSerifKR/NotoSansKR)
# v1.9: switched from the bundled Noto KR fonts to WenQuanYi Zen Hei, which is
# installed system-wide and covers the Hangul syllables used in this document.
pdfmetrics.registerFont(TTFont('NotoSerifKR',
    f'{FONT_DIR_KR}/wqy-zenhei.ttc', subfontIndex=0))
pdfmetrics.registerFont(TTFont('NotoSerifKR-Bold',
    f'{FONT_DIR_KR}/wqy-zenhei.ttc', subfontIndex=0))
registerFontFamily('NotoSerifKR', normal='NotoSerifKR', bold='NotoSerifKR-Bold')

# Korean sans (same WQY Zen Hei)
pdfmetrics.registerFont(TTFont('NotoSansKR',
    f'{FONT_DIR_KR}/wqy-zenhei.ttc', subfontIndex=0))
pdfmetrics.registerFont(TTFont('NotoSansKR-Bold',
    f'{FONT_DIR_KR}/wqy-zenhei.ttc', subfontIndex=0))
registerFontFamily('NotoSansKR', normal='NotoSansKR', bold='NotoSansKR-Bold')

# English / math fallback (Liberation Serif for italic, NotoSerifSC for CJK glyphs)
pdfmetrics.registerFont(TTFont('NotoSerifSC',
    f'{FONT_DIR_SC}/NotoSerifSC-Regular.ttf'))
pdfmetrics.registerFont(TTFont('NotoSerifSC-Bold',
    f'{FONT_DIR_SC}/NotoSerifSC-Bold.ttf'))
registerFontFamily('NotoSerifSC', normal='NotoSerifSC', bold='NotoSerifSC-Bold')

# Monospace
pdfmetrics.registerFont(TTFont('Mono',
    '/usr/share/fonts/truetype/chinese/SarasaMonoSC-Regular.ttf'))
pdfmetrics.registerFont(TTFont('Mono-Bold',
    '/usr/share/fonts/truetype/chinese/SarasaMonoSC-Bold.ttf'))
registerFontFamily('Mono', normal='Mono', bold='Mono-Bold')

# ── Cascade palette (auto-generated) ───────────────────────────────────
PAGE_BG       = colors.HexColor('#f6f5f4')
SECTION_BG    = colors.HexColor('#efefed')
CARD_BG       = colors.HexColor('#eae9e6')
TABLE_STRIPE  = colors.HexColor('#ecebe8')
HEADER_FILL   = colors.HexColor('#564e37')
COVER_BLOCK   = colors.HexColor('#5b5647')
BORDER        = colors.HexColor('#cbc7bc')
ICON          = colors.HexColor('#806d35')
ACCENT        = colors.HexColor('#a68629')
ACCENT_2      = colors.HexColor('#509fb9')
TEXT_PRIMARY  = colors.HexColor('#232220')
TEXT_MUTED    = colors.HexColor('#88867e')

# ── Styles ──────────────────────────────────────────────────────────────
body_style = ParagraphStyle(
    name='Body', fontName='NotoSerifKR', fontSize=10.5, leading=18,
    alignment=TA_LEFT, wordWrap='CJK', firstLineIndent=2*4.5*mm,
    textColor=TEXT_PRIMARY
)

body_noindent = ParagraphStyle(
    name='BodyNoIndent', fontName='NotoSerifKR', fontSize=10.5, leading=18,
    alignment=TA_LEFT, wordWrap='CJK', textColor=TEXT_PRIMARY
)

h1_style = ParagraphStyle(
    name='H1', fontName='NotoSerifKR-Bold', fontSize=18, leading=26,
    alignment=TA_LEFT, spaceBefore=18, spaceAfter=10,
    textColor=HEADER_FILL, wordWrap='CJK'
)

h2_style = ParagraphStyle(
    name='H2', fontName='NotoSerifKR-Bold', fontSize=13, leading=20,
    alignment=TA_LEFT, spaceBefore=12, spaceAfter=6,
    textColor=HEADER_FILL, wordWrap='CJK'
)

h3_style = ParagraphStyle(
    name='H3', fontName='NotoSerifKR-Bold', fontSize=11, leading=16,
    alignment=TA_LEFT, spaceBefore=8, spaceAfter=4,
    textColor=ACCENT, wordWrap='CJK'
)

caption_style = ParagraphStyle(
    name='Caption', fontName='NotoSerifKR', fontSize=9, leading=12,
    alignment=TA_CENTER, spaceBefore=4, spaceAfter=10,
    textColor=TEXT_MUTED, wordWrap='CJK'
)

code_style = ParagraphStyle(
    name='Code', fontName='Mono', fontSize=8.5, leading=12,
    alignment=TA_LEFT, leftIndent=12, rightIndent=12,
    backColor=SECTION_BG, borderColor=BORDER, borderWidth=0.5,
    borderPadding=6, spaceBefore=6, spaceAfter=6,
    textColor=TEXT_PRIMARY
)

bullet_style = ParagraphStyle(
    name='Bullet', fontName='NotoSerifKR', fontSize=10.5, leading=18,
    alignment=TA_LEFT, wordWrap='CJK', leftIndent=18, bulletIndent=6,
    textColor=TEXT_PRIMARY
)

# ── TOC setup ──────────────────────────────────────────────────────────
toc_level0 = ParagraphStyle(
    name='TOC0', fontName='NotoSerifKR-Bold', fontSize=11, leading=18,
    leftIndent=0, spaceBefore=4, spaceAfter=2,
    textColor=TEXT_PRIMARY
)
toc_level1 = ParagraphStyle(
    name='TOC1', fontName='NotoSerifKR', fontSize=10, leading=16,
    leftIndent=18, spaceBefore=2, spaceAfter=2,
    textColor=TEXT_MUTED
)


class TocDocTemplate(BaseDocTemplate):
    def afterFlowable(self, flowable):
        if hasattr(flowable, 'bookmark_name'):
            level = getattr(flowable, 'bookmark_level', 0)
            text = getattr(flowable, 'bookmark_text', '')
            key = getattr(flowable, 'bookmark_key', '')
            self.notify('TOCEntry', (level, text, self.page, key))


def add_heading(text, style, level=0):
    key = f'h_{hashlib.md5(text.encode()).hexdigest()[:8]}'
    p = Paragraph(f'<a name="{key}"/>{text}', style)
    p.bookmark_name = key
    p.bookmark_level = level
    p.bookmark_text = text
    p.bookmark_key = key
    return p


# ── Page templates (header/footer) ─────────────────────────────────────
def first_page(canvas, doc):
    """Cover page - no header/footer."""
    canvas.saveState()
    # full-bleed background tint
    canvas.setFillColor(PAGE_BG)
    canvas.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
    canvas.restoreState()


def later_page(canvas, doc):
    """Body pages - header bar + page number."""
    canvas.saveState()
    canvas.setFillColor(PAGE_BG)
    canvas.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
    # Header line
    canvas.setStrokeColor(BORDER)
    canvas.setLineWidth(0.5)
    canvas.line(20*mm, A4[1] - 15*mm, A4[0] - 20*mm, A4[1] - 15*mm)
    # Header text - left
    canvas.setFont('NotoSerifKR', 8)
    canvas.setFillColor(TEXT_MUTED)
    canvas.drawString(20*mm, A4[1] - 13*mm, 'USBasp-Pico — RP2040 AVR Programmer')
    # Header text - right
    canvas.drawRightString(A4[0] - 20*mm, A4[1] - 13*mm, 'v1.9 / 2026-11-12')
    # Footer
    canvas.setStrokeColor(BORDER)
    canvas.line(20*mm, 15*mm, A4[0] - 20*mm, 15*mm)
    canvas.setFont('NotoSerifKR', 8)
    canvas.setFillColor(TEXT_MUTED)
    canvas.drawString(20*mm, 12*mm, 'Z.ai')
    canvas.drawRightString(A4[0] - 20*mm, 12*mm, f'{doc.page}')
    canvas.restoreState()


# ── Build content ──────────────────────────────────────────────────────
IMG_DIR = '/home/z/my-project/usbasp-pico/docs/images'
PDF_OUT = '/home/z/my-project/work/usbasp-pico/docs/USBasp-Pico_문서.pdf'

story = []

# ─── COVER ────────────────────────────────────────────────────────────
story.append(Spacer(1, 60*mm))
cover_title_style = ParagraphStyle(
    name='CoverTitle', fontName='NotoSerifKR-Bold', fontSize=36, leading=44,
    alignment=TA_CENTER, textColor=TEXT_PRIMARY, wordWrap='CJK'
)
cover_subtitle_style = ParagraphStyle(
    name='CoverSub', fontName='NotoSerifKR', fontSize=16, leading=24,
    alignment=TA_CENTER, textColor=TEXT_MUTED, wordWrap='CJK'
)
cover_meta_style = ParagraphStyle(
    name='CoverMeta', fontName='NotoSerifKR', fontSize=11, leading=16,
    alignment=TA_CENTER, textColor=TEXT_MUTED, wordWrap='CJK'
)
story.append(Paragraph('USBasp-Pico', cover_title_style))
story.append(Spacer(1, 10*mm))
story.append(Paragraph('라즈베리파이 피코 (RP2040) 용<br/>USBasp 호환 AVR 프로그래머', cover_subtitle_style))
story.append(Spacer(1, 30*mm))
# accent bar
story.append(HRFlowable(width='40%', thickness=2, color=ACCENT, hAlign='CENTER'))
story.append(Spacer(1, 8*mm))
story.append(Paragraph('TinyUSB Vendor-Class + ISP/TPI/HVSP 백엔드', cover_meta_style))
story.append(Paragraph('5V / 3.3V AVR 지원 + 12V HVSP 모드', cover_meta_style))
story.append(Spacer(1, 50*mm))
story.append(Paragraph('버전 1.9  /  2026-11-12', cover_meta_style))
story.append(Paragraph('Z.ai', cover_meta_style))
story.append(PageBreak())

# ─── TOC ──────────────────────────────────────────────────────────────
toc = TableOfContents()
toc.levelStyles = [toc_level0, toc_level1]
story.append(add_heading('목차', h1_style, level=0))
story.append(Spacer(1, 6*mm))
story.append(toc)
story.append(PageBreak())

# ─── Chapter 1: Project Overview ──────────────────────────────────────
story.append(add_heading('1. 프로젝트 개요', h1_style, level=0))
story.append(Paragraph(
    '<b>USBasp-Pico</b>는 라즈베리파이 피코 (RP2040)를 기반으로, 원본 USBasp '
    '(Thomas Fischl 저, mariusgreuel 포크 기반)와 완벽하게 호환되는 AVR '
    '프로그래머 펌웨어입니다. 호스트 PC의 avrdude은 일반 USBasp로 인식하며, '
    '별도의 드라이버나 설정 변경 없이 <font face="Mono">-c usbasp</font> '
    '옵션으로 사용할 수 있습니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '원본 USBasp는 ATmega8 + V-USB(소프트웨어 USB, 저속 1.5 Mbps) 조합으로 '
    '구성되어 있으나, RP2040은 하드웨어 USB 풀스피드 (12 Mbps) 컨트롤러와 '
    '듀얼 코어 Cortex-M0+를 갖추고 있어 훨씬 빠르고 안정적인 통신이 가능합니다. '
    '또한 플래시 2MB, RAM 264KB로 고용량 AVR (256KB 이상 플래시) 프로그래밍에도 '
    '제약이 없습니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>주요 특징</b>', h3_style))
features = [
    '<b>USBasp 완벽 호환</b> — VID=0x16C0, PID=0x05DC, 동일 프로토콜',
    '<b>고용량 AVR 지원</b> — ATmega2560 등 256KB 플래시 칩까지 (SETLONGADDRESS 지원)',
    '<b>TPI 모드</b> — ATtiny 4/5/9/10/20/40 등 소형 AVR 프로그래밍',
    '<b>HVSP 12V 모드 (옵션)</b> — RESET 비활성화된 ATtiny85 등 복구용',
    '<b>SCK 자동/수동 설정</b> — 500 Hz ~ 1.5 MHz 12단계',
    '<b>UF2 자동 빌드</b> — Pico BOOTSEL 모드로 드래그 앤 드롭 플래싱',
    '<b>확장 핀 할당</b> — ISP + HVSP 동시 핀 매핑, LED 상태 표시',
]
for f in features:
    story.append(Paragraph(f'• {f}', bullet_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '본 프로젝트는 mariusgreuel/USBasp 포크의 프로토콜 사양을 그대로 유지하면서, '
    '구현을 RP2040 + TinyUSB로 옮긴 것입니다. 원본 V-USB 기반 ATmega8 펌웨어와 '
    '호환되는 모든 avrdude 기능을 그대로 사용할 수 있습니다.', body_style))
story.append(PageBreak())

# ─── Chapter 2: USBasp protocol analysis ──────────────────────────────
story.append(add_heading('2. USBasp 프로토콜 분석', h1_style, level=0))
story.append(Paragraph(
    'USBasp는 V-USB 기반의 ATmega8 펌웨어로, USB low-speed (1.5 Mbps)에서 '
    '동작합니다. 모든 명령은 EP0 vendor-specific control transfer로 전달되며, '
    'bmRequestType은 0x40 (OUT) 또는 0xC0 (IN), bRequest는 함수 코드(1~127)로 '
    '구성됩니다. wValue와 wIndex는 명령 파라미터를 전달하고, 데이터 스테이지는 '
    '블록 전송(READFLASH/WRITEFLASH 등)에 사용됩니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>USBtinyISP 대비 USBasp 장점</b>', h3_style))
data = [
    ['항목', 'USBtinyISP', 'USBasp (본 프로젝트)'],
    ['고용량 AVR (>128KB)', '미지원 (1 byte addr)', '지원 (SETLONGADDRESS)'],
    ['USB 속도', '저속 1.5 Mbps', '풀스피드 12 Mbps'],
    ['TPI 모드', '미지원', '지원 (USBASP_FUNC_TPI_*)'],
    ['블록 전송', 'EP0 vendor only', 'EP0 데이터 스테이지 4KB'],
    ['SCK 단계', '8 단계', '12 단계 (500 Hz~1.5 MHz)'],
    ['avrdude 지원', '-c usbtiny', '-c usbasp (기본)'],
]
t = Table(data, colWidths=[50*mm, 50*mm, 60*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTSIZE', (0,0), (-1,0), 10),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,1), (-1,-1), 9),
    ('ALIGN', (0,0), (-1,-1), 'LEFT'),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 4),
    ('BOTTOMPADDING', (0,0), (-1,-1), 4),
]))
story.append(t)
story.append(Paragraph('표 1. USBtinyISP vs USBasp 비교', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    'mariusgreuel 포크는 원본 USBasp와 프로토콜 레벨에서 동일합니다. 다만 '
    '윈도우 드라이버 패키징, 빌드 시스템 정리, 일부 USB 설명자 개선이 추가되어 '
    '있으며, 본 프로젝트는 이 포크의 프로토콜 상수를 그대로 사용합니다. '
    '확장된 명령어(USBASP_FUNC_TPI_*, USBASP_FUNC_GETCAPABILITIES)도 모두 '
    '구현되어 avrdude의 TPI 프로그래밍을 그대로 지원합니다.', body_style))
story.append(PageBreak())

# ─── Chapter 3: Firmware architecture ─────────────────────────────────
story.append(add_heading('3. RP2040 펌웨어 아키텍처', h1_style, level=0))
story.append(Paragraph(
    '펌웨어는 TinyUSB vendor-class 드라이버 위에 USBasp 프로토콜을 구현하고, '
    '하위 백엔드로 ISP / TPI / HVSP 세 가지 모드를 분리한 구조입니다. '
    'USB 통신은 RP2040의 하드웨어 USB 컨트롤러가 처리하며, GPIO bit-bang '
    'SPI는 코어0에서 실행됩니다. 코어1은 미사용 상태로 두어 향후 확장 '
    '(예: USB CDC 디버그 채널)에 대비합니다.', body_style))
story.append(Spacer(1, 4*mm))

# Architecture diagram
img_path = os.path.join(IMG_DIR, 'block_diagram.png')
if os.path.exists(img_path):
    img = Image(img_path, width=170*mm, height=110*mm)
    story.append(img)
    story.append(Paragraph('그림 1. USBasp-Pico 펌웨어 아키텍처 블록 다이어그램', caption_style))

story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>펌웨어 파일 구조</b>', h3_style))
files_data = [
    ['파일', '역할', '라인 수'],
    ['main.c', 'USB 초기화, LED, 메인 루프', '~100'],
    ['usb_descriptors.c', 'USB 장치/설정/문자열 디스크립터', '~150'],
    ['usbasp_protocol.c', 'USBasp vendor-class 프로토콜 핸들러', '~280'],
    ['isp.c / isp.h', 'ISP bit-bang 백엔드 (500Hz~1.5MHz)', '~250'],
    ['tpi.c / tpi.h', 'TPI 프로토콜 백엔드 (ATtiny 4/5/9/10)', '~150'],
    ['hvsp.c / hvsp.h', 'HVSP 12V 모드 백엔드 (ATtiny85 등)', '~200'],
    ['usbasp.h / usbasp_protocol.h', '공용 상수/핀 정의', '~80'],
    ['tusb_config.h', 'TinyUSB 설정 (Vendor 클래스)', '~70'],
    ['CMakeLists.txt', '빌드 설정 (Pico SDK + TinyUSB)', '~70'],
]
t = Table(files_data, colWidths=[50*mm, 80*mm, 30*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('ALIGN', (2,0), (2,-1), 'CENTER'),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 2. 펌웨어 소스 파일 구조', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '프로토콜 핸들러는 TinyUSB의 <font face="Mono">tud_vendor_control_xfer_cb</font> '
    '콜백으로 진입합니다. SETUP 단계에서 bRequest를 dispatch하여 ISP/TPI/HVSP '
    '백엔드 함수를 호출하고, 데이터 스테이지는 <font face="Mono">tud_control_xfer</font> '
    '한 번의 호출로 4KB 단위 전송을 처리합니다. 쓰기 명령(WRITEFLASH 등)은 ACK '
    '단계에서 4KB scratch 버퍼의 데이터를 처리합니다.', body_style))
story.append(PageBreak())

# ─── Chapter 4: Hardware & pinout ────────────────────────────────────
story.append(add_heading('4. 하드웨어 구성 및 핀 할당', h1_style, level=0))
story.append(Paragraph(
    '라즈베리파이 피코의 GPIO는 3.3V I/O입니다. 5V AVR 타겟을 프로그래밍하려면 '
    '레벨시프터(TXS0108E, 74LVC245 등)가 필요하며, 3.3V AVR은 Pico와 직접 연결이 '
    '가능합니다. ISP는 6-pin 표준 커넥터, HVSP는 4개의 신호선 + 12V 제어선으로 '
    '구성됩니다. Pico의 온보드 LED(GP25) 대신 GP6을 상태 표시용으로 사용합니다.', body_style))
story.append(Spacer(1, 4*mm))

img_path = os.path.join(IMG_DIR, 'pinout_pico.png')
if os.path.exists(img_path):
    img = Image(img_path, width=120*mm, height=140*mm)
    story.append(img)
    story.append(Paragraph('그림 2. USBasp-Pico 핀 할당도', caption_style))

story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>ISP 핀 할당 (기본)</b>', h3_style))
isp_data = [
    ['Pico 핀', 'ISP 신호', '방향', '설명'],
    ['GP2', 'SCK', '출력', 'SPI 클럭 (500 Hz ~ 1.5 MHz)'],
    ['GP3', 'MOSI / HVSP SDI', '출력', 'Pico -> 타겟 데이터 (HVSP SDI 와 공유, v1.9)'],
    ['GP4', 'MISO', '입력', '타겟 -> Pico 데이터'],
    ['GP5', 'RESET', '출력', '타겟 RESET (활성 LOW)'],
    ['GP6', 'LED', '출력', '활동 표시 LED (활성 LOW)'],
    ['GND', 'GND', '공통', '접지 (타겟과 공통 필수)'],
    ['3V3', 'VCC', '출력', '3.3V AVR 타겟 전원 직접 공급'],
    ['VBUS', '5V', '출력', '5V AVR 타겟 (레벨시프터経由)'],
]
t = Table(isp_data, colWidths=[25*mm, 25*mm, 20*mm, 90*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 3. ISP 핀 할당', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>HVSP 핀 할당 (옵션, USBASP_PICO_ENABLE_HVSP=1 시) - v1.9</b>', h3_style))
hvsp_data = [
    ['Pico 핀', 'HVSP 신호', 'ATtiny85 핀', '설명'],
    ['GP3',  'SDI (공유)', 'PB1 (6)', '데이터 입력, ISP MOSI 와 동일 핀 공유 (v1.9)'],
    ['GP7',  'SCI',         'PB0 (5)', 'HVSP 클럭'],
    ['GP8',  'SDO',         'PB2 (7)', '데이터 출력 (타겟 -> Pico)'],
    ['GP9',  'SII',         'PB3 (2)', '명령어 입력'],
    ['GP10', '12V EN',      '부스트 ON', '12V 부스트컨버터 인에이블'],
    ['GP11', '(free)',      '-',        'v1.9 에서 사용자 자유 핀으로 반환'],
]
t = Table(hvsp_data, colWidths=[25*mm, 30*mm, 35*mm, 70*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 4. HVSP 핀 할당 (ATtiny85 기준)', caption_style))
story.append(PageBreak())

# ─── Chapter 5: Wiring ─────────────────────────────────────────────────
story.append(add_heading('5. 배선도', h1_style, level=0))

story.append(add_heading('5.1 5V AVR 타겟 + 레벨시프터', h2_style, level=1))
story.append(Paragraph(
    '5V AVR (ATmega328P @5V, ATtiny85 @5V 등)를 프로그래밍할 때는 Pico의 3.3V '
    'I/O를 5V로 변환해야 합니다. TXS0108E (양방향 자동 방향 전환) 또는 '
    '74LVC245 (단방향, MISO/MOSI 방향 고정 필요) 등의 레벨시프터를 사용합니다. '
    '레벨시프터의 HVcc는 타겟 전압(5V)과 같게 설정하고, 타겟 VCC는 Pico VBUS에서 '
    '직접 공급합니다.', body_noindent))
story.append(Spacer(1, 3*mm))
img_path = os.path.join(IMG_DIR, 'wiring_basic.png')
if os.path.exists(img_path):
    img = Image(img_path, width=170*mm, height=110*mm)
    story.append(img)
    story.append(Paragraph('그림 3. 5V AVR 타겟 + 레벨시프터 배선도', caption_style))

story.append(Spacer(1, 4*mm))
story.append(add_heading('5.2 3.3V AVR 타겟 직접 연결', h2_style, level=1))
story.append(Paragraph(
    '3.3V 동작 AVR (ATmega328P @3.3V, ATtiny85 @3.3V 등)은 레벨시프터 없이 '
    'Pico와 직접 연결할 수 있습니다. Pico의 3V3 핀에서 타겟 VCC를 공급하고, '
    '모든 신호선은 3.3V 레벨로 직접 연결합니다. Pico의 GPIO는 3.3V tolerant이므로 '
    'MISO 입력도 안전합니다.', body_noindent))
story.append(Spacer(1, 3*mm))
img_path = os.path.join(IMG_DIR, 'wiring_3v3.png')
if os.path.exists(img_path):
    img = Image(img_path, width=170*mm, height=100*mm)
    story.append(img)
    story.append(Paragraph('그림 4. 3.3V AVR 타겟 직접 연결 배선도', caption_style))

story.append(PageBreak())
story.append(add_heading('5.3 12V HVSP 모드 (ATtiny85 복구)', h2_style, level=1))
story.append(Paragraph(
    'RESET 핀이 RSTDISBL 퓨즈로 비활성화되었거나, 클럭 설정이 잘못되어 일반 ISP '
    '모드로 진입할 수 없는 ATtiny85 (및 ATtiny25/45/13/24/44/84) 칩은 12V HVSP '
    '(High-Voltage Serial Programming) 모드로 복구할 수 있습니다. RESET 핀에 '
    '12V를 인가하면 백도어 프로그래밍 모드로 진입합니다. 본 펌웨어는 HVSP '
    '백엔드를 포함하고 있으나, avrdude의 -c usbasp 모드는 HVSP를 지원하지 않으므로 '
    '별도의 Python 호스트 도구가 필요합니다.', body_noindent))
story.append(Spacer(1, 3*mm))
img_path = os.path.join(IMG_DIR, 'wiring_hvsp.png')
if os.path.exists(img_path):
    img = Image(img_path, width=180*mm, height=120*mm)
    story.append(img)
    story.append(Paragraph('그림 5. HVSP 12V 모드 배선도 (ATtiny85)', caption_style))

story.append(PageBreak())
# ─── Chapter 6: HVSP details ──────────────────────────────────────────
story.append(add_heading('6. 12V HVSP 모드 상세', h1_style, level=0))
story.append(Paragraph(
    'ATtiny85의 HVSP 프로토콜은 11비트 데이터/명령 프레임을 4개 핀(SDI, SDO, '
    'SII, SCI)으로 직렬 전송합니다. 진입 시퀀스는 다음과 같습니다:', body_style))
story.append(Spacer(1, 3*mm))
seq_text = """1. 타겟 VCC 인가 (5V, 1.8V 이상에서 20us 이내 상승)
2. SDI/SII/SDO 라인을 LOW 로 유지
3. RESET 을 LOW 로 설정 (0V)
4. 12V 를 RESET 핀에 인가 (11.5 ~ 12.5V)
5. 최소 10us 대기
6. SDO 를 입력으로 전환 (드라이브 충돌 방지)
7. 최소 300us 대기 후 직렬 명령 전송 시작"""
story.append(Paragraph(seq_text.replace('\n', '<br/>'), body_noindent))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '12V 생성은 외부 부스트컨버터가 필요합니다. MAX662A (12V 30mA 전용 IC, '
    '가장 간단) 또는 MC34063A + 인덕터 + 다이오드 조합을 사용할 수 있습니다. '
    'RP2040-Zero의 GP10 출력으로 부스트컨버터의 EN (또는 SHDN) 핀을 제어하여 '
    'HVSP 모드 진입 시에만 12V를 인가합니다 (v1.9 에서 12V_EN 이 GP11 에서 GP10 '
    '으로 이동했습니다). 12V는 RESET 핀에만 인가되어야 하며, '
    'VCC는 5V를 유지해야 합니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>HVSP 핀 할당 (RP2040-Zero, v1.9: MOSI/SDI 공유)</b>', h3_style))
hvsp_pin_data = [
    ['Pico 핀', 'HVSP 신호', 'ATtiny85 핀', '방향', '설명'],
    ['GP3',  'SDI (공유)', 'PB1 (6)', '출력', '데이터 입력, ISP MOSI 와 동일 핀 공유 (v1.9)'],
    ['GP7',  'SCI',         'PB0 (5)', '출력', 'HVSP 클럭'],
    ['GP8',  'SDO',         'PB2 (7)', '입력', '데이터 출력 (타겟 -> Pico)'],
    ['GP9',  'SII',         'PB3 (2)', '출력', '명령어 입력'],
    ['GP10', '12V_EN',      '부스트 ON', '출력', '12V 부스트컨버터 인에이블'],
    ['GP11', '(free)',      '-',         '-',   'v1.9 에서 사용자 자유 핀으로 반환'],
]
t = Table(hvsp_pin_data, colWidths=[20*mm, 25*mm, 30*mm, 20*mm, 70*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 4. HVSP 핀 할당 (RP2040-Zero, v1.9 MOSI/SDI 공유)', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '<b>RP2040-Zero 핀 선택 이유</b>: GP16 은 온보드 WS2812 RGB LED 에 연결되어 '
    '있어 일반 GPIO 로 사용하기 어렵습니다. GP22-GP25 는 바닥면 솔더 패드로만 '
    '접근 가능하여 빵판 실험에 부적합합니다. v1.9 부터는 ISP MOSI (GP3) 와 '
    'HVSP SDI 를 동일한 핀에 공유합니다. 두 신호는 모두 Pico -> 타겟 출력이며 '
    'MSB 우선 직렬 전송이고, ISP 와 HVSP 는 동시에 활성화되지 않으므로 충돌이 '
    '없습니다. HVSP 신호는 GP3 (SDI 공유) + GP7-GP10 (SCI/SDO/SII/12V_EN) 에 '
    '할당되며, GP11 은 사용자 자유 핀으로 반환됩니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>HVSP 사용법 (Python 호스트 도구)</b>', h3_style))
story.append(Paragraph(
    'avrdude 의 USBasp 드라이버는 HVSP 명령을 지원하지 않으므로, 본 프로젝트는 '
    '별도의 Python 호스트 도구 <font face="Mono">scripts/usbasp_pico_hvsp.py</font> '
    '를 제공합니다. 이 도구는 pyusb 를 통해 USB vendor control transfer (요청 코드 '
    '200-207)를 전송하여 펌웨어의 HVSP 백엔드를 구동합니다.', body_style))
story.append(Paragraph(
    '<font face="Mono">pip install pyusb<br/>'
    '<br/>'
    '# 시그니처 읽기 (12V 진입 검증)<br/>'
    'python3 usbasp_pico_hvsp.py readsig<br/>'
    '<br/>'
    '# 모든 퓨즈 읽기<br/>'
    'python3 usbasp_pico_hvsp.py readfuses<br/>'
    '<br/>'
    '# 칩 지우기 (락 비트도 클리어)<br/>'
    'python3 usbasp_pico_hvsp.py erase<br/>'
    '<br/>'
    '# 퓨즈 쓰기 (예: ATtiny85 기본값)<br/>'
    'python3 usbasp_pico_hvsp.py writefuse low 0x62<br/>'
    'python3 usbasp_pico_hvsp.py writefuse high 0xD7<br/>'
    'python3 usbasp_pico_hvsp.py writefuse ext 0xFE<br/>'
    '<br/>'
    '# 플래시 256 바이트 덤프<br/>'
    'python3 usbasp_pico_hvsp.py dumpflash 0 256 > flash.bin</font>',
    code_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph(
    '<b>HVSP vendor 요청 코드 (USBasp-Pico 전용)</b>: 본 프로젝트는 원본 USBasp '
    '프로토콜과의 호환성을 유지하기 위해, 표준 USBasp 요청 (1-16, 127)은 그대로 '
    '두고 HVSP 전용 요청을 200-207 범위에 추가했습니다. avrdude -c usbasp 플로우는 '
    '이 범위를 접근하지 않으므로 일반 ISP/TPI 프로그래밍은 영향받지 않습니다.', body_style))
story.append(Spacer(1, 3*mm))
hvsp_req_data = [
    ['코드', '이름', '방향', '용도'],
    ['200', 'HVSP_CONNECT',    'OUT', '12V 인가 + HVSP 모드 진입'],
    ['201', 'HVSP_DISCONNECT', 'OUT', '12V 차단 + 핀 해제'],
    ['202', 'HVSP_CHIP_ERASE', 'OUT', '칩 지우기 (락 비트 포함)'],
    ['203', 'HVSP_READ_SIG',   'IN',  '3바이트 시그니처 읽기'],
    ['204', 'HVSP_READ_FUSE',   'IN',  '퓨즈 읽기 (low/high/ext)'],
    ['205', 'HVSP_WRITE_FUSE', 'OUT', '퓨즈 쓰기 (low/high/ext)'],
    ['206', 'HVSP_READ_FLASH',  'IN',  '플래시 1바이트 읽기'],
    ['207', 'HVSP_WRITE_FLASH', 'OUT', '플래시 16비트 워드 쓰기'],
]
t = Table(hvsp_req_data, colWidths=[15*mm, 50*mm, 20*mm, 80*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 5. USBasp-Pico HVSP vendor 요청 코드', caption_style))
story.append(PageBreak())

# ─── Chapter 6.5: SWIO (CH32V003) ───────────────────────────────────────
story.append(add_heading('6.5 SWIO 모드 (CH32V003 RISC-V)', h1_style, level=0))
story.append(Paragraph(
    '본 프로젝트는 WCH CH32V003 RISC-V MCU 의 단선 디버그/프로그래밍 인터페이스인 '
    '<b>SWIO (Single-Wire I/O)</b> 를 추가로 지원합니다. SWIO 는 USB CDC ACM 가상 '
    '시리얼 포트를 통해 ASCII 명령으로 구동됩니다. avrdude 와는 별도 경로이므로 '
    'USBasp 호환성에 영향을 주지 않습니다.', body_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph(
    '<b>SWIO 핀 할당</b>: GP12 (RP2040-Zero 엣지 핀, ISP/HVSP 와 충돌 없음). '
    'SWIO 라인은 오픈드레인이므로 외부 4.7k~10k 풀업 저항이 필요합니다. '
    'CH32V003 는 3.3V 동작이므로 Pico 3V3 핀에서 직접 전원을 공급할 수 있습니다.', body_style))
story.append(Spacer(1, 3*mm))
img_path = os.path.join(IMG_DIR, 'wiring_swio.png')
if os.path.exists(img_path):
    img = Image(img_path, width=170*mm, height=95*mm)
    story.append(img)
    story.append(Paragraph('그림 6. SWIO 모드 배선도 (CH32V003)', caption_style))

story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>SWIO 프레임 포맷</b>', h3_style))
story.append(Paragraph(
    'SWIO 는 1비트 시작(0) + 7비트 레지스터 주소 (MSB 우선) + 1비트 R/W '
    '(0=읽기, 1=쓰기) + 32비트 데이터 (LSB 우선) 로 구성된 35비트 프레임을 '
    '사용합니다. RP2040 GPIO bit-bang 으로 ~125 kHz 클럭을 생성합니다.', body_noindent))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>CDC ASCII 명령 프로토콜</b>', h3_style))
swio_cmd_data = [
    ['명령', '인자', '응답', '설명'],
    ['?',    '없음',     "+",         'CDC 통신 테스트'],
    ['i',    '없음',     "!",         'SWIO 핀 초기화'],
    ['s',    '없음',     "+",         'SWIO 리셋 (50 high clocks)'],
    ['p',    '없음',     "+",         '타겟 전원 ON (SWIO idle high)'],
    ['P',    '없음',     "+",         '타겟 전원 OFF'],
    ['r',    'reg',      '4 hex bytes + "\\n"', 'WCH 디버그 레지스터 읽기'],
    ['w',    'reg val',  "+",         'WCH 디버그 레지스터 쓰기'],
]
t = Table(swio_cmd_data, colWidths=[15*mm, 35*mm, 45*mm, 65*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 6. SWIO CDC ASCII 명령 프로토콜', caption_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>Python 호스트 도구 사용법</b>', h3_style))
story.append(Paragraph(
    '<font face="Mono">pip install pyserial<br/>'
    '<br/>'
    '# CDC 포트 확인 (Linux: /dev/ttyACM0, macOS: /dev/cu.usbmodem*)<br/>'
    'ls /dev/ttyACM*<br/>'
    '<br/>'
    '# 연결 테스트<br/>'
    'python3 scripts/usbasp_pico_swio.py --port /dev/ttyACM0 ping<br/>'
    '<br/>'
    '# SWIO 초기화<br/>'
    'python3 scripts/usbasp_pico_swio.py --port /dev/ttyACM0 init<br/>'
    '<br/>'
    '# 디버그 레지스터 0x06 읽기 (데이터 전송 레지스터)<br/>'
    'python3 scripts/usbasp_pico_swio.py --port /dev/ttyACM0 r 06<br/>'
    '<br/>'
    '# 디버그 레지스터 0x7F = 0x12345678 쓰기<br/>'
    'python3 scripts/usbasp_pico_swio.py --port /dev/ttyACM0 w 7F 12345678<br/>'
    '<br/>'
    '# SWIO 리셋 (50 high clocks)<br/>'
    'python3 scripts/usbasp_pico_swio.py --port /dev/ttyACM0 reset</font>', code_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>minichlink 호환성 (CDC binary 모드)</b>', h3_style))
story.append(Paragraph(
    'CDC 인터페이스는 두 가지 프로토콜을 동시에 지원합니다 (첫 바이트로 자동 판별):', body_style))
story.append(Paragraph(
    '<b>1) Binary 모드 (Ardulink 호환, minichlink 사용)</b>: 첫 바이트가 '
    '<font face="Mono">r</font>/<font face="Mono">w</font>/<font face="Mono">p</font>/'
    '<font face="Mono">P</font> 인 경우. 개행 문자 없이 고정 길이 바이너리 프레임.', body_noindent))
mc_data = [
    ['명령', '전송', '응답', '비고'],
    ['r',  "0x72, reg7",         "4 bytes LE value",  'minichlink ReadReg32'],
    ['w',  "0x77, reg7, u32 LE", "0x2B ('+')",        'minichlink WriteReg32'],
    ['p',  "0x70",               "0x2B ('+')",        '전원 ON / SWIO init'],
    ['P',  "0x50",               "0x2B ('+')",        '전원 OFF'],
]
t = Table(mc_data, colWidths=[15*mm, 50*mm, 35*mm, 65*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 8. CDC binary 모드 명령 (Ardulink / minichlink 호환)', caption_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph(
    '<b>2) ASCII 모드 (터미널 / 우리 도구)</b>: 첫 바이트가 '
    '<font face="Mono">?</font>/<font face="Mono">i</font>/<font face="Mono">s</font> '
    '인 경우. 줄바꿈으로 종료되는 텍스트 명령. <font face="Mono">?</font> -> '
    '<font face="Mono">"+\\n"</font>, <font face="Mono">i</font> -> '
    '<font face="Mono">"!\\n"</font> (SWIO init), <font face="Mono">s</font> -> '
    '<font face="Mono">"+\\n"</font> (50 high clocks 리셋).', body_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph(
    '<b>시작 시 동기 문자</b>: 펌웨어 부팅 후 CDC 포트에 '
    '<font face="Mono">"!"</font> (1 바이트, 개행 없음) 를 출력합니다. '
    '이것은 minichlink 의 <font face="Mono">TryInit_Ardulink</font> 가 기다리는 '
    'Ardulink 동기 문자입니다.', body_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>minichlink 직접 사용</b>', h3_style))
story.append(Paragraph(
    'CDC 포트가 Ardulink 프로토콜을 말하므로, minichlink 를 직접 연결하여 '
    'CH32V003 플래시 프로그래밍이 가능합니다. minichlink 의 -A (또는 '
    '--ardulink) 플래그로 Ardulink 모드 강제:', body_style))
story.append(Paragraph(
    '<font face="Mono"># minichlink 빌드 (ch32v003fun 저장소)<br/>'
    'cd ch32v003fun/minichlink<br/>'
    'make<br/>'
    '<br/>'
    '# CDC 포트로 CH32V003 플래시 쓰기<br/>'
    'minichlink -A -d /dev/ttyACM0 -w firmware.bin 0x08000000<br/>'
    '<br/>'
    '# 칩 정보 확인<br/>'
    'minichlink -A -d /dev/ttyACM0 -i<br/>'
    '<br/>'
    '# 칩 지우기<br/>'
    'minichlink -A -d /dev/ttyACM0 -E</font>',
    code_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph(
    '<b>참고</b>: minichlink 버전에 따라 -A 대신 -C ardulink 또는 '
    '별도의 인터페이스 설정이 필요할 수 있습니다. 최신 minichlink 는 '
    'Ardulink 를 자동 감지하므로 -d 만으로도 동작할 수 있습니다.', body_style))
story.append(PageBreak())
story.append(Paragraph(
    '본 도구는 두 가지 USB 전송 방식을 모두 지원합니다 (자동 감지):', body_style))
story.append(Paragraph(
    '<b>1) Vendor request (pyusb, 기본)</b>: USBasp vendor control transfer '
    '로 SWIO 명령 전송. USBasp-Pico 전용 요청 코드 210-213 사용. 별도 '
    '시리얼 포트 없이 VID/PID 만으로 장치 접근.', body_noindent))
story.append(Paragraph(
    '<b>2) CDC ACM (pyserial)</b>: 가상 시리얼 포트 (/dev/ttyACM0) 로 '
    'ASCII 명령 전송. 사람 친화적 터미널 인터페이스.', body_noindent))
story.append(Spacer(1, 3*mm))
swio_req_data = [
    ['코드', '이름', '방향', '용도'],
    ['210', 'SWIO_INIT',         'OUT', 'SWIO 핀 초기화 (오픈드레인, idle high)'],
    ['211', 'SWIO_RESET_TARGET', 'OUT', '50 high clocks (CH32V003 디버그 리셋)'],
    ['212', 'SWIO_READ_REG',      'IN',  '4바이트 LE 값 응답 (wValue low = reg)'],
    ['213', 'SWIO_WRITE_REG',     'OUT', '4바이트 LE 값 데이터 스테이지 (wValue low = reg)'],
]
t = Table(swio_req_data, colWidths=[15*mm, 50*mm, 20*mm, 80*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 7. USBasp-Pico SWIO vendor 요청 코드 (USBasp 위 확장)', caption_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>Python 도구 사용법 (vendor request 방식)</b>', h3_style))
story.append(Paragraph(
    '<font face="Mono">pip install pyusb<br/>'
    '<br/>'
    '# SWIO 초기화<br/>'
    'python3 scripts/usbasp_pico_swio.py init<br/>'
    '<br/>'
    '# SWIO 타겟 리셋<br/>'
    'python3 scripts/usbasp_pico_swio.py reset<br/>'
    '<br/>'
    '# 디버그 레지스터 0x06 읽기<br/>'
    'python3 scripts/usbasp_pico_swio.py r 06<br/>'
    '<br/>'
    '# 디버그 레지스터 0x7F = 0x12345678 쓰기<br/>'
    'python3 scripts/usbasp_pico_swio.py w 7F 12345678<br/>'
    '<br/>'
    '# CDC 방식 강제 (시리얼 포트 경로 지정)<br/>'
    'python3 scripts/usbasp_pico_swio.py --cdc --port /dev/ttyACM0 r 06</font>',
    code_style))
story.append(PageBreak())

# ─── Chapter 7: Build ──────────────────────────────────────────────────
story.append(add_heading('7. 빌드 방법', h1_style, level=0))
story.append(Paragraph(
    '빌드에는 ARM Cortex-M0+ 크로스 컴파일러 (arm-none-eabi-gcc 10+), '
    'CMake 3.13+, Ninja, Pico SDK 2.x가 필요합니다. 빌드 과정은 표준 Pico SDK '
    '패턴을 따릅니다.', body_style))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>필수 도구</b>', h3_style))
story.append(Paragraph(
    '<font face="Mono">arm-none-eabi-gcc</font> (ARM 공식 다운로드 또는 배포판 패키지)<br/>'
    '<font face="Mono">cmake</font> 3.13 이상<br/>'
    '<font face="Mono">ninja</font> (권장) 또는 make<br/>'
    '<font face="Mono">Pico SDK</font> 2.3.0 이상 (raspberrypi/pico-sdk)<br/>'
    '<font face="Mono">TinyUSB</font> (Pico SDK 서브모듈로 자동 포함)', body_noindent))
story.append(Spacer(1, 3*mm))
story.append(Paragraph('<b>빌드 명령</b>', h3_style))
story.append(Paragraph(
    '<font face="Mono"># 환경 변수 설정<br/>'
    'export PICO_SDK_PATH=/path/to/pico-sdk<br/>'
    'export PATH=$PATH:/path/to/arm-none-eabi-gcc/bin<br/>'
    '<br/>'
    '# Pico SDK 서브모듈 초기화 (최초 1회)<br/>'
    'cd $PICO_SDK_PATH &amp;&amp; git submodule update --init lib/tinyusb<br/>'
    '<br/>'
    '# 빌드 디렉토리 생성 및 구성<br/>'
    'cd /home/z/my-project/usbasp-pico<br/>'
    'cmake -B build -G Ninja<br/>'
    '<br/>'
    '# 컴파일 및 UF2 생성<br/>'
    'cmake --build build<br/>'
    '<br/>'
    '# 결과물<br/>'
    '# build/usbasp-pico.uf2 (플래싱용)<br/>'
    '# build/usbasp-pico.elf  (디버그용)<br/>'
    '# build/usbasp-pico.hex  (일반 헥사)</font>', code_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>Pico 플래싱</b>', h3_style))
story.append(Paragraph(
    '1. Pico의 BOOTSEL 버튼을 누른 채 USB 연결 (대용량 저장장치로 인식)<br/>'
    '2. <font face="Mono">build/usbasp-pico.uf2</font> 파일을 Pico 드라이브에 복사<br/>'
    '3. Pico가 자동 리부팅되며 USBasp로 인식', body_noindent))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '<b>HVSP 비활성화 빌드</b>: CMakeLists.txt에서 '
    '<font face="Mono">USBASP_PICO_ENABLE_HVSP=1</font>을 0으로 변경하면 '
    'HVSP 관련 코드가 스텁 처리되어 플래시 사용량이 줄어듭니다. 12V 회로를 '
    '구성하지 않은 경우 추천합니다.', body_style))
story.append(PageBreak())

# ─── Chapter 8: avrdude usage ─────────────────────────────────────────
story.append(add_heading('8. avrdude 호환성 및 사용법', h1_style, level=0))
story.append(Paragraph(
    'USBasp-Pico는 표준 USBasp로 인식되므로 avrdude의 -c usbasp 옵션으로 '
    '그대로 사용할 수 있습니다. avrdude 5.2 이상에서 USBasp가 지원됩니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>OS별 드라이버 설정</b>', h3_style))
story.append(Paragraph(
    '<b>Linux</b>: udev 룰 추가 (일반 사용자 권한)<br/>'
    '<font face="Mono">sudo nano /etc/udev/rules.d/99-usbasp.rules</font><br/>'
    '<font face="Mono">SUBSYSTEM=="usb", ATTRS{idVendor}=="16c0", '
    'ATTRS{idProduct}=="05dc", MODE="0666", GROUP="plugdev"</font><br/>'
    '<font face="Mono">sudo udevadm control --reload-rules</font>', code_style))
story.append(Paragraph(
    '<b>Windows</b>: Zadig (https://zadig.akeo.ie/) 실행 후 USBasp 장치를 '
    'libusb-win32 또是 WinUSB 드라이버로 설정합니다. libusbK도 호환됩니다.', body_noindent))
story.append(Paragraph(
    '<b>macOS</b>: libusb (Homebrew: <font face="Mono">brew install libusb</font>) '
    '설치 후 별도 드라이버 불필요.', body_noindent))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>avrdude 명령 예제</b>', h3_style))
story.append(Paragraph(
    '<font face="Mono"># ATmega328P 시그니처 읽기<br/>'
    'avrdude -c usbasp -p m328p<br/>'
    '<br/>'
    '# 플래시 쓰기 + 검증<br/>'
    'avrdude -c usbasp -p m328p -U flash:w:firmware.hex<br/>'
    '<br/>'
    '# 퓨즈 읽기<br/>'
    'avrdude -c usbasp -p m328p -U lfuse:r:-:h -U hfuse:r:-:h<br/>'
    '<br/>'
    '# 퓨즈 쓰기 (예: 내부 8MHz 클럭)<br/>'
    'avrdude -c usbasp -p m328p -U lfuse:w:0xE2:m<br/>'
    '<br/>'
    '# SCK 속도 지정 (느린 클럭 AVR)<br/>'
    'avrdude -c usbasp -p m328p -B 8  # 8 kHz SCK<br/>'
    '<br/>'
    '# TPI 모드 (ATtiny10)<br/>'
    'avrdude -c usbasp -p t10 -U flash:w:tiny10.hex</font>', code_style))
story.append(PageBreak())

# ─── Chapter 9: Test/verify ───────────────────────────────────────────
story.append(add_heading('9. 테스트 및 검증', h1_style, level=0))
story.append(Paragraph(
    '펌웨어 정상 동작 확인 절차는 다음과 같습니다. 빌드 후 UF2 플래싱, USB '
    '인식, avrdude 통신, 실제 AVR 프로그래밍 단계로 나뉩니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>검증 체크리스트</b>', h3_style))
check_data = [
    ['단계', '확인 항목', '성공 기준'],
    ['1', 'UF2 파일 크기', '70KB 이상 (실제 펌웨어 ~38KB)'],
    ['2', 'USB 인식', 'lsusb 출력에 16c0:05dc 표시'],
    ['3', 'avrdude 인식', 'avrdude -c usbasp -p m328p 시 "found USBasp"'],
    ['4', '시그니처 읽기', 'm328p: 0x1E 0x95 0x0F 일치'],
    ['5', '플래시 쓰기', '4KB 블록 단위 정상 전송 (avrdude 진행률 표시)'],
    ['6', '플래시 검증', '-U flash:v:firmware.hex 정상 완료'],
    ['7', '퓨즈 읽기/쓰기', 'lfuse/hfuse/efuse 정상 읽기 + 쓰기'],
    ['8', 'EEPROM 접근', 'EEPROM 읽기/쓰기 정상 동작'],
    ['9', '고용량 AVR', 'ATmega2560 256KB 플래시 정상 프로그래밍'],
    ['10', 'TPI 모드', 'ATtiny10 시그니처 0x1E 0x90 0x07 정상'],
]
t = Table(check_data, colWidths=[15*mm, 70*mm, 75*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('ALIGN', (0,0), (0,-1), 'CENTER'),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 5. 펌웨어 검증 체크리스트', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>문제 해결</b>', h3_style))
story.append(Paragraph(
    '<b>avrdude가 USBasp를 찾지 못함</b>: USB 케이블/포트 확인, udev 룰 '
    '적용 여부, 드라이버 설치 상태 확인. <font face="Mono">lsusb</font>로 '
    '16c0:05dc가 보이는지 먼저 점검.', body_noindent))
story.append(Paragraph(
    '<b>시그니처 0x00 0x00 0x00 읽힘</b>: SCK 속도가 너무 빠름. '
    '<font face="Mono">-B 32</font> 또는 <font face="Mono">-B 16</font>으로 '
    '느리게 설정 후 재시도. 타겟 클럭이 1.5MHz 이하인 경우 필수.', body_noindent))
story.append(Paragraph(
    '<b>플래시 검증 실패</b>: 배선 불량(MISO 특히), GND 공통 누락, '
    '타겟 VCC 부족(500mA 이상 필요 시 외부 전원).', body_noindent))
story.append(Paragraph(
    '<b>고용량 AVR 실패</b>: avrdude 버전 5.11+ 필요 (SETLONGADDRESS '
    '지원). 구버전에서는 128KB 이상 주소 접근 불가.', body_noindent))
story.append(PageBreak())

# ─── Chapter 10: Related projects ────────────────────────────────────
story.append(add_heading('10. 관련 프로젝트 및 참고자료', h1_style, level=0))
story.append(Paragraph(
    '본 프로젝트는 다음 오픈소스 프로젝트들을 기반으로 하거나 참고했습니다. '
    '각각의 접근 방식과 본 프로젝트의 차이점을 정리합니다.', body_style))
story.append(Spacer(1, 4*mm))
related_data = [
    ['프로젝트', '구현 방식', '본 프로젝트와 차이'],
    ['USBasp (Thomas Fischl)', 'ATmega8 + V-USB, 12MHz', '원본. 본 프로젝트의 프로토콜 기반'],
    ['mariusgreuel/USBasp', 'USBasp 포크 (윈도우 드라이버/빌드 개선)', '본 프로젝트의 직접 베이스'],
    ['USBtinyISP (Dick Streefland)', 'ATtiny2313 + V-USB', '고용량 AVR 미지원, 본 프로젝트의 대안'],
    ['SpeedyCraftah/avr-pico-programmer', 'Pico + CDC 시리얼 + 자체 프로토콜', 'avrdude 비호환, Node.js 호스트 필요'],
    ['kevincuzner/linuxspi', 'Raspberry Pi (Pi 4 등) + linux SPI 드라이버', 'RP2040 아님, avrdude -c linuxspi'],
    ['tsaarni/avr-hvsp', 'Arduino Nano + HVSP (ATtiny85)', 'USB 비호환, HVSP 전용'],
    ['ericdraken/hvsp', 'ATtiny85 + HVSP charge pump 자체 회로', 'RP2040 아님, HVSP 참고용'],
    ['AVRDUDE (avrdudes/avrdude)', 'USBasp 호스트 드라이버 구현체', '펌웨어와 호스트 매칭 기준'],
]
t = Table(related_data, colWidths=[55*mm, 50*mm, 65*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 6. 관련 프로젝트 비교', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>참고 자료</b>', h3_style))
refs = [
    'USBasp 원본: https://www.fischl.de/usbasp/',
    'mariusgreuel 포크: https://github.com/mariusgreuel/USBasp',
    'AVRDUDE: https://github.com/avrdudes/avrdude',
    'Raspberry Pi Pico SDK: https://github.com/raspberrypi/pico-sdk',
    'TinyUSB: https://github.com/hathach/tinyusb',
    'ATtiny85 datasheet (HVSP): https://www.microchip.com/wwwproducts/ATtiny85',
    'MAX662A 12V charge pump: https://www.analog.com/max662',
    'V-USB (원본 USB 드라이버): https://www.obdev.at/products/v-usb/',
]
for r in refs:
    story.append(Paragraph(f'• {r}', bullet_style))
story.append(PageBreak())

# ─── Chapter 11: License ──────────────────────────────────────────────
story.append(add_heading('11. 라이선스 및 크레딧', h1_style, level=0))
story.append(Paragraph(
    '본 프로젝트의 펌웨어 소스 코드는 USBasp 원본의 GNU GPL v2 라이선스를 '
    '계승합니다. USBasp 프로토콜 사양 및 프로토콜 상수(USBASP_FUNC_*, '
    'USBASP_ISP_SCK_*)는 Thomas Fischl의 원본 USBasp에서 파생되었습니다. '
    'mariusgreuel 포크의 개선사항(빌드 시스템, 드라이버 패키징)을 참고했습니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph('<b>사용된 오픈소스 컴포넌트</b>', h3_style))
lic_data = [
    ['컴포넌트', '라이선스', '저작권'],
    ['USBasp (Thomas Fischl)', 'GPL v2', 'Thomas Fischl (2005~2011)'],
    ['mariusgreuel/USBasp fork', 'GPL v2', 'Marius Greuel'],
    ['V-USB', 'GPL v2 / 상용', 'OBJECTIVE DEVELOPMENT Software GmbH'],
    ['AVRDUDE', 'GPL v2', 'avrdudes 커뮤니티, Dick Streefland'],
    ['TinyUSB', 'MIT', 'Ha Thach (tinyusb.org)'],
    ['Pico SDK', 'BSD-3-Clause', 'Raspberry Pi (Trading) Ltd'],
    ['arm-none-eabi-gcc', 'GPL v3 + GCC runtime exception', 'Free Software Foundation'],
]
t = Table(lic_data, colWidths=[55*mm, 50*mm, 65*mm])
t.setStyle(TableStyle([
    ('BACKGROUND', (0,0), (-1,0), HEADER_FILL),
    ('TEXTCOLOR', (0,0), (-1,0), colors.white),
    ('FONTNAME', (0,0), (-1,0), 'NotoSerifKR-Bold'),
    ('FONTNAME', (0,1), (-1,-1), 'NotoSerifKR'),
    ('FONTSIZE', (0,0), (-1,-1), 9),
    ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
    ('GRID', (0,0), (-1,-1), 0.5, BORDER),
    ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, TABLE_STRIPE]),
    ('LEFTPADDING', (0,0), (-1,-1), 6),
    ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ('TOPPADDING', (0,0), (-1,-1), 3),
    ('BOTTOMPADDING', (0,0), (-1,-1), 3),
]))
story.append(t)
story.append(Paragraph('표 7. 사용된 오픈소스 컴포넌트 라이선스', caption_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '<b>배포 조건</b>: 본 펌웨어 바이너리(usbasp-pico.uf2)는 GPL v2 조건에 따라 '
    '배포됩니다. 소스 코드 요청 시 본 저장소(또는 미러)를 통해 제공되며, '
    '수정된 버전을 배포할 경우 동일한 GPL v2 라이선스를 유지해야 합니다.', body_style))
story.append(Spacer(1, 4*mm))
story.append(Paragraph(
    '<b>면책 조항</b>: 본 프로젝트는 어떠한 보증도 제공하지 않습니다. '
    '12V HVSP 모드 사용 시 타겟 AVR 손상 위험이 있으므로, 데이터시트를 '
    '충분히 숙지한 후 사용하시기 바랍니다. 특히 RESET 핀에 12V 이상의 '
    '전압이나 부정극성 전압이 인가되지 않도록 주의해야 합니다.', body_style))

# ── Build PDF ─────────────────────────────────────────────────────────
doc = TocDocTemplate(
    PDF_OUT, pagesize=A4,
    leftMargin=20*mm, rightMargin=20*mm,
    topMargin=20*mm, bottomMargin=20*mm,
    title='USBasp-Pico 문서',
    author='Z.ai',
    subject='라즈베리파이 피코 (RP2040) 용 USBasp 호환 AVR 프로그래머',
    creator='Z.ai (ReportLab)',
)

# Cover page template (no header/footer) + body template (with header/footer)
cover_frame = Frame(0, 0, A4[0], A4[1], leftPadding=0, rightPadding=0,
                    topPadding=0, bottomPadding=0, id='cover')
body_frame = Frame(20*mm, 18*mm, A4[0]-40*mm, A4[1]-35*mm, id='body')
doc.addPageTemplates([
    PageTemplate(id='cover', frames=[cover_frame], onPage=first_page),
    PageTemplate(id='body', frames=[body_frame], onPage=later_page),
])

# Switch to body template after cover
from reportlab.platypus.doctemplate import NextPageTemplate
# Insert NextPageTemplate after cover content
cover_pos = 1  # cover ends with PageBreak
new_story = []
for i, f in enumerate(story):
    new_story.append(f)
    if i == cover_pos and hasattr(f, '__class__') and f.__class__.__name__ == 'PageBreak':
        new_story.append(NextPageTemplate('body'))

doc.multiBuild(new_story)
print(f"Generated PDF: {PDF_OUT}")
print(f"Size: {os.path.getsize(PDF_OUT)} bytes")
