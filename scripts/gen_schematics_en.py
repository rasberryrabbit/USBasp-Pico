#!/usr/bin/env python3
"""
Generate English-labeled wiring/schematic diagrams for USBasp-Pico
(targeting the Waveshare RP2040-Zero pin layout).

Outputs:
  wiring_basic.png      - 5V AVR target + level shifter (ISP only)
  wiring_3v3.png        - 3.3V AVR target direct (ISP only)
  wiring_hvsp.png       - 12V HVSP mode (ATtiny85), RP2040-Zero pinout
  pinout_pico.png       - RP2040-Zero pinout for USBasp-Pico (ISP + HVSP)
  block_diagram.png     - firmware architecture
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle

OUT = "/home/z/my-project/usbasp-pico/docs/images"
os.makedirs(OUT, exist_ok=True)

# Colors
C_PICO_BG = "#F8FAFC"
C_PICO_BORDER = "#243447"
C_AVRR_BG = "#FEF3C7"
C_AVRR_BORDER = "#92400E"
C_CONN_BG = "#EFF6FF"
C_CONN_BORDER = "#1E40AF"
C_LABEL_BG = "#F1F5F9"
C_BOOST_BG = "#FEE2E2"
C_BOOST_BORDER = "#991B1B"
C_TEXT = "#243447"
C_WIRE = "#4B5563"
C_WIRE_HI = "#DC2626"
C_WIRE_LO = "#1E40AF"
C_WIRE_GND = "#111827"
C_WIRE_SIG = "#059669"
C_WARN = "#9A3412"   # for GP16 warning highlight


def rounded_box(ax, x, y, w, h, label, fc, ec, fontsize=10, lw=2, sub=None):
    box = FancyBboxPatch((x, y), w, h,
                         boxstyle="round,pad=0.02,rounding_size=0.04",
                         linewidth=lw, edgecolor=ec, facecolor=fc, zorder=2)
    ax.add_patch(box)
    ax.text(x + w/2, y + h/2 + (0.04 if sub else 0), label,
            ha='center', va='center', fontsize=fontsize,
            color=C_TEXT, weight='bold', zorder=3)
    if sub:
        ax.text(x + w/2, y + h/2 - 0.05, sub,
                ha='center', va='center', fontsize=fontsize - 2,
                color=C_TEXT, zorder=3)


def wire(ax, x1, y1, x2, y2, color=C_WIRE, lw=1.6):
    ax.plot([x1, x2], [y1, y2], color=color, linewidth=lw,
            linestyle='-', solid_capstyle='round', zorder=1)


def junction(ax, x, y, color=C_WIRE):
    ax.add_patch(Circle((x, y), 0.012, color=color, zorder=5))


def add_title(ax, text, subtitle=None):
    ax.text(0.5, 0.97, text, ha='center', va='top',
            fontsize=14, weight='bold', color=C_TEXT,
            transform=ax.transAxes)
    if subtitle:
        ax.text(0.5, 0.92, subtitle, ha='center', va='top',
                fontsize=10, color="#475569",
                transform=ax.transAxes)


def new_ax(w=10, h=7):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis('off')
    fig.patch.set_facecolor('white')
    return fig, ax


def make_pinout():
    """RP2040-Zero pinout - ISP (default) + HVSP + SWIO."""
    fig, ax = new_ax(7.5, 9.5)
    add_title(ax, "USBasp-Pico Pin Assignment",
              "Waveshare RP2040-Zero (ISP + HVSP + SWIO)")

    box_x, box_y, box_w, box_h = 0.30, 0.04, 0.40, 0.84
    rounded_box(ax, box_x, box_y, box_w, box_h, "", C_PICO_BG, C_PICO_BORDER,
                fontsize=10, lw=2.5)
    ax.text(0.5, 0.85, "Waveshare RP2040-Zero", ha='center', fontsize=11,
            weight='bold', color=C_TEXT)
    ax.text(0.5, 0.82, "RP2040 @ 133 MHz, 2 MB Flash", ha='center', fontsize=9,
            color="#475569")
    ax.text(0.5, 0.79, "USB-C / BOOT / RESET", ha='center', fontsize=8,
            color="#475569")
    ax.add_patch(Rectangle((box_x + box_w/2 - 0.05, box_y + box_h + 0.005),
                            0.10, 0.025, color="#1F2937"))
    ax.text(0.5, box_y + box_h + 0.045, "USB-C", ha='center', fontsize=9,
            color=C_TEXT)

    # ----- ISP pins (left side, top) -----
    isp_pins = [
        ("GP2",  "ISP SCK",              C_WIRE_SIG),
        ("GP3",  "ISP MOSI / HVSP SDI",  C_WIRE_SIG),
        ("GP4",  "ISP MISO",             C_WIRE_SIG),
        ("GP5",  "ISP RESET",            C_WIRE_SIG),
        ("GP6",  "STATUS LED",           C_WARN),
        ("GND",  "Ground",               C_WIRE_GND),
        ("3V3",  "Target 3V3",           C_WIRE_LO),
        ("VBUS", "5V (USB)",             C_WIRE_HI),
    ]
    # ----- HVSP + SWIO pins (right side) -----
    # v1.9: MOSI/SDI shared on GP3 (shown on left); HVSP SDO/SII/12V_EN
    # shifted down by one pin (GP9->GP8, GP10->GP9, GP11->GP10). GP11 is free.
    right_pins = [
        ("GP7",  "HVSP SCI",     C_WIRE_SIG),
        ("GP8",  "HVSP SDO",     C_WIRE_SIG),
        ("GP9",  "HVSP SII",     C_WIRE_SIG),
        ("GP10", "12V EN",       C_WIRE_HI),
        ("GP11", "(free)",       C_WARN),
        ("GP12", "SWIO (CH32V003)", C_WIRE_SIG),
        ("GP16", "WS2812 LED (avoid)", C_WARN),
        ("GP22-25", "solder pads only", C_WARN),
    ]

    y0 = 0.76
    dy = 0.043
    for i, (pin, fn, c) in enumerate(isp_pins):
        y = y0 - i * dy
        ax.plot([box_x, box_x - 0.05], [y, y], color="#1F2937", lw=2)
        ax.add_patch(Circle((box_x, y), 0.005, color="#1F2937"))
        ax.text(box_x - 0.06, y, pin, ha='right', va='center',
                fontsize=8, family='monospace')
        ax.text(box_x + 0.01, y, fn, ha='left', va='center',
                fontsize=8, color=c, family='monospace')
    for i, (pin, fn, c) in enumerate(right_pins):
        y = y0 - i * dy
        ax.plot([box_x + box_w, box_x + box_w + 0.05], [y, y],
                color="#1F2937", lw=2)
        ax.add_patch(Circle((box_x + box_w, y), 0.005, color="#1F2937"))
        ax.text(box_x + box_w + 0.06, y, pin, ha='left', va='center',
                fontsize=8, family='monospace')
        ax.text(box_x + box_w - 0.01, y, fn, ha='right', va='center',
                fontsize=8, color=c, family='monospace')

    # Legend
    legend_items = [
        ("Signal (3.3V I/O)", C_WIRE_SIG),
        ("Power 5V", C_WIRE_HI),
        ("Power 3.3V", C_WIRE_LO),
        ("GND", C_WIRE_GND),
        ("Warning / avoid", C_WARN),
    ]
    for i, (lbl, c) in enumerate(legend_items):
        ly = 0.005 + i * 0.022
        ax.add_patch(Rectangle((0.04, ly), 0.018, 0.012, color=c))
        ax.text(0.07, ly + 0.006, lbl, fontsize=8, va='center', color=C_TEXT)

    plt.savefig(os.path.join(OUT, "pinout_pico.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


def make_wiring_basic():
    fig, ax = new_ax(11, 7)
    add_title(ax, "Wiring: 5V AVR target + level shifter (ISP)",
              "Use a bidirectional level shifter such as TXS0108E or 74LVC245")

    rounded_box(ax, 0.04, 0.30, 0.18, 0.45, "RP2040-Zero", C_PICO_BG,
                C_PICO_BORDER, fontsize=10, sub="(3.3V)")
    rounded_box(ax, 0.38, 0.32, 0.20, 0.41, "Level Shifter",
                C_CONN_BG, C_CONN_BORDER, fontsize=10, sub="TXS0108E")
    rounded_box(ax, 0.74, 0.30, 0.20, 0.45, "AVR ISP 6-pin",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=10, sub="5V target")

    y_sck, y_mosi, y_miso, y_reset, y_gnd, y_vcc = 0.69, 0.64, 0.59, 0.54, 0.49, 0.44

    pico_labels = [
        ("GP2",  y_sck,   "SCK",   C_WIRE_SIG),
        ("GP3",  y_mosi,  "MOSI",  C_WIRE_SIG),
        ("GP4",  y_miso,  "MISO",  C_WIRE_SIG),
        ("GP5",  y_reset, "RESET", C_WIRE_SIG),
        ("GND",  y_gnd,   "GND",   C_WIRE_GND),
        ("3V3",  y_vcc,   "LVcc",  C_WIRE_LO),
        ("VBUS", 0.39,    "5V",    C_WIRE_HI),
        ("GP6",  0.34,    "LED",   C_WARN),
    ]
    for pin, y, lbl, c in pico_labels:
        ax.text(0.045, y, pin, ha='left', va='center', fontsize=8,
                family='monospace', color=C_TEXT)
        ax.text(0.215, y, lbl, ha='right', va='center', fontsize=8,
                family='monospace', color=c)

    lv_left = [
        ("SCK",   y_sck,   C_WIRE_SIG),
        ("MOSI",  y_mosi,  C_WIRE_SIG),
        ("MISO",  y_miso,  C_WIRE_SIG),
        ("RESET", y_reset, C_WIRE_SIG),
        ("GND",   y_gnd,   C_WIRE_GND),
        ("LVcc",  y_vcc,   C_WIRE_LO),
    ]
    lv_right = [
        ("SCK",   y_sck,   C_WIRE_SIG),
        ("MOSI",  y_mosi,  C_WIRE_SIG),
        ("MISO",  y_miso,  C_WIRE_SIG),
        ("RESET", y_reset, C_WIRE_SIG),
        ("GND",   y_gnd,   C_WIRE_GND),
        ("HVcc",  y_vcc,   C_WIRE_HI),
    ]
    for lbl, y, c in lv_left:
        ax.text(0.375, y, lbl, ha='right', va='center', fontsize=8,
                family='monospace', color=c)
    for lbl, y, c in lv_right:
        ax.text(0.585, y, lbl, ha='left', va='center', fontsize=8,
                family='monospace', color=c)

    avr_labels = [
        ("1:MISO",  y_miso,  C_WIRE_SIG),
        ("2:VTG",   y_vcc,   C_WIRE_HI),
        ("3:SCK",   y_sck,   C_WIRE_SIG),
        ("4:MOSI",  y_mosi,  C_WIRE_SIG),
        ("5:RESET", y_reset, C_WIRE_SIG),
        ("6:GND",   y_gnd,   C_WIRE_GND),
    ]
    for pin, y, c in avr_labels:
        ax.text(0.745, y, pin.split(':')[0], ha='right', va='center', fontsize=8,
                family='monospace', color="#475569")
        ax.text(0.95, y, pin.split(':')[1], ha='right', va='center', fontsize=8,
                family='monospace', color=c, weight='bold')

    for lbl, y, c in lv_left:
        wire(ax, 0.22, y, 0.38, y, color=c)

    # Pico VBUS -> HVcc
    wire(ax, 0.22, 0.39, 0.34, 0.39, color=C_WIRE_HI)
    wire(ax, 0.34, 0.39, 0.34, y_vcc, color=C_WIRE_HI)
    wire(ax, 0.34, y_vcc, 0.38, y_vcc, color=C_WIRE_HI)

    for lbl, y, c in lv_right:
        if lbl == "HVcc":
            wire(ax, 0.58, y_vcc, 0.74, y_vcc, color=C_WIRE_HI)
        else:
            wire(ax, 0.58, y, 0.74, y, color=c)

    notes = [
        "* Level shifter HVcc must match target voltage (5V)",
        "* Target power supplied directly from RP2040-Zero VBUS (5V)",
        "* MISO direction: 5V -> 3.3V; MOSI/SCK/RESET: 3.3V -> 5V",
        "* For 3.3V AVR, omit the level shifter (see wiring_3v3)",
    ]
    for i, t in enumerate(notes):
        ax.text(0.04, 0.26 - i * 0.022, t, fontsize=8, color=C_TEXT)

    plt.savefig(os.path.join(OUT, "wiring_basic.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


def make_wiring_3v3():
    fig, ax = new_ax(10, 6)
    add_title(ax, "Wiring: 3.3V AVR target (no level shifter needed)",
              "For ATmega328P/ATtiny85 etc. running at 3.3V")

    rounded_box(ax, 0.04, 0.30, 0.30, 0.45, "RP2040-Zero (3.3V)",
                C_PICO_BG, C_PICO_BORDER, fontsize=10)
    rounded_box(ax, 0.66, 0.30, 0.30, 0.45, "AVR Target (3.3V)",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=10, sub="6-pin ISP")

    pico_pins = [
        ("GP2",  0.69, "SCK",    C_WIRE_SIG),
        ("GP3",  0.64, "MOSI",   C_WIRE_SIG),
        ("GP4",  0.59, "MISO",   C_WIRE_SIG),
        ("GP5",  0.54, "RESET",  C_WIRE_SIG),
        ("GND",  0.49, "GND",    C_WIRE_GND),
        ("3V3",  0.44, "VCC",    C_WIRE_LO),
        ("GP6",  0.34, "LED",    C_WARN),
    ]
    avr_pins = [
        ("1:MISO",  0.69, C_WIRE_SIG),
        ("2:VTG",   0.64, C_WIRE_LO),
        ("3:SCK",   0.59, C_WIRE_SIG),
        ("4:MOSI",  0.54, C_WIRE_SIG),
        ("5:RESET", 0.49, C_WIRE_SIG),
        ("6:GND",   0.44, C_WIRE_GND),
    ]
    for pin, y, lbl, c in pico_pins:
        ax.text(0.05, y, pin, ha='left', va='center', fontsize=8,
                family='monospace', color=C_TEXT)
        ax.text(0.32, y, lbl, ha='right', va='center', fontsize=8,
                family='monospace', color=c)
    for pin, y, c in avr_pins:
        ax.text(0.67, y, pin.split(':')[0], ha='left', va='center', fontsize=8,
                family='monospace', color="#475569")
        ax.text(0.94, y, pin.split(':')[1], ha='right', va='center',
                fontsize=8, family='monospace', color=c, weight='bold')

    wire_map = [
        (0.69, 0.59, C_WIRE_SIG),
        (0.64, 0.54, C_WIRE_SIG),
        (0.59, 0.69, C_WIRE_SIG),
        (0.54, 0.49, C_WIRE_SIG),
        (0.49, 0.44, C_WIRE_GND),
        (0.44, 0.64, C_WIRE_LO),
    ]
    for src_y, dst_y, color in wire_map:
        wire(ax, 0.34, src_y, 0.66, dst_y, color=color)

    notes = [
        "* 3.3V AVR: RP2040-Zero 3V3 pin supplies target VCC directly",
        "* SCK/MOSI/RESET: Pico output (3.3V) -> target input (3.3V) direct",
        "* MISO: target output (3.3V) -> Pico input (3.3V tolerant) direct",
        "* Common GND is mandatory",
    ]
    for i, t in enumerate(notes):
        ax.text(0.04, 0.26 - i * 0.022, t, fontsize=8, color=C_TEXT)

    plt.savefig(os.path.join(OUT, "wiring_3v3.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


def make_wiring_hvsp():
    """HVSP wiring for RP2040-Zero pinout (v1.9: MOSI/SDI shared on GP3)."""
    fig, ax = new_ax(12, 8)
    add_title(ax, "Wiring: HVSP mode (12V) - ATtiny85 recovery",
              "RP2040-Zero pinout (v1.9). For unbricking AVRs with disabled RESET or wrong clock")

    rounded_box(ax, 0.04, 0.30, 0.18, 0.55, "RP2040-Zero", C_PICO_BG,
                C_PICO_BORDER, fontsize=10)
    rounded_box(ax, 0.40, 0.40, 0.20, 0.35, "12V Boost",
                C_BOOST_BG, C_BOOST_BORDER, fontsize=10,
                sub="MAX662A")
    rounded_box(ax, 0.74, 0.20, 0.20, 0.65, "ATtiny85",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=10, sub="8-pin DIP")

    # v1.9 pin mapping: GP3 (SDI, shared with ISP MOSI), GP7 (SCI),
    # GP8 (SDO), GP9 (SII), GP10 (12V_EN). GP3 is on the ISP side but
    # listed here too because it carries the HVSP SDI signal.
    pico_pins = [
        ("GP3",  0.79, "SDI*",   C_WIRE_SIG),
        ("GP7",  0.74, "SCI",    C_WIRE_SIG),
        ("GP8",  0.69, "SDO",    C_WIRE_SIG),
        ("GP9",  0.64, "SII",    C_WIRE_SIG),
        ("GP10", 0.59, "12V EN", C_WIRE_HI),
        ("VBUS", 0.54, "5V",     C_WIRE_HI),
        ("GND",  0.49, "GND",    C_WIRE_GND),
    ]
    for pin, y, lbl, c in pico_pins:
        ax.text(0.045, y, pin, ha='left', va='center', fontsize=8,
                family='monospace', color=C_TEXT)
        ax.text(0.215, y, lbl, ha='right', va='center', fontsize=8,
                family='monospace', color=c)

    ax.text(0.405, 0.65, "VIN", ha='right', va='center', fontsize=8,
            family='monospace', color=C_WIRE_HI)
    ax.text(0.405, 0.59, "EN",  ha='right', va='center', fontsize=8,
            family='monospace', color=C_WIRE_HI)
    ax.text(0.405, 0.49, "GND", ha='right', va='center', fontsize=8,
            family='monospace', color=C_WIRE_GND)
    ax.text(0.595, 0.65, "VOUT", ha='left', va='center', fontsize=8,
            family='monospace', color=C_WIRE_HI)
    ax.text(0.595, 0.49, "GND", ha='left', va='center', fontsize=8,
            family='monospace', color=C_WIRE_GND)

    # ATtiny85 HVSP pins per datasheet Table 26-1:
    #   PB0 = SCI (clock), PB1 = SDI (data in), PB2 = SDO (data out),
    #   PB3 = SII (instruction)
    avr_pins = [
        ("1:RESET",   0.79, C_WIRE_HI),
        ("2:PB3/SII",  0.74, C_WIRE_SIG),
        ("3:PB4",      0.69, C_WIRE_GND),
        ("4:GND",      0.64, C_WIRE_GND),
        ("5:PB0/SCI",  0.59, C_WIRE_SIG),
        ("6:PB1/SDI",  0.54, C_WIRE_SIG),
        ("7:PB2/SDO",  0.49, C_WIRE_SIG),
        ("8:VCC",      0.44, C_WIRE_HI),
    ]
    for pin, y, c in avr_pins:
        ax.text(0.745, y, pin, ha='right', va='center', fontsize=8,
                family='monospace', color=c)

    # Pico VBUS -> Boost VIN
    wire(ax, 0.22, 0.54, 0.34, 0.54, color=C_WIRE_HI)
    wire(ax, 0.34, 0.54, 0.34, 0.65, color=C_WIRE_HI)
    wire(ax, 0.34, 0.65, 0.40, 0.65, color=C_WIRE_HI)
    # Pico 12V EN (GP10) -> Boost EN
    wire(ax, 0.22, 0.59, 0.36, 0.59, color=C_WIRE_HI)
    wire(ax, 0.36, 0.59, 0.40, 0.59, color=C_WIRE_HI)
    # Pico GND -> Boost GND
    wire(ax, 0.22, 0.49, 0.38, 0.49, color=C_WIRE_GND)
    wire(ax, 0.38, 0.49, 0.40, 0.49, color=C_WIRE_GND)

    # Boost VOUT (12V) -> ATtiny85 RESET (pin 1)
    wire(ax, 0.60, 0.65, 0.68, 0.65, color=C_WIRE_HI)
    wire(ax, 0.68, 0.65, 0.68, 0.79, color=C_WIRE_HI)
    wire(ax, 0.68, 0.79, 0.74, 0.79, color=C_WIRE_HI)

    # Boost GND -> ATtiny85 GND (pin 4)
    wire(ax, 0.60, 0.49, 0.70, 0.49, color=C_WIRE_GND)
    wire(ax, 0.70, 0.49, 0.70, 0.64, color=C_WIRE_GND)
    wire(ax, 0.70, 0.64, 0.74, 0.64, color=C_WIRE_GND)

    # Pico HVSP signals -> ATtiny85
    # v1.9 mapping:
    #   GP7  SCI -> pin 5 PB0/SCI
    #   GP9  SII -> pin 2 PB3/SII
    #   GP8  SDO -> pin 7 PB2/SDO
    #   GP3  SDI -> pin 6 PB1/SDI
    pairs = [
        (0.74, 0.59, C_WIRE_SIG),  # GP7  SCI -> pin 5 PB0/SCI
        (0.64, 0.74, C_WIRE_SIG),  # GP9  SII -> pin 2 PB3/SII
        (0.69, 0.49, C_WIRE_SIG),  # GP8  SDO -> pin 7 PB2/SDO
        (0.79, 0.54, C_WIRE_SIG),  # GP3  SDI -> pin 6 PB1/SDI
    ]
    for src_y, dst_y, color in pairs:
        if abs(src_y - dst_y) > 0.1:
            wire(ax, 0.22, src_y, 0.30, src_y, color=color)
            wire(ax, 0.30, src_y, 0.30, dst_y, color=color)
            wire(ax, 0.30, dst_y, 0.74, dst_y, color=color)
        else:
            wire(ax, 0.22, src_y, 0.74, dst_y, color=color)

    # Pico VBUS -> ATtiny85 VCC (pin 8) - 5V
    junction(ax, 0.34, 0.54, C_WIRE_HI)
    wire(ax, 0.28, 0.54, 0.28, 0.30, color=C_WIRE_HI)
    wire(ax, 0.28, 0.30, 0.72, 0.30, color=C_WIRE_HI)
    wire(ax, 0.72, 0.30, 0.72, 0.44, color=C_WIRE_HI)
    wire(ax, 0.72, 0.44, 0.74, 0.44, color=C_WIRE_HI)

    notes = [
        "* v1.9: GP3 = ISP MOSI / HVSP SDI (shared). ISP and HVSP never run at the same time, no conflict.",
        "* HVSP entry: VCC on -> SDI/SII/SDO low -> apply 12V to RESET -> switch SDO to input -> wait 300us -> send serial",
        "* 12V boost: MAX662A recommended (12V 30mA), or MC34063A + inductor + diode",
        "* CAUTION: 12V applied to RESET pin only; VCC stays at 5V",
        "* avrdude -c usbasp does NOT support HVSP; use usbasp_pico_hvsp.py host tool",
        "* Pinout: GP3=SDI(shared), GP7=SCI, GP8=SDO, GP9=SII, GP10=12V_EN  (GP11 free)",
    ]
    for i, t in enumerate(notes):
        ax.text(0.04, 0.16 - i * 0.022, t, fontsize=8, color=C_TEXT)

    plt.savefig(os.path.join(OUT, "wiring_hvsp.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


def make_block_diagram():
    fig, ax = new_ax(12, 8)
    add_title(ax, "USBasp-Pico Firmware Architecture",
              "TinyUSB Vendor (USBasp) + CDC (SWIO) composite device")

    rounded_box(ax, 0.02, 0.78, 0.20, 0.15, "Host PC\n(avrdude / swio / hvsp tool)",
                "#FFFFFF", "#1F2937", fontsize=9)
    rounded_box(ax, 0.30, 0.78, 0.18, 0.15, "USB 2.0\nFull-Speed",
                C_CONN_BG, C_CONN_BORDER, fontsize=9)
    rounded_box(ax, 0.55, 0.42, 0.42, 0.48, "RP2040-Zero",
                C_PICO_BG, C_PICO_BORDER, fontsize=10, lw=2.5)

    inner_x = 0.57
    # TinyUSB (Vendor + CDC)
    rounded_box(ax, inner_x, 0.78, 0.18, 0.10, "TinyUSB\nVendor + CDC",
                C_LABEL_BG, "#475569", fontsize=8)
    rounded_box(ax, inner_x + 0.21, 0.78, 0.18, 0.10, "USBasp Protocol\n+ CDC CLI",
                C_LABEL_BG, "#475569", fontsize=8)
    # Backends: ISP, TPI, HVSP, SWIO
    rounded_box(ax, inner_x, 0.62, 0.10, 0.13, "ISP",
                "#EFF6FF", "#1E40AF", fontsize=8)
    rounded_box(ax, inner_x + 0.11, 0.62, 0.10, 0.13, "TPI",
                "#F0FDF4", "#059669", fontsize=8)
    rounded_box(ax, inner_x + 0.22, 0.62, 0.10, 0.13, "HVSP\n(12V)",
                "#FFF7ED", "#9A3412", fontsize=8)
    rounded_box(ax, inner_x + 0.33, 0.62, 0.10, 0.13, "SWIO\n(CH32V003)",
                "#F0FDF4", "#059669", fontsize=7)
    rounded_box(ax, inner_x + 0.44, 0.62, 0.10, 0.13, "GPIO\nDriver",
                "#F8FAFC", "#243447", fontsize=8)
    # Hardware
    rounded_box(ax, inner_x, 0.50, 0.20, 0.09, "RP2040 USB",
                "#FFFFFF", "#1F2937", fontsize=8)
    rounded_box(ax, inner_x + 0.23, 0.50, 0.20, 0.09, "GPIO Pins",
                "#FFFFFF", "#1F2937", fontsize=8)
    rounded_box(ax, inner_x + 0.46, 0.50, 0.07, 0.09, "12V\nMOSFET",
                "#FEE2E2", "#991B1B", fontsize=7)

    # Targets
    rounded_box(ax, 0.05, 0.40, 0.20, 0.15, "AVR\n(ISP 6-pin)",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=9)
    rounded_box(ax, 0.05, 0.20, 0.20, 0.15, "ATtiny85\n(HVSP)",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=9)
    rounded_box(ax, 0.05, 0.00, 0.20, 0.15, "CH32V003\n(SWIO)",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=9)

    # Wires (USB host -> pico)
    wire(ax, 0.22, 0.86, 0.30, 0.86, color=C_WIRE, lw=2)
    wire(ax, 0.48, 0.86, 0.55, 0.86, color=C_WIRE, lw=2)
    # USB -> Protocol + CDC CLI
    wire(ax, inner_x + 0.18, 0.78, inner_x, 0.78, color=C_WIRE_LO, lw=1)
    wire(ax, inner_x, 0.78, inner_x, 0.75, color=C_WIRE_SIG, lw=1)
    wire(ax, inner_x + 0.21, 0.78, inner_x + 0.21, 0.75, color=C_WIRE_SIG, lw=1)
    # Backends -> GPIO driver
    wire(ax, inner_x, 0.62, inner_x, 0.59, color=C_WIRE_SIG, lw=1)
    wire(ax, inner_x + 0.11, 0.62, inner_x + 0.11, 0.59, color=C_WIRE_SIG, lw=1)
    wire(ax, inner_x + 0.22, 0.62, inner_x + 0.22, 0.59, color=C_WIRE_HI, lw=1)
    wire(ax, inner_x + 0.33, 0.62, inner_x + 0.33, 0.59, color=C_WIRE_SIG, lw=1)
    wire(ax, inner_x + 0.44, 0.62, inner_x + 0.44, 0.59, color=C_WIRE_SIG, lw=1)
    # GPIO -> pins
    wire(ax, inner_x + 0.10, 0.50, inner_x + 0.10, 0.40, color=C_WIRE_SIG, lw=1)
    wire(ax, inner_x + 0.33, 0.50, inner_x + 0.33, 0.40, color=C_WIRE_SIG, lw=1)
    # Pico -> targets
    wire(ax, inner_x, 0.40, 0.25, 0.40, color=C_WIRE_SIG, lw=2)
    wire(ax, inner_x + 0.20, 0.50, 0.25, 0.25, color=C_WIRE_HI, lw=2)
    wire(ax, inner_x + 0.33, 0.50, 0.25, 0.10, color=C_WIRE_SIG, lw=2)
    wire(ax, 0.25, 0.40, 0.25, 0.55, color=C_WIRE_SIG, lw=2)
    wire(ax, 0.25, 0.25, 0.25, 0.18, color=C_WIRE_HI, lw=2)
    wire(ax, 0.25, 0.10, 0.25, 0.05, color=C_WIRE_SIG, lw=2)

    notes = [
        "* avrdude -c usbasp: vendor-class EP0 control transfers (ISP/TPI/avrdude)",
        "* SWIO host tool: CDC ACM virtual serial port (ASCII command line)",
        "* HVSP host tool: vendor-class EP0 (request codes 200-207)",
        "* Composite USB device: VID=0x16C0, PID=0x05DC (USBasp-compatible)",
    ]
    for i, t in enumerate(notes):
        ax.text(0.04, 0.42 - i * 0.022, t, fontsize=8, color=C_TEXT)

    plt.savefig(os.path.join(OUT, "block_diagram.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


def make_wiring_swio():
    """SWIO wiring for CH32V003 (single wire)."""
    fig, ax = new_ax(11, 6)
    add_title(ax, "Wiring: SWIO mode for WCH CH32V003",
              "Single-wire protocol via CDC ACM virtual serial port")

    rounded_box(ax, 0.04, 0.25, 0.30, 0.55, "RP2040-Zero",
                C_PICO_BG, C_PICO_BORDER, fontsize=11,
                sub="GP12 = SWIO")
    rounded_box(ax, 0.66, 0.25, 0.30, 0.55, "CH32V003",
                C_AVRR_BG, C_AVRR_BORDER, fontsize=11, sub="RISC-V MCU")

    pico_pins = [
        ("GP12", 0.69, "SWIO", C_WIRE_SIG),
        ("GND",  0.64, "GND",  C_WIRE_GND),
        ("3V3",  0.59, "VCC",  C_WIRE_LO),
        ("VBUS", 0.54, "5V",   C_WIRE_HI),
        ("GP6",  0.44, "LED",  C_WARN),
    ]
    ch32_pins = [
        ("SWIO",   0.69, C_WIRE_SIG),
        ("GND",    0.64, C_WIRE_GND),
        ("VCC",    0.59, C_WIRE_LO),
        ("VCC",    0.54, C_WIRE_HI),
    ]
    for pin, y, lbl, c in pico_pins:
        ax.text(0.05, y, pin, ha='left', va='center', fontsize=8,
                family='monospace', color=C_TEXT)
        ax.text(0.32, y, lbl, ha='right', va='center', fontsize=8,
                family='monospace', color=c)
    for pin, y, c in ch32_pins:
        ax.text(0.67, y, pin, ha='left', va='center', fontsize=8,
                family='monospace', color=c)
        ax.text(0.94, y, pin, ha='right', va='center',
                fontsize=8, family='monospace', color=c, weight='bold')

    # SWIO line (with pull-up note)
    wire(ax, 0.34, 0.69, 0.50, 0.69, color=C_WIRE_SIG)
    wire(ax, 0.50, 0.69, 0.66, 0.69, color=C_WIRE_SIG)
    # Pull-up resistor symbol (small box on the line)
    pu_x, pu_y = 0.50, 0.72
    ax.add_patch(Rectangle((pu_x - 0.018, pu_y - 0.018), 0.036, 0.022,
                            facecolor="#FFFFFF", edgecolor=C_TEXT, lw=1))
    ax.text(pu_x, pu_y - 0.035, "4.7k-10k", ha='center', fontsize=7, color=C_TEXT)
    wire(ax, pu_x, pu_y - 0.018, pu_x, 0.69, color=C_WIRE_SIG)

    # GND direct
    wire(ax, 0.34, 0.64, 0.66, 0.64, color=C_WIRE_GND)
    # VCC: 3.3V (CH32V003 default)
    wire(ax, 0.34, 0.59, 0.66, 0.59, color=C_WIRE_LO)
    # VCC alternative: 5V
    wire(ax, 0.34, 0.54, 0.50, 0.54, color=C_WIRE_HI)
    wire(ax, 0.50, 0.54, 0.50, 0.59, color=C_WIRE_HI)
    junction(ax, 0.50, 0.59, C_WIRE_HI)

    notes = [
        "* SWIO is single-wire open-drain; external 4.7k-10k pull-up required",
        "* CH32V003 runs at 3.3V; supply from RP2040-Zero 3V3 pin",
        "* Connect GND common; LED optional",
        "* Host tool: python3 usbasp_pico_swio.py (CDC virtual serial port)",
        "* SWIO frame: 1 start + 7 reg + 1 R/W + 32 data bits (LSB first)",
    ]
    for i, t in enumerate(notes):
        ax.text(0.04, 0.21 - i * 0.022, t, fontsize=8, color=C_TEXT)

    plt.savefig(os.path.join(OUT, "wiring_swio.png"), dpi=200,
                bbox_inches='tight', facecolor='white')
    plt.close(fig)


if __name__ == "__main__":
    make_pinout()
    make_wiring_basic()
    make_wiring_3v3()
    make_wiring_hvsp()
    make_wiring_swio()
    make_block_diagram()
    print("Generated:")
    for f in sorted(os.listdir(OUT)):
        path = os.path.join(OUT, f)
        size = os.path.getsize(path)
        print(f"  {f} ({size} bytes)")
