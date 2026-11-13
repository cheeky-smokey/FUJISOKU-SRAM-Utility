#!/usr/bin/env python3
"""FUJISOKU SRAM Utility の回路図 (KiCad 7) を作る。

docs/hardware.md の結線表どおりに、ピンごとに短い線と名前 (ネットラベル) を付ける方式で描く。
同じ名前のラベルどうしがつながる。部品の形は KiCad 標準ライブラリから回路図の中にコピーする
(23LC512 だけは標準ライブラリに無いので、このスクリプトで作る)。

使い方: python3 gen_schematic.py [--shield] [KiCad のシンボルライブラリのフォルダ]
        (既定は /usr/share/kicad/symbols。KiCad 7 以降)
  --shield  Arduino UNO R4 シールド基板用の回路図を shield/ に作る。
            カードは 40 ピンのボックスヘッダ (J1) からリボンケーブルでスロット基板へ。
            62256 は載せない。部品は DIP (ソケット) のフットプリントを割り当てる。
"""
import os, re, sys, uuid, json

SHIELD = '--shield' in sys.argv[1:]
_args = [a for a in sys.argv[1:] if not a.startswith('--')]
LIBDIR = _args[0] if _args else '/usr/share/kicad/symbols'
HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, 'shield') if SHIELD else HERE
NAME = 'FSU_Shield' if SHIELD else 'FUJISOKU_SRAM_Utility'
NS = uuid.UUID('3b8e9a4c-1d2f-4e6a-8c7b-9f0e1d2c3b4a' if SHIELD else '6f1c1d1e-3c55-4b7a-9d0e-5a3f2b1c0d9e')

# シールド基板のフットプリント (DIP はソケット付き)
FP = {
    'A1': 'FSU:Arduino_UNO_R4_Shield',   # shield/FSU.pretty (gen_pcb.py が作る)
    'U1': 'Package_DIP:DIP-28_W7.62mm_Socket',
    'U2': 'Package_DIP:DIP-28_W7.62mm_Socket',
    'U3': 'Package_DIP:DIP-8_W7.62mm_Socket',
    'J1': 'Connector_IDC:IDC-Header_2x20_P2.54mm_Vertical',
    'J2': 'Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical',
    'J3': 'Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical',
    'R1': 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal',
    'C1': 'Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm',
    'C2': 'Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm',
    'C3': 'Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm',
    'C4': 'Capacitor_THT:CP_Radial_D5.0mm_P2.00mm',
} if SHIELD else {}

# ---------------------------------------------------------------------------------------------
# S 式
# ---------------------------------------------------------------------------------------------
TOK = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')
class Atom(str): pass
def parse(text):
    pos, stack = 0, [[]]
    while True:
        m = TOK.match(text, pos)
        if not m or m.end() == pos: break
        pos = m.end()
        if m.group(1): stack.append([])
        elif m.group(2): lst = stack.pop(); stack[-1].append(lst)
        elif m.group(3): stack[-1].append(m.group(3)[1:-1].replace('\\"', '"').replace('\\\\', '\\'))
        else: stack[-1].append(Atom(m.group(4)))
    return stack[0]
def num(v):
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s
def dump(x, ind=0):
    if isinstance(x, list):
        if all(not isinstance(e, list) for e in x): return '(' + ' '.join(dump(e) for e in x) + ')'
        out = '(' + dump(x[0])
        for e in x[1:]:
            out += ('\n' + '  ' * (ind + 1) + dump(e, ind + 1)) if isinstance(e, list) else (' ' + dump(e))
        return out + ')'
    if isinstance(x, Atom): return str(x)
    if isinstance(x, bool): return 'yes' if x else 'no'
    if isinstance(x, (int, float)): return num(x)
    return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'
def find(lst, key): return [e for e in lst if isinstance(e, list) and e and e[0] == key]
A = Atom
_n = [0]
def uid(tag=None):
    _n[0] += 1
    return str(uuid.uuid5(NS, '%s/%d' % (tag or '', _n[0])))

# ---------------------------------------------------------------------------------------------
# ライブラリ
# ---------------------------------------------------------------------------------------------
_libs = {}
def libsym(lib_id):
    lib, name = lib_id.split(':')
    if lib not in _libs:
        root = parse(open(os.path.join(LIBDIR, lib + '.kicad_sym'), encoding='utf-8').read())[0]
        _libs[lib] = {e[1]: e for e in find(root, 'symbol')}
    s = _libs[lib][name]
    assert not find(s, 'extends'), lib_id
    return s

def custom_23lc512():
    """SPI SRAM 23LC512 (8 ピン)。1 /CS, 2 SO, 3 NC, 4 VSS, 5 SI, 6 SCK, 7 /HOLD, 8 VCC"""
    def pin(typ, x, y, ang, name, number, length=2.54):
        return [A('pin'), A(typ), A('line'), [A('at'), x, y, ang], [A('length'), length],
                [A('name'), name, [A('effects'), [A('font'), [A('size'), 1.27, 1.27]]]],
                [A('number'), number, [A('effects'), [A('font'), [A('size'), 1.27, 1.27]]]]]
    def prop(k, v, y, hide=False):
        p = [A('property'), k, v, [A('at'), 0, y, 0], [A('effects'), [A('font'), [A('size'), 1.27, 1.27]]]]
        if hide: p[-1].append(A('hide'))
        return p
    return [A('symbol'), '23LC512', [A('in_bom'), A('yes')], [A('on_board'), A('yes')],
            prop('Reference', 'U', 10.16), prop('Value', '23LC512', -10.16),
            prop('Footprint', 'Package_DIP:DIP-8_W7.62mm', 0, True),
            prop('Datasheet', 'https://ww1.microchip.com/downloads/en/DeviceDoc/20005155B.pdf', 0, True),
            prop('ki_description', '512 Kbit (64 KB) SPI serial SRAM', 0, True),
            [A('symbol'), '23LC512_0_1',
             [A('rectangle'), [A('start'), -7.62, 7.62], [A('end'), 7.62, -7.62],
              [A('stroke'), [A('width'), 0.254], [A('type'), A('default')]], [A('fill'), [A('type'), A('background')]]]],
            [A('symbol'), '23LC512_1_1',
             pin('input', -10.16, 5.08, 0, '~{CS}', '1'),
             pin('output', 10.16, 5.08, 180, 'SO', '2'),
             pin('no_connect', -10.16, -5.08, 0, 'NC', '3'),
             pin('power_in', 0, -10.16, 90, 'VSS', '4'),
             pin('input', -10.16, 0, 0, 'SI', '5'),
             pin('input', -10.16, 2.54, 0, 'SCK', '6'),
             pin('input', -10.16, -2.54, 0, '~{HOLD}', '7'),
             pin('power_in', 0, 10.16, 270, 'VCC', '8')]]

CUSTOM = {'FSU:23LC512': custom_23lc512()}
def get_def(lib_id):
    return CUSTOM[lib_id] if lib_id in CUSTOM else libsym(lib_id)

def pins_of(sdef):
    out = {}
    def walk(x):
        for e in x:
            if isinstance(e, list) and e and e[0] == 'pin':
                at = find(e, 'at')[0]
                out[find(e, 'number')[0][1]] = (float(at[1]), float(at[2]), int(float(at[3])), find(e, 'name')[0][1], e[1])
            elif isinstance(e, list): walk(e)
    walk(sdef)
    return out

def body_of(sdef):
    """本体の四角 (ライブラリ座標、y は上向き) を返す。無ければピンから求める"""
    xs, ys = [], []
    def walk(x):
        for e in x:
            if isinstance(e, list) and e and e[0] == 'rectangle':
                for k in ('start', 'end'):
                    p = find(e, k)[0]; xs.append(float(p[1])); ys.append(float(p[2]))
            elif isinstance(e, list): walk(e)
    walk(sdef)
    if not xs:
        for (x, y, *_r) in pins_of(sdef).values(): xs.append(x); ys.append(y)
    return min(xs), max(xs), min(ys), max(ys)

# ---------------------------------------------------------------------------------------------
# 回路図の部品
# ---------------------------------------------------------------------------------------------
ROOT = uid('root')
items, used_libs = [], {}
pwr_count = [0]

def rot_xy(dx, dy, rot):
    """画面座標 (y 下向き) で、反時計回りに rot 度回す"""
    if rot == 0: return dx, dy
    if rot == 90: return dy, -dx
    if rot == 180: return -dx, -dy
    if rot == 270: return -dy, dx

def text_effects(size=1.27, justify=None, hide=False, bold=False):
    font = [A('font'), [A('size'), size, size]]
    if bold: font.append(A('bold'))
    e = [A('effects'), font]
    if justify: e.append([A('justify')] + [A(j) for j in justify.split()])
    if hide: e.append(A('hide'))
    return e

def symbol(lib_id, ref, value, x, y, rot=0, ref_at=None, val_at=None, extra=None, ref_hide=False, val_hide=False, dnp=False, val_angle=0):
    sdef = get_def(lib_id)
    used_libs[lib_id] = sdef
    props = []
    lib_props = {p[1]: p for p in find(sdef, 'property')}
    def lib_off(key):
        p = lib_props.get(key)
        at = find(p, 'at')[0] if p else [None, 0, 0, 0]
        dx, dy = rot_xy(float(at[1]), -float(at[2]), rot)
        return x + dx, y + dy
    rp = ref_at or (lib_off('Reference') + ('',))
    vp = val_at or (lib_off('Value') + ('',))
    props.append([A('property'), 'Reference', ref, [A('at'), rp[0], rp[1], 0], text_effects(justify=rp[2] or None, hide=ref_hide)])
    props.append([A('property'), 'Value', value, [A('at'), vp[0], vp[1], val_angle], text_effects(justify=vp[2] or None, hide=val_hide)])
    for key in ('Footprint', 'Datasheet'):
        v = lib_props[key][2] if key in lib_props else ''
        if key == 'Footprint' and ref in FP: v = FP[ref]
        props.append([A('property'), key, v, [A('at'), x, y, 0], text_effects(hide=True)])
    for k, v in (extra or {}).items():
        props.append([A('property'), k, v, [A('at'), x, y, 0], text_effects(hide=True)])
    pins = pins_of(sdef)
    node = [A('symbol'), [A('lib_id'), lib_id], [A('at'), x, y, rot], [A('unit'), 1],
            [A('in_bom'), A('yes')], [A('on_board'), A('yes')], [A('dnp'), A('yes' if dnp else 'no')],
            [A('uuid'), uid(ref)]] + props
    for n in sorted(pins, key=lambda s: (len(s), s)):
        node.append([A('pin'), n, [A('uuid'), uid(ref + '.' + n)]])
    node.append([A('instances'), [A('project'), NAME, [A('path'), '/' + ROOT, [A('reference'), ref], [A('unit'), 1]]]])
    items.append(node)
    return sdef

def pin_point(sdef, x, y, n, rot=0):
    px, py, ang, name, typ = pins_of(sdef)[n]
    dx, dy = rot_xy(px, -py, rot)
    out = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[ang]
    ox, oy = rot_xy(out[0], out[1], rot)
    return x + dx, y + dy, (round(ox), round(oy))

def wire(x1, y1, x2, y2):
    items.append([A('wire'), [A('pts'), [A('xy'), x1, y1], [A('xy'), x2, y2]],
                  [A('stroke'), [A('width'), 0], [A('type'), A('default')]], [A('uuid'), uid('w')]])

def junction(x, y):
    items.append([A('junction'), [A('at'), x, y], [A('diameter'), 0], [A('color'), 0, 0, 0, 0], [A('uuid'), uid('j')]])

def no_connect(x, y):
    items.append([A('no_connect'), [A('at'), x, y], [A('uuid'), uid('nc')]])

def label(name, x, y, d):
    ang, just = {(-1, 0): (180, 'right bottom'), (1, 0): (0, 'left bottom'),
                 (0, -1): (90, 'left bottom'), (0, 1): (270, 'right bottom')}[d]
    items.append([A('label'), name, [A('at'), x, y, ang], text_effects(justify=just), [A('uuid'), uid('l')]])

def power(kind, x, y, d=(0, -1)):
    """電源記号。d は線の向き (記号の頭が向く方向)"""
    pwr_count[0] += 1
    ref = '#PWR%03d' % pwr_count[0]
    if kind == 'GND':
        rot = {(0, 1): 0, (-1, 0): 270, (1, 0): 90, (0, -1): 180}[d]
    else:
        rot = {(0, -1): 0, (-1, 0): 90, (1, 0): 270, (0, 1): 180}[d]
    lib_id = 'power:' + ('GND' if kind == 'GND' else '+5V')
    sdef = get_def(lib_id); used_libs[lib_id] = sdef
    off = 3.81 if kind == 'GND' else 3.556
    vx, vy = x + d[0] * (off + 1.0), y + d[1] * off
    just = 'right' if d == (-1, 0) else ('left' if d == (1, 0) else None)
    if d[1] == 0: vy = y
    # 横向きの記号でも名前は横書きにする (KiCad は記号の向きに合わせて文字も回すので、その分を戻す)
    symbol(lib_id, ref, kind, x, y, rot, ref_at=(x, y, ''), val_at=(vx, vy, just or ''), ref_hide=True,
           val_angle=(360 - rot) % 360)

def connect(sdef, x, y, conns, rot=0, stub=2.54):
    """conns: {ピン番号: ネット名 | 'NC' | '+5V' | 'GND'}"""
    for n, net in conns.items():
        px, py, d = pin_point(sdef, x, y, n, rot)
        if net == 'NC':
            no_connect(px, py); continue
        ex, ey = px + d[0] * stub, py + d[1] * stub
        wire(px, py, ex, ey)
        if net in ('+5V', 'GND'): power(net, ex, ey, d)
        else: label(net, ex, ey, d)

def strap(sdef, x, y, conns, stub=5.08):
    """左側の隣り合うピンを +5V / GND に固定する (MCP23S17 のアドレスピンなど)。
    同じ電源のピンは縦の線でまとめ、+5V は上、GND は下に電源記号を 1 つだけ置く"""
    groups = {}
    for n, net in conns.items():
        px, py, d = pin_point(sdef, x, y, n)
        assert d == (-1, 0), 'strap は左側のピンだけ'
        ex = px - stub
        wire(px, py, ex, py)
        groups.setdefault(net, []).append((ex, py))
    for net, pts in groups.items():
        pts.sort(key=lambda p: p[1])
        ex = pts[0][0]
        # 縦の線は点ごとに区切る (途中に線の端が来るところで切っておかないと KiCad はつながったとみなさない)
        for (_x0, ya), (_x1, yb) in zip(pts, pts[1:]): wire(ex, ya, ex, yb)
        for (_x, py) in pts[1:-1]: junction(ex, py)
        if net == '+5V':
            top = pts[0][1]
            wire(ex, top, ex, top - 2.54); power('+5V', ex, top - 2.54, (0, -1))
            if len(pts) > 1: junction(ex, top)
        else:
            bot = pts[-1][1]
            wire(ex, bot, ex, bot + 2.54); power('GND', ex, bot + 2.54, (0, 1))
            if len(pts) > 1: junction(ex, bot)

def strap_bottom(sdef, x, y, pins, net='GND', stub=2.54):
    """下向きの隣り合うピン (Arduino の GND など) を横の線でまとめ、電源記号を 1 つだけ置く"""
    pts = []
    for n in pins:
        px, py, d = pin_point(sdef, x, y, n)
        assert d == (0, 1), 'strap_bottom は下側のピンだけ'
        wire(px, py, px, py + stub); pts.append((px, py + stub))
    pts.sort()
    yb = pts[0][1]
    for (xa, _ya), (xb, _yb) in zip(pts, pts[1:]): wire(xa, yb, xb, yb)   # 点ごとに区切る
    for (px, _y) in pts[1:-1]: junction(px, yb)
    mx = pts[len(pts) // 2][0]
    wire(mx, yb, mx, yb + 2.54); power(net, mx, yb + 2.54, (0, 1))

def text(s, x, y, size=1.27, bold=False):
    items.append([A('text'), s, [A('at'), x, y, 0], text_effects(size=size, justify='left bottom', bold=bold), [A('uuid'), uid('t')]])

def body_box(sdef, x, y):
    x0, x1, y0, y1 = body_of(sdef)
    return x + x0, x + x1, y - y1, y - y0   # 左, 右, 上, 下 (画面座標)

def ic(lib_id, ref, value, x, y, conns, note=None):
    """IC を置いて、ピンをつなぐ。部品番号は左上、型番と説明は下"""
    sdef = get_def(lib_id)
    l, r, t, b = body_box(sdef, x, y)
    symbol(lib_id, ref, value, x, y, ref_at=(l, t - 1.27, 'left'), val_at=(l, b + 13.97, 'left'))
    connect(sdef, x, y, conns)
    if note:
        for i, line in enumerate(note.split('\n')):
            text(line, l, b + 17.78 + i * 2.54)
    return sdef

# ---------------------------------------------------------------------------------------------
# 回路 (docs/hardware.md §3〜§9)
# ---------------------------------------------------------------------------------------------
ADDR = ['ADDR%d' % i for i in range(18)]
DATA = ['DATA%d' % i for i in range(8)]

# Arduino UNO R4 (KiCad の UNO R3 の形。ピン配置は同じ)
ic('MCU_Module:Arduino_UNO_R3', 'A1', 'Arduino UNO R4 (Minima / WiFi)', 70, 95, {
    '17': '~{CARD_DET}',   # D2: /CARD DET (F37)、内蔵プルアップ
    '18': 'NC',            # D3: 未接続 (PIN_MCP_INT)
    '19': '~{CS_MCP}',     # D4
    '20': '~{RESET_MCP}',  # D5
    '21': '~{CS_SRAM}',    # D6
    '22': '~{CS_SD}',      # D7
    '26': 'MOSI', '27': 'MISO', '28': 'SCK',
    '11': 'BATT_DET',      # A2
    '13': 'SDA', '14': 'SCL',
    '5': '+5V',
    '2': 'NC', '3': 'NC', '4': 'NC', '8': 'NC', '9': 'NC', '10': 'NC', '12': 'NC', '15': 'NC', '16': 'NC',
    '23': 'NC', '24': 'NC', '25': 'NC', '30': 'NC', '31': 'NC', '32': 'NC',
}, note='D2 = /CARD DET (F37), INPUT_PULLUP, L = card in\nA2 = BATT-DET (F2), analog in\nD3, D8-D10, A0, A1, A3: not used')
strap_bottom(get_def('MCU_Module:Arduino_UNO_R3'), 70, 95, ['29', '6', '7'])

# MCP23S17 ch6 (アドレス A0-A15)。A2:A0 = 110
mcp_common = {'9': '+5V', '10': 'GND', '11': '~{CS_MCP}', '12': 'SCK', '13': 'MOSI', '14': 'MISO',
              '18': '~{RESET_MCP}', '19': 'NC', '20': 'NC'}
c6 = dict(mcp_common)
c6.update({str(21 + i): ADDR[i] for i in range(8)})       # GPA0-7 = A0-A7
c6.update({str(1 + i): ADDR[8 + i] for i in range(8)})    # GPB0-7 = A8-A15
md = ic('Interface_Expansion:MCP23S17_SP', 'U1', 'MCP23S17 ch6 (address bus)', 180, 80, c6,
        note='Hardware address A2:A0 = 110 (ch6)\nGPA = A0-A7, GPB = A8-A15')
strap(md, 180, 80, {'15': 'GND', '16': '+5V', '17': '+5V'})   # A0 = 0, A1 = 1, A2 = 1

# MCP23S17 ch7 (データ D0-D7 と制御線)。A2:A0 = 111
c7 = dict(mcp_common)
c7.update({str(21 + i): DATA[i] for i in range(8)})       # GPA0-7 = D0-D7
c7.update({'1': 'CE2', '2': '~{OE_CARD}', '3': 'NC' if SHIELD else '~{OE_62256}', '4': '~{WE}', '5': '~{CE1}', '6': 'NC',
           '7': 'ADDR16', '8': 'ADDR17'})
ic('Interface_Expansion:MCP23S17_SP', 'U2', 'MCP23S17 ch7 (data + control)', 180, 185, c7,
   note=('Hardware address A2:A0 = 111 (ch7)\nGPA = D0-D7, GPB0 CE2, GPB1 card /OE,\nGPB2 not used (62256 /OE), GPB3 /WE, GPB4 /CE1, GPB5 unused, GPB6 A16, GPB7 A17'
         if SHIELD else
         'Hardware address A2:A0 = 111 (ch7)\nGPA = D0-D7, GPB0 CE2 (= 62256 /CS), GPB1 card /OE,\nGPB2 62256 /OE, GPB3 /WE, GPB4 /CE1, GPB5 unused, GPB6 A16, GPB7 A17'))
strap(md, 180, 185, {'15': '+5V', '16': '+5V', '17': '+5V'})  # A2:A0 = 111

# FUJISOKU カードのスロット (F1-F38)
card = {'1': 'GND', '2': 'BATT_DET', '3': '+5V', '4': '~{CE1}', '5': 'CE2', '6': '~{WE}', '7': '~{OE_CARD}'}
card.update({str(8 + i): DATA[i] for i in range(8)})       # F8-F15 = D0-D7
card.update({str(16 + i): ADDR[i] for i in range(16)})     # F16-F31 = A0-A15
card.update({'32': 'ADDR16', '33': 'ADDR17', '34': 'NC', '35': 'NC', '36': '+5V', '37': '~{CARD_DET}', '38': 'GND'})

def header_to_card(p):
    """シールドの 40 ピンヘッダの p 番ピンにつながるカードの F 番号 (39, 40 は空き)。
    1 = F2, 2 = F1, 3 = F4, 4 = F3 … 37 = F38, 38 = F37"""
    if p > 38: return None
    return p + 1 if p % 2 else p - 1

if SHIELD:
    # 40 ピン (2x20) ボックスヘッダ。ストレートのリボンケーブルでスロット基板の 40 ピンヘッダへ
    jdef = get_def('Connector_Generic:Conn_02x20_Odd_Even')
    JX, JY = 318, 128
    symbol('Connector_Generic:Conn_02x20_Odd_Even', 'J1', '40-pin box header to card slot board', JX, JY,
           ref_at=(JX + 1.27, JY - 26.67, ''), val_at=(JX + 1.27, JY + 29.21, ''))
    hdr = {str(p): (card[str(header_to_card(p))] if header_to_card(p) else 'NC') for p in range(1, 41)}
    connect(jdef, JX, JY, hdr, stub=7.62)
    for p in range(1, 39):   # 線の上にカードの F 番号を書く
        px, py, d = pin_point(jdef, JX, JY, str(p))
        text('F%d' % header_to_card(p), px - 6.6 if d[0] < 0 else px + 1.2, py - 0.5, size=1.0)
    for i, line in enumerate(['Header pin 1 = F2, 2 = F1, 3 = F4, 4 = F3 ... 37 = F38, 38 = F37',
                              '(pin 2k-1 = F2k, pin 2k = F2k-1). Pins 39, 40 not connected.',
                              'Straight 40-pin IDC ribbon cable to the slot board, pin 1 to pin 1.',
                              'F34 A18 / F35 A19 not connected.',
                              'F3/F36 (Vcc) and F1/F38 (GND) are joined inside the card.']):
        text(line, JX - 22, JY + 33 + i * 2.54)
else:
    jdef = get_def('Connector_Generic:Conn_01x38')
    JX, JY = 300, 128
    symbol('Connector_Generic:Conn_01x38', 'J1', 'FUJISOKU card slot (F1-F38)', JX, JY,
           ref_at=(JX - 3, JY - 50.8, 'left'), val_at=(JX - 3, JY + 53.34, 'left'))
    connect(jdef, JX, JY, card, stub=2.54)
    for i, line in enumerate(['Pin n = card pin Fn. F34 A18 / F35 A19 not connected.',
                              'F3/F36 (Vcc) and F1/F38 (GND) are joined inside the card.',
                              'Slot made with 2.54 mm <-> 1.27 mm adapters (docs/hardware.md 12.1).']):
        text(line, JX - 3, JY + 56.5 + i * 2.54)

# BATT-DET のプルダウン 1 MΩ (A2 と GND の間)
rdef = get_def('Device:R')
RX, RY = 262, 212
symbol('Device:R', 'R1', '1M', RX, RY, ref_at=(RX + 2.54, RY - 1.27, 'left'), val_at=(RX + 2.54, RY + 1.27, 'left'))
connect(rdef, RX, RY, {'1': 'BATT_DET', '2': 'GND'})
text('BATT-DET pull-down: no battery 0.24 V, new battery 2.75 V', RX - 12, RY + 15.24)

# SPI SRAM 23LC512 (オプション、/CS = D6)
ic('FSU:23LC512', 'U3', '23LC512 SPI SRAM (optional)', 70, 175, {
    '1': '~{CS_SRAM}', '2': 'MISO', '3': 'NC', '4': 'GND', '5': 'MOSI', '6': 'SCK', '7': '+5V', '8': '+5V'},
   note='USE_SPI_SRAM (auto-detected)')

# 62256 (オプション、動作確認用。シールドには載せない)
if not SHIELD:
    pdef = get_def('Memory_RAM:KM62256CLP')
    p = {'1': 'ADDR14', '2': 'ADDR12', '3': 'ADDR7', '4': 'ADDR6', '5': 'ADDR5', '6': 'ADDR4', '7': 'ADDR3', '8': 'ADDR2',
         '9': 'ADDR1', '10': 'ADDR0', '21': 'ADDR10', '23': 'ADDR11', '24': 'ADDR9', '25': 'ADDR8', '26': 'ADDR13',
         '11': 'DATA0', '12': 'DATA1', '13': 'DATA2', '15': 'DATA3', '16': 'DATA4', '17': 'DATA5', '18': 'DATA6', '19': 'DATA7',
         '20': 'CE2', '22': '~{OE_62256}', '27': '~{WE}', '14': 'GND', '28': '+5V'}
    ic('Memory_RAM:KM62256CLP', 'U4', '62256 parallel SRAM (optional)', 360, 105, p,
       note='USE_SRAM62256. /CS = CE2 (GPB0)\nTest SRAM for "test p"')

# SD カードモジュール (オプション、/CS = D7)
sdef6 = get_def('Connector_Generic:Conn_01x06')
SX, SY = 75, 222
symbol('Connector_Generic:Conn_01x06', 'J2', 'SD card module, 5 V type (optional)', SX, SY,
       ref_at=(SX - 3, SY - 8.89, 'left'), val_at=(SX + 4, SY - 1.27, 'left'))
connect(sdef6, SX, SY, {'1': 'GND', '2': '+5V', '3': 'MISO', '4': 'MOSI', '5': 'SCK', '6': '~{CS_SD}'})
text('USE_SD. 3.3 V regulator + level shifter on module', SX + 4, SY + 2.54)
text('(pin order differs by module)', SX + 4, SY + 5.08)

# LCD 20x4 + PCF8574 (オプション、I2C 0x27)
sdef4 = get_def('Connector_Generic:Conn_01x04')
LX, LY = 75, 250
symbol('Connector_Generic:Conn_01x04', 'J3', 'LCD 20x4, PCF8574 I2C backpack 0x27 (optional)', LX, LY,
       ref_at=(LX - 3, LY - 6.35, 'left'), val_at=(LX + 4, LY - 1.27, 'left'))
connect(sdef4, LX, LY, {'1': 'GND', '2': '+5V', '3': 'SDA', '4': 'SCL'})
text('USE_LCD (auto-detected)', LX + 4, LY + 2.54)

# パスコン (推奨値)
cdef = get_def('Device:C')
if SHIELD:
    caps = [('C1', '0.1u', 'Device:C'), ('C2', '0.1u', 'Device:C'), ('C3', '0.1u', 'Device:C'), ('C4', '10u', 'Device:C_Polarized')]
    cap_note = 'Bypass capacitors: C1 U1, C2 U2, C3 U3 (0.1u ceramic). C4 10u bulk near J1 (card supply)'
else:
    caps = [('C1', '0.1u', 'Device:C'), ('C2', '0.1u', 'Device:C'), ('C3', '0.1u', 'Device:C'), ('C4', '0.1u', 'Device:C')]
    cap_note = 'Bypass capacitors (recommended), one near each IC: C1 U1, C2 U2, C3 U3, C4 U4'
for i, (ref, val, lib) in enumerate(caps):
    cx, cy = 165 + i * 12.7, 250
    symbol(lib, ref, val, cx, cy, ref_at=(cx + 2.54, cy - 1.27, 'left'), val_at=(cx + 2.54, cy + 1.27, 'left'))
    connect(get_def(lib), cx, cy, {'1': '+5V', '2': 'GND'})
text(cap_note, 160, 266.7)

# GND の電源フラグ (ERC 用。+5V は Arduino の 5V ピンが出力)
fdef = get_def('power:PWR_FLAG'); used_libs['power:PWR_FLAG'] = fdef
symbol('power:PWR_FLAG', '#FLG01', 'PWR_FLAG', 225, 250, ref_at=(225, 250, ''), val_at=(225, 246, ''), ref_hide=True)
wire(225, 250, 225, 252.54)
power('GND', 225, 252.54, (0, 1))

# 全体の注記
notes = ['Single 5 V supply from the Arduino 5V pin. All parts are 5 V, no level shifting.',
         'SPI (D13 SCK, D12 MISO, D11 MOSI) is shared by both MCP23S17, the SPI SRAM and the SD card module.',
         'Labels with the same name are connected. Source: docs/hardware.md, FUJISOKU_SRAM_COPY/config.h (v2.8).']
if SHIELD:
    notes.append('Shield board: DIP parts in sockets, no 62256. The card slot board connects to J1 with a ribbon cable.')
text('FUJISOKU SRAM Utility - Arduino UNO R4 shield' if SHIELD else 'FUJISOKU SRAM Utility - schematic', 20, 22, size=2.5, bold=True)
for i, line in enumerate(notes):
    text(line, 20, 27 + i * 2.54)

# ---------------------------------------------------------------------------------------------
# 書き出し
# ---------------------------------------------------------------------------------------------
def lib_entry(lib_id, sdef):
    e = list(sdef); e[1] = lib_id
    return e

sch = [A('kicad_sch'), [A('version'), A('20230121')], [A('generator'), A('eeschema')], [A('uuid'), ROOT],
       [A('paper'), 'A3'],
       [A('title_block'), [A('title'), 'FUJISOKU SRAM Utility' + (' - UNO R4 shield' if SHIELD else '')],
        [A('date'), '2026-11-13' if SHIELD else '2026-11-12'], [A('rev'), 'shield 1.0' if SHIELD else '2.8'],
        [A('company'), 'github.com/cheeky-smokey/FUJISOKU-SRAM-Utility'],
        [A('comment'), 1, 'Arduino UNO R4 + MCP23S17 x 2 reader / writer for FUJISOKU SRAM memory cards'],
        [A('comment'), 2, 'Generated by hardware/kicad/gen_schematic.py%s from docs/hardware.md' % (' --shield' if SHIELD else '')]],
       [A('lib_symbols')] + [lib_entry(k, v) for k, v in sorted(used_libs.items())]]
sch += items
sch.append([A('sheet_instances'), [A('path'), '/', [A('page'), '1']]])
os.makedirs(OUTDIR, exist_ok=True)
with open(os.path.join(OUTDIR, NAME + '.kicad_sch'), 'w', encoding='utf-8') as f:
    f.write(dump(sch) + '\n')
if not SHIELD:   # シールドの .kicad_pro は gen_pcb.py が設計ルールと一緒に作る
    pro = {'meta': {'filename': NAME + '.kicad_pro', 'version': 1}, 'schematic': {'legacy_lib_dir': '', 'legacy_lib_list': []},
           'sheets': [[ROOT, '']], 'boards': [], 'libraries': {'pinned_footprint_libs': [], 'pinned_symbol_libs': []}}
    with open(os.path.join(OUTDIR, NAME + '.kicad_pro'), 'w', encoding='utf-8') as f:
        json.dump(pro, f, indent=2); f.write('\n')
print('wrote', os.path.relpath(os.path.join(OUTDIR, NAME + '.kicad_sch'), HERE))
