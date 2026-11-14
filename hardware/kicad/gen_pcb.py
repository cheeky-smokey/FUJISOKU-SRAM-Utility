#!/usr/bin/env python3
"""FUJISOKU SRAM Utility の Arduino UNO R4 シールド基板 (KiCad 7) を作る。

shield/FSU_Shield.kicad_sch (gen_schematic.py --shield が作る) のネットリストから基板を組み立て、
部品を置き、Freerouting で自動配線し、GND のベタを敷いて、DRC とガーバーの書き出しまで行う。

    python3 gen_schematic.py --shield
    python3 gen_pcb.py

要るもの: KiCad 7 (kicad-cli と pcbnew の Python モジュール)、KiCad の標準フットプリント
(既定 /usr/share/kicad/footprints、環境変数 KICAD7_FOOTPRINT_DIR で変更可)、
Java と Freerouting 1.9 (環境変数 FREEROUTING_JAR、既定 ~/tools/freerouting.jar)。
画面の無い環境では xvfb-run を使う。

できるもの (shield/):
    FSU_Shield.kicad_pcb / .kicad_pro / fp-lib-table / FSU.pretty/   KiCad のプロジェクト
    gerber/FSU_Shield_gerber.zip    基板メーカーに出すガーバーとドリル
    FSU_Shield_top.png, FSU_Shield_bottom.png   確認用の画像
    drc_report.txt                  DRC の結果

座標は Arduino の基板の左下を原点、右が +x、上が +y (mm)。Arduino の公式図面と同じ向き。
"""
import os, re, sys, json, math, shutil, subprocess, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SH = os.path.join(HERE, 'shield')
NAME = 'FSU_Shield'
FPDIR = os.environ.get('KICAD7_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
FREEROUTING = os.environ.get('FREEROUTING_JAR', os.path.expanduser('~/tools/freerouting.jar'))
PCB = os.path.join(SH, NAME + '.kicad_pcb')
PRO = os.path.join(SH, NAME + '.kicad_pro')
SCH = os.path.join(SH, NAME + '.kicad_sch')
NS = uuid.UUID('9d4c2b7e-5a61-4f3e-8b2d-1c0a9e8f7d6c')

# 設計ルール (JLCPCB などの標準 2 層、1.6 mm、1 oz で余裕のある値)
TRACK, POWER_TRACK, CLEAR = 0.3, 0.6, 0.25           # mm
VIA, VIA_DRILL = 0.8, 0.4

# ---------------------------------------------------------------------------------------------
# S 式 (gen_schematic.py と同じ)
# ---------------------------------------------------------------------------------------------
TOK = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')
class Atom(str): pass
A = Atom
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
    if isinstance(x, (int, float)): return num(x)
    return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'
def find(lst, key): return [e for e in lst if isinstance(e, list) and e and e[0] == key]
_n = [0]
def uid(tag=''):
    _n[0] += 1
    return str(uuid.uuid5(NS, '%s/%d' % (tag, _n[0])))

# ---------------------------------------------------------------------------------------------
# 1. シールド用の Arduino のフットプリント (FSU.pretty/Arduino_UNO_R4_Shield)
# ---------------------------------------------------------------------------------------------
# KiCad 標準の Module:Arduino_UNO_R3 は、上から見るとデジタル側のピンが下に来る (Arduino を裏返して
# 載せる向き)。シールドは Arduino と同じ向きで上に重ねるので、上下を反転したものを作る。
# 原点は Arduino の基板の左下の角。パッド番号は標準のまま (シンボル MCU_Module:Arduino_UNO_R3 と一致)。
UNO_HOLES = [(13.97, 2.54), (15.24, 50.8), (66.04, 7.62), (66.04, 35.56)]
UNO_OUTLINE = [(0, 0), (66.04, 0), (66.04, 2.54), (68.58, 5.08), (68.58, 37.85), (66.04, 40.39),
               (66.04, 51.82), (64.52, 53.34), (0, 53.34)]
FP_ARDUINO = 'Arduino_UNO_R4_Shield'

def make_arduino_footprint():
    src = parse(open(os.path.join(FPDIR, 'Module.pretty', 'Arduino_UNO_R3_WithMountingHoles.kicad_mod'), encoding='utf-8').read())[0]
    def tr(node):   # 標準のフットプリントの座標 (fx, fy) → シールドの座標 (x, -y)
        if not isinstance(node, list): return node
        if node and node[0] in ('at', 'start', 'end', 'xy', 'mid', 'center') and len(node) >= 3:
            out = [node[0], float(node[1]) + 27.94, -(float(node[2]) + 2.54)] + node[3:]
            if node[0] == 'at' and len(out) > 3 and not str(out[3]).startswith('unlocked'): out[3] = 0
            return out
        return [tr(e) for e in node]
    def layer_of(e):
        ls = find(e, 'layer')
        return ls[0][1] if ls else None
    out = [A('footprint'), FP_ARDUINO, [A('version'), A('20221018')], [A('generator'), A('pcbnew')], [A('layer'), 'F.Cu'],
           [A('descr'), 'Arduino UNO R3 / R4 (Minima, WiFi) shield, seen from the top. Vertically mirrored copy of '
                        'Module:Arduino_UNO_R3_WithMountingHoles (that one is drawn for an Arduino mounted upside down). '
                        'Origin = lower left corner of the Arduino board.'],
           [A('tags'), 'Arduino UNO R3 R4 shield'], [A('attr'), A('through_hole')]]
    stroke = lambda w: [A('stroke'), [A('width'), w], [A('type'), A('solid')]]
    out.append([A('fp_text'), A('reference'), 'REF**', [A('at'), 2, -51.5], [A('layer'), 'F.Fab'],
                [A('effects'), [A('font'), [A('size'), 1, 1], [A('thickness'), 0.15]]], [A('tstamp'), uid('fp')]])
    out.append([A('fp_text'), A('value'), FP_ARDUINO, [A('at'), 34, -27], [A('layer'), 'F.Fab'],
                [A('effects'), [A('font'), [A('size'), 1, 1], [A('thickness'), 0.15]]], [A('tstamp'), uid('fp')]])
    for e in src:
        if not isinstance(e, list): continue
        if e[0] == 'fp_line' and layer_of(e) == 'F.Fab':     # Arduino の外形と USB / DC ジャックの位置
            out.append(tr([A('fp_line'), find(e, 'start')[0], find(e, 'end')[0], stroke(0.1), [A('layer'), 'F.Fab'], [A('tstamp'), uid('fp')]]))
        elif e[0] == 'pad':
            p = tr([x for x in e if not (isinstance(x, list) and x and x[0] == 'tstamp')])
            p.append([A('tstamp'), uid('fp')])
            out.append(p)
    # ヘッダの列と取り付け穴の周りだけをコートヤード (部品どうしの重なりの確認範囲) にする
    for (x0, x1, y) in [(26.67, 64.77, 2.54), (17.53, 64.77, 50.8)]:
        out.append([A('fp_rect'), [A('start'), x0, -(y + 1.27)], [A('end'), x1, -(y - 1.27)], stroke(0.05), [A('fill'), A('none')], [A('layer'), 'F.CrtYd'], [A('tstamp'), uid('fp')]])
        out.append([A('fp_rect'), [A('start'), x0 + 0.1, -(y + 1.17)], [A('end'), x1 - 0.1, -(y - 1.17)], stroke(0.12), [A('fill'), A('none')], [A('layer'), 'F.SilkS'], [A('tstamp'), uid('fp')]])
    for (hx, hy) in UNO_HOLES:   # M3 のねじ頭の分
        out.append([A('fp_circle'), [A('center'), hx, -hy], [A('end'), hx + 3.0, -hy], stroke(0.05), [A('fill'), A('none')], [A('layer'), 'F.CrtYd'], [A('tstamp'), uid('fp')]])
    os.makedirs(os.path.join(SH, 'FSU.pretty'), exist_ok=True)
    with open(os.path.join(SH, 'FSU.pretty', FP_ARDUINO + '.kicad_mod'), 'w', encoding='utf-8') as f:
        f.write(dump(out) + '\n')
    with open(os.path.join(SH, 'fp-lib-table'), 'w', encoding='utf-8') as f:
        f.write('(fp_lib_table\n  (version 7)\n  (lib (name "FSU")(type "KiCad")(uri "${KIPRJMOD}/FSU.pretty")(options "")'
                '(descr "FUJISOKU SRAM Utility shield"))\n)\n')

# J1: キー付きの MIL メスソケット (基板用 2x20、2.54 mm)
FP_SOCKET = 'MIL_Socket_2x20_P2.54mm_Vertical'

def make_socket_footprint():
    """穴の並びと番号は Connector_IDC:IDC-Header_2x20_P2.54mm_Vertical と同じ
    (1 番 (0, 0)、2 番 (2.54, 0)、3 番 (0, 2.54) …)。キーは奇数ピンの列の側 (シールドでは奥 = U1 側)。
    挿さるスロットユニットの L 型ボックスヘッダの外形を F.Fab に描き、コートヤードもその大きさにする
    (ヘッダの口はシールドから約 2 mm 上に来るので、その下に背の高い部品を置かない)"""
    stroke = lambda w: [A('stroke'), [A('width'), w], [A('type'), A('solid')]]
    font = [A('effects'), [A('font'), [A('size'), 1, 1], [A('thickness'), 0.15]]]
    def rect(x0, y0, x1, y1, layer, w):
        return [A('fp_rect'), [A('start'), x0, y0], [A('end'), x1, y1], stroke(w), [A('fill'), A('none')],
                [A('layer'), layer], [A('tstamp'), uid('fp')]]
    out = [A('footprint'), FP_SOCKET, [A('version'), A('20221018')], [A('generator'), A('pcbnew')], [A('layer'), 'F.Cu'],
           [A('descr'), 'MIL (IDC) 2x20 female socket for PCB, 2.54 mm, keyed (mates with a 2x20 box header, '
                        'e.g. Hirose HIF3BA-40D-2.54R). Pads and numbering as Connector_IDC:IDC-Header_2x20_P2.54mm_Vertical. '
                        'Key on the odd-pin side. F.Fab also shows the mated right-angle box header.'],
           [A('tags'), 'MIL IDC socket female 2x20 keyed'], [A('attr'), A('through_hole')],
           [A('fp_text'), A('reference'), 'REF**', [A('at'), 1.27, -6.2], [A('layer'), 'F.SilkS'], font, [A('tstamp'), uid('fp')]],
           [A('fp_text'), A('value'), FP_SOCKET, [A('at'), 1.27, 54.6], [A('layer'), 'F.Fab'], font, [A('tstamp'), uid('fp')]]]
    bx0, bx1, by0, by1 = 1.27 - 3.1, 1.27 + 3.1, 24.13 - 27.94, 24.13 + 27.94       # ソケット本体 6.2 x 55.88
    out += [rect(bx0, by0, bx1, by1, 'F.Fab', 0.1), rect(bx0 - 0.11, by0 - 0.11, bx1 + 0.11, by1 + 0.11, 'F.SilkS', 0.12)]
    out += [rect(bx0 - 1.0, 22.13, bx0, 26.13, 'F.Fab', 0.1),                         # キー
            rect(bx0 - 1.11, 22.02, bx0 - 0.11, 26.24, 'F.SilkS', 0.12)]
    hx0, hx1, hy0, hy1 = 1.27 - 4.425, 1.27 + 4.425, 24.13 - 29.21, 24.13 + 29.21    # 挿さるボックスヘッダ 8.85 x 58.42
    out += [rect(hx0, hy0, hx1, hy1, 'F.Fab', 0.1), rect(hx0 - 0.25, hy0 - 0.25, hx1 + 0.25, hy1 + 0.25, 'F.CrtYd', 0.05)]
    out.append([A('fp_poly'), [A('pts'), [A('xy'), -3.3, -0.6], [A('xy'), -3.3, 0.6], [A('xy'), -2.4, 0]],    # 1 番ピンの印
                stroke(0.12), [A('fill'), A('solid')], [A('layer'), 'F.SilkS'], [A('tstamp'), uid('fp')]])
    for k in range(20):
        for col in range(2):
            n = 2 * k + col + 1
            pad = [A('pad'), str(n), A('thru_hole'), A('roundrect') if n == 1 else A('circle'), [A('at'), col * 2.54, k * 2.54],
                   [A('size'), 1.7, 1.7], [A('drill'), 1.0], [A('layers'), '*.Cu', '*.Mask']]
            if n == 1: pad.append([A('roundrect_rratio'), 0.147059])
            out.append(pad + [[A('tstamp'), uid('fp')]])
    with open(os.path.join(SH, 'FSU.pretty', FP_SOCKET + '.kicad_mod'), 'w', encoding='utf-8') as f:
        f.write(dump(out) + '\n')

# ---------------------------------------------------------------------------------------------
# 2. ネットリスト
# ---------------------------------------------------------------------------------------------
def read_netlist():
    net = os.path.join(SH, NAME + '.net')
    subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', net, SCH],
                   check=True, stdout=subprocess.DEVNULL)
    root = parse(open(net, encoding='utf-8').read())[0]
    os.remove(net)
    comps = {}
    for c in find(find(root, 'components')[0], 'comp'):
        ref = find(c, 'ref')[0][1]
        comps[ref] = {'value': find(c, 'value')[0][1], 'footprint': find(c, 'footprint')[0][1],
                      'tstamp': find(c, 'tstamps')[0][1]}
    pinnet = {}
    for n in find(find(root, 'nets')[0], 'net'):
        name = find(n, 'name')[0][1]
        nodes = [(find(nd, 'ref')[0][1], find(nd, 'pin')[0][1]) for nd in find(n, 'node')]
        if len(nodes) < 2: continue                  # 未接続のピン
        for nd in nodes: pinnet[nd] = name
    return comps, pinnet

# ---------------------------------------------------------------------------------------------
# 3. プロジェクトファイル (設計ルールとネットクラス)
# ---------------------------------------------------------------------------------------------
def write_project():
    # まず別のプロセスで KiCad に既定のプロジェクトファイルを作らせ、それを書き換える
    if os.path.exists(PRO): os.remove(PRO)
    code = 'import pcbnew,sys; b=pcbnew.NewBoard(sys.argv[1]); pcbnew.SaveBoard(sys.argv[1], b)'
    subprocess.run([sys.executable, '-c', code, PCB], check=True)
    pro = json.load(open(PRO))
    ds = pro['board']['design_settings']
    ds['rules'].update({'min_clearance': 0.2, 'min_track_width': 0.2, 'min_via_diameter': 0.6,
                        'min_through_hole_diameter': 0.3, 'min_via_annular_width': 0.13,
                        'min_copper_edge_clearance': 0.5, 'min_hole_clearance': 0.25, 'min_hole_to_hole': 0.25,
                        'min_silk_clearance': 0.0, 'min_text_height': 0.8, 'min_text_thickness': 0.12,
                        'min_resolved_spokes': 1})   # GND のピンは配線でもつながっているので、ベタへの足は 1 本でよい
    ds['track_widths'] = [0.0, TRACK, POWER_TRACK, 1.0]
    ds['via_dimensions'] = [{'diameter': 0.0, 'drill': 0.0}, {'diameter': VIA, 'drill': VIA_DRILL}]
    # 部品番号などの文字がシルクの枠に少し重なるのは注意 (warning) のままにする
    sev = ds.setdefault('rule_severities', {})
    sev.update({'silk_overlap': 'warning', 'silk_over_copper': 'warning', 'lib_footprint_issues': 'ignore',
                'lib_footprint_mismatch': 'ignore'})
    base = dict(pro['net_settings']['classes'][0]) if pro['net_settings'].get('classes') else {}
    base.update({'name': 'Default', 'clearance': CLEAR, 'track_width': TRACK, 'via_diameter': VIA, 'via_drill': VIA_DRILL})
    power = dict(base); power.update({'name': 'Power', 'track_width': POWER_TRACK})
    pro['net_settings']['classes'] = [base, power]
    pro['net_settings']['netclass_patterns'] = [{'netclass': 'Power', 'pattern': '+5V'}, {'netclass': 'Power', 'pattern': 'GND'}]
    pro['meta']['filename'] = NAME + '.kicad_pro'
    with open(PRO, 'w') as f: json.dump(pro, f, indent=2); f.write('\n')
    os.remove(PCB)

# ---------------------------------------------------------------------------------------------
# 4. 配置
# ---------------------------------------------------------------------------------------------
# 置き方 (部品の 1 番ピンの位置と向き)。向きは KiCad の回転 (反時計回り、度)。
#   DIP を 90 度: 1 番ピンが左下、1-14 番が下の列を右へ。270 度: 1 番ピンが右上、1-14 番が上の列を左へ。
#   ボックスヘッダを 270 度: 1 番ピンが右端、奇数ピンが上の列、偶数ピンが下の列。
# 避ける場所: DC ジャックの上 (x < 13.5, y < 13.5)、ICSP の上 (x > 61.5, 22 < y < 34)、取り付け穴の周り
LAYOUT = {
    'A1': (0, 0, 0),
    'J1': (55.88, 26.67, 270),     # カード用 40 ピンのメスソケット。1 番 (F2) が右端、キーは奥 (U1 側)
    'U1': (50.80, 44.45, 270),     # MCP23S17 ch6 (アドレス)。1 番ピンが右上
    'U2': (58.42, 15.24, 270),     # MCP23S17 ch7 (データ・制御)。1 番ピンが右上 (VDD / VSS が J1 側の列)
    'U3': (5.08, 36.83, 90),       # 23LC512
    'J2': (58.42, 46.99, 0),       # SD カードモジュール (1 番 GND が上)
    'J3': (65.41, 20.32, 0),       # LCD (1 番 GND が上)
    'R1': (13.97, 17.78, 0),       # BATT-DET のプルダウン
    'C4': (17.00, 10.16, 0),       # 10u (カードの電源)
    'C3': (5.08, 47.70, 0),        # U3 のパスコン
}

def cap_for(board, ic_ref, cap_ref, inner_y, outer_y, inner_row_y):
    """MCP23S17 の VDD (9) / VSS (10) ピンのすぐ横にパスコンを置く位置を返す"""
    fp = board.FindFootprintByReference(ic_ref)
    p9 = [p for p in fp.Pads() if p.GetNumber() == '9'][0].GetPosition()
    p10 = [p for p in fp.Pads() if p.GetNumber() == '10'][0].GetPosition()
    x9, y9 = to_real(p9); x10, _y = to_real(p10)
    y = inner_y if abs(y9 - inner_row_y) < 0.1 else outer_y
    return (x9, y, 0 if x10 > x9 else 180)

X0, Y0 = 100.0, 130.0      # Arduino の基板の左下を KiCad の座標のここに置く
def P(x, y):
    import pcbnew
    return pcbnew.VECTOR2I(pcbnew.FromMM(X0 + x), pcbnew.FromMM(Y0 - y))
def to_real(v):
    import pcbnew
    return round(pcbnew.ToMM(v.x) - X0, 4), round(Y0 - pcbnew.ToMM(v.y), 4)

def build_board(comps, pinnet):
    import pcbnew
    board = pcbnew.NewBoard(PCB)
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.SetAuxOrigin(P(0, 0))          # ガーバーとドリルの原点 = 基板の左下
    ds.SetGridOrigin(P(0, 0))
    nets = {}
    for name in sorted(set(pinnet.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    board.SynchronizeNetsAndNetClasses(True)
    for ref in sorted(comps):
        c = comps[ref]
        lib, name = c['footprint'].split(':')
        libpath = os.path.join(SH, 'FSU.pretty') if lib == 'FSU' else os.path.join(FPDIR, lib + '.pretty')
        fp = pcbnew.FootprintLoad(libpath, name)
        assert fp, c['footprint']
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(c['value'])
        fp.SetPath(pcbnew.KIID_PATH('/' + c['tstamp']))
        board.Add(fp)
        for pad in fp.Pads():
            n = pinnet.get((ref, pad.GetNumber()))
            if n: pad.SetNet(nets[n])
    def place(ref, x, y, rot):
        fp = board.FindFootprintByReference(ref)
        fp.SetPosition(P(x, y)); fp.SetOrientationDegrees(rot)
    for ref, (x, y, rot) in LAYOUT.items(): place(ref, x, y, rot)
    # パスコン (足の間隔 5 mm): U1・U2 とも J1 側 (VDD / VSS のある列の外)。
    # U2 と Arduino の電源ヘッダの間は 2.3 mm しかなく、5 mm 間隔のパスコンが入らないので U2 は 270 度にした
    place('C1', *cap_for(board, 'U1', 'C1', inner_y=33.3, outer_y=47.7, inner_row_y=36.83))
    # (U1 / U2 / J1 の向きは 8 通りを自動配線で比べ、全部つながってビアが最も少ないものにした)
    place('C2', *cap_for(board, 'U2', 'C2', inner_y=18.6, outer_y=4.95, inner_row_y=15.24))
    return board, nets

def finish_text(board):
    """部品番号の位置を整え、シルクに説明を書く"""
    import pcbnew
    def ref_at(ref, x, y, size=1.0, angle=0):
        fp = board.FindFootprintByReference(ref)
        t = fp.Reference(); t.SetPosition(P(x, y))
        t.SetTextAngleDegrees((angle - fp.GetOrientationDegrees()) % 360)   # 文字の角度は部品の向きからの相対
        t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size))); t.SetTextThickness(pcbnew.FromMM(0.15))
        fp.Value().SetVisible(False)
    def silk(text, x, y, size=1.0, layer=None, just='center', angle=0, bold=False):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text); t.SetPosition(P(x, y))
        t.SetLayer(pcbnew.F_SilkS if layer is None else layer)
        t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
        t.SetTextThickness(pcbnew.FromMM(size * (0.2 if bold else 0.15)))
        t.SetHorizJustify({'left': pcbnew.GR_TEXT_H_ALIGN_LEFT, 'right': pcbnew.GR_TEXT_H_ALIGN_RIGHT,
                           'center': pcbnew.GR_TEXT_H_ALIGN_CENTER}[just])
        t.SetTextAngleDegrees(angle)
        if layer == pcbnew.B_SilkS: t.SetMirrored(True)
        board.Add(t)
    # 部品番号: IC はソケットの中、他は部品の横
    ref_at('U1', 34.29, 40.64); ref_at('U2', 41.91, 11.43); ref_at('U3', 8.89, 40.64)
    ref_at('A1', 2.5, 51.3, 0.8)
    ref_at('J1', 62.6, 30.6); ref_at('J2', 55.4, 46.99); ref_at('J3', 65.41, 10.5)
    ref_at('C3', 3.2, 47.7); ref_at('C4', 18.0, 13.9); ref_at('R1', 17.78, 17.78, 0.8)
    for ref in ('C1', 'C2'):        # パスコンの番号は左のパッドのさらに左
        pads = [to_real(p.GetPosition()) for p in board.FindFootprintByReference(ref).Pads()]
        x, y = min(pads)
        ref_at(ref, x - 1.9, y)
    for ref in ('J1', 'J2', 'J3', 'R1', 'C1', 'C2', 'C3', 'C4'):
        board.FindFootprintByReference(ref).Value().SetVisible(False)
    # 中身の説明 (IC の中)
    silk('MCP23S17 ch6 ADDR', 34.29, 39.0, 0.8)
    silk('MCP23S17 ch7 DATA', 41.91, 9.8, 0.8)
    silk('23LC512', 8.89, 39.0, 0.8)
    # コネクタのピン名
    for i, s in enumerate(['GND', '5V', 'MISO', 'MOSI', 'SCK', 'CS']):
        silk(s, 60.2, 46.99 - 2.54 * i, 0.8, just='left')
    silk('SD', 55.4, 45.4, 0.8)
    for i, s in enumerate(['GND', '5V', 'SDA', 'SCL']):
        silk(s, 63.6, 20.32 - 2.54 * i, 0.8, just='right')
    silk('LCD', 65.41, 22.3, 0.8)
    silk('KEY', 31.75, 30.7, 0.8)                     # J1 のキー (奥)
    silk('CARD', 61.9, 28.9, 0.8, just='left')
    silk('1=F2', 61.9, 26.67, 0.8, just='left')
    silk('2=F1', 61.9, 24.13, 0.8, just='left')
    silk('Powered by\nCheeky-Smokey', 1.6, 6.4, 1.0, just='left')   # 左下 (取り付け穴の上、C4 の左)
    # 裏面: 名前と説明 (パッドの列の間のすき間に書く)
    B = pcbnew.B_SilkS
    silk('FUJISOKU SRAM Utility shield rev 2.0', 34.3, 33.6, 1.0, B, bold=True)
    silk('github.com/cheeky-smokey/FUJISOKU-SRAM-Utility', 34.3, 31.6, 0.8, B)
    silk('J1: slot unit plugs in directly (keyed, key = U1 side)', 40.6, 21.6, 0.8, B)
    silk('pin 1=F2 2=F1 3=F4 4=F3 ... 37=F38 38=F37', 40.6, 20.0, 0.8, B)
    silk('39/40 NC.  5 V only, no level shifting', 34.3, 29.6, 0.8, B)   # C2 のパッドを避けて J1 と U1 の間に

def edge_cuts(board):
    import pcbnew
    pts = UNO_OUTLINE + [UNO_OUTLINE[0]]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(x1, y1)); s.SetEnd(P(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(pcbnew.FromMM(0.1))
        board.Add(s)

def gnd_zones(board, nets):
    import pcbnew
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(nets['GND'])
        z.SetLocalClearance(pcbnew.FromMM(0.3))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetThermalReliefGap(pcbnew.FromMM(0.5))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetAssignedPriority(0)
        z.SetZoneName('GND_' + ('top' if layer == pcbnew.F_Cu else 'bottom'))
        ol = z.Outline(); ol.NewOutline()
        for (x, y) in UNO_OUTLINE: ol.Append(P(x, y))
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

# ---------------------------------------------------------------------------------------------
# 5. 自動配線 (Freerouting) とその結果の取り込み
# ---------------------------------------------------------------------------------------------
def autoroute(board, passes=60):
    import pcbnew, tempfile
    work = tempfile.mkdtemp(prefix='fsu_route_')    # Freerouting はログなどを作業フォルダに作るので別の場所で動かす
    dsn = os.path.join(work, NAME + '.dsn'); ses = os.path.join(work, NAME + '.ses')
    assert pcbnew.ExportSpecctraDSN(board, dsn), 'DSN export failed'
    cmd = ['java', '-jar', FREEROUTING, '-de', dsn, '-do', ses, '-mp', str(passes), '-mt', '1', '-da']
    if not os.environ.get('DISPLAY'): cmd = ['xvfb-run', '-a'] + cmd
    log = subprocess.run(cmd, capture_output=True, text=True, timeout=1800, cwd=work)
    if not os.path.exists(ses):
        print(log.stdout[-3000:], log.stderr[-3000:]); raise SystemExit('Freerouting failed')
    for line in (log.stdout + log.stderr).splitlines():
        if re.search(r'unrouted|completed', line, re.I): print('  freerouting:', line.split('INFO')[-1].strip()[:160])
    n = import_ses(board, ses)
    shutil.rmtree(work, ignore_errors=True)
    return n

def import_ses(board, path):
    """Specctra のセッションファイルの配線とビアを基板に入れる (KiCad 7 の pcbnew モジュールには
    画面無しで使える取り込み関数が無いので自分で読む)。DSN の座標は y が上向き。"""
    import pcbnew
    root = parse(open(path, encoding='utf-8').read())[0]
    routes = find(root, 'routes')[0]
    res = find(routes, 'resolution')[0]
    unit = {'um': 1000.0, 'mm': 1e6, 'mil': 25400.0, 'inch': 25.4e6}[res[1]] / float(res[2])   # 1 単位 = ? nm
    lay = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
    vias = {}
    for ps in find(find(routes, 'library_out')[0], 'padstack') if find(routes, 'library_out') else []:
        m = re.search(r'_(\d+):(\d+)_um', ps[1])
        vias[ps[1]] = (int(m.group(1)) * 1000, int(m.group(2)) * 1000) if m else (pcbnew.FromMM(VIA), pcbnew.FromMM(VIA_DRILL))
    count = 0
    netmap = {str(k): v for k, v in board.GetNetsByName().items()}
    for n in find(find(routes, 'network_out')[0], 'net'):
        ni = netmap[str(n[1])]
        for w in find(n, 'wire'):
            for pth in find(w, 'path'):
                layer, width = pth[1], float(pth[2]) * unit
                xy = [float(v) * unit for v in pth[3:] if not isinstance(v, list)]
                pts = [pcbnew.VECTOR2I(int(round(xy[i])), int(round(-xy[i + 1]))) for i in range(0, len(xy) - 1, 2)]
                for a, b in zip(pts, pts[1:]):
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(a); t.SetEnd(b); t.SetWidth(int(round(width)))
                    t.SetLayer(lay[layer]); t.SetNet(ni)
                    board.Add(t); count += 1
        for v in find(n, 'via'):
            size, drill = vias.get(v[1], (pcbnew.FromMM(VIA), pcbnew.FromMM(VIA_DRILL)))
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(int(round(float(v[2]) * unit)), int(round(-float(v[3]) * unit))))
            via.SetWidth(size); via.SetDrill(drill)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetNet(ni)
            board.Add(via); count += 1
    return count

# ---------------------------------------------------------------------------------------------
# 6. 確認と書き出し
# ---------------------------------------------------------------------------------------------
def run_drc(board):
    import pcbnew
    rpt = os.path.join(SH, 'drc_report.txt')
    pcbnew.WriteDRCReport(board, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
    text = open(rpt, encoding='utf-8').read()
    text = re.sub(r'^\*\* Created on .*$', '** Created by gen_pcb.py', text, flags=re.M)
    text = text.replace(SH + os.sep, '')
    open(rpt, 'w', encoding='utf-8').write(text)
    viol = re.search(r'\*\* Found (\d+) DRC violations', text)
    unc = re.search(r'\*\* Found (\d+) unconnected pads', text)
    return int(viol.group(1)) if viol else -1, int(unc.group(1)) if unc else -1, text

def check_board_nets(board):
    """基板の銅のつながり (パッドのネット) を check_netlist.py --shield で仕様書の表と照らし合わせる"""
    import pcbnew
    nets = {}
    for p in board.GetPads():
        if p.GetNetCode() > 0:
            nets.setdefault(p.GetNetname(), []).append((p.GetParent().GetReference(), p.GetNumber()))
    sx = [A('export'), [A('nets')] + [[A('net'), [A('name'), n]] + [[A('node'), [A('ref'), r], [A('pin'), pin]] for r, pin in sorted(v)]
                                      for n, v in sorted(nets.items())]]
    path = os.path.join(SH, '_board_nets.net')
    open(path, 'w', encoding='utf-8').write(dump(sx) + '\n')
    r = subprocess.run([sys.executable, os.path.join(HERE, 'check_netlist.py'), '--shield', path], capture_output=True, text=True)
    os.remove(path)
    return r.returncode == 0, r.stdout.strip()

def export_outputs():
    gdir = os.path.join(SH, 'gerber')
    shutil.rmtree(gdir, ignore_errors=True); os.makedirs(gdir)
    tmp = os.path.join(gdir, 'tmp'); os.makedirs(tmp)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'gerbers', '--output', tmp + os.sep,
                    '--layers', 'F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts',
                    '--subtract-soldermask', '--use-drill-file-origin', PCB], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'drill', '--output', tmp + os.sep, '--format', 'excellon',
                    '--drill-origin', 'plot', '--excellon-units', 'mm', '--excellon-separate-th', PCB],
                   check=True, stdout=subprocess.DEVNULL)
    import zipfile
    zpath = os.path.join(gdir, NAME + '_gerber.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(tmp)):
            info = zipfile.ZipInfo(f, date_time=(2026, 11, 13, 0, 0, 0))   # 毎回同じ zip になるように
            info.compress_type = zipfile.ZIP_DEFLATED
            data = open(os.path.join(tmp, f), 'rb').read()
            fixed = b'2026-11-13T00:00:00+00:00'      # 作成日時を固定 (作り直しても差分が出ないように)
            data = re.sub(rb'(G04 #@! TF\.CreationDate,|; #@! TF\.CreationDate,|%TF\.CreationDate,)[^*\n]*', rb'\g<1>' + fixed, data)
            data = re.sub(rb'("CreationDate":\s*")[^"]*', rb'\g<1>' + fixed, data)
            data = re.sub(rb'(G04 Created by KiCad \([^)]*\) date )[^*\n]*', rb'\g<1>2026-11-13 00:00:00', data)
            data = re.sub(rb'(; DRILL file \{[^}]*\} date )[^\n]*', rb'\g<1>2026-11-13 00:00:00', data)
            z.writestr(info, data)
    shutil.rmtree(tmp)
    # 原寸の PDF (A4 に 100 % で印刷して Arduino を重ね、ピンの位置を確かめる用)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'pdf', '--output', os.path.join(SH, NAME + '_1to1.pdf'),
                    '--layers', 'Edge.Cuts,F.Mask,F.SilkS', '--black-and-white', PCB], check=True, stdout=subprocess.DEVNULL)
    return zpath

def render_png(board, width=1600):
    """確認用の画像 (表と裏、基板メーカーで作ったときの見た目に近い色)。
    層ごとに白黒の SVG を書き出して rsvg-convert で画像にし、緑のレジスト・金色のパッド・白いシルクで重ねる。
    穴は基板のデータから描く。裏は左右反転 (裏から見た向き)"""
    import pcbnew
    from PIL import Image, ImageDraw, ImageChops
    tmp = os.path.join(SH, '_render'); os.makedirs(tmp, exist_ok=True)
    def raster(layer, mirror):
        svg = os.path.join(tmp, layer + '.svg'); png = os.path.join(tmp, layer + ('_m' if mirror else '') + '.png')
        cmd = ['kicad-cli', 'pcb', 'export', 'svg', '--output', svg, '--layers', layer, '--black-and-white',
               '--page-size-mode', '2', '--exclude-drawing-sheet', '--drill-shape-opt', '0']
        if mirror: cmd.append('--mirror')
        subprocess.run(cmd + [PCB], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['rsvg-convert', '-w', str(width), svg, '-o', png], check=True)
        im = Image.open(png).convert('RGBA')
        a = im.getchannel('A').point(lambda v: 255 if v > 100 else 0)
        return a                                    # 描かれている所 = 255
    holes = [(to_real(p.GetPosition()), pcbnew.ToMM(p.GetDrillSize().x)) for p in board.GetPads() if p.GetDrillSize().x > 0]
    holes += [(to_real(v.GetPosition()), pcbnew.ToMM(v.GetDrillValue())) for v in board.GetTracks() if v.GetClass() == 'PCB_VIA']
    out = []
    for side, cu, mask, silk in (('top', 'F.Cu', 'F.Mask', 'F.SilkS'), ('bottom', 'B.Cu', 'B.Mask', 'B.SilkS')):
        mirror = side == 'bottom'
        edge = raster('Edge.Cuts', mirror)
        W, H = edge.size
        # 外側を塗りつぶして基板の範囲を求める
        fill = Image.new('L', (W + 2, H + 2), 0); fill.paste(edge, (1, 1))
        ImageDraw.floodfill(fill, (0, 0), 128)
        board_m = fill.crop((1, 1, W + 1, H + 1)).point(lambda v: 0 if v == 128 else 255)
        bb = edge.getbbox()                         # 外形の線の範囲 = 0..68.58 mm, 0..53.34 mm
        sx = (bb[2] - bb[0]) / 68.58; sy = (bb[3] - bb[1]) / 53.34
        def px(x, y):
            if mirror: x = 68.58 - x
            return bb[0] + x * sx, bb[1] + (53.34 - y) * sy
        cu_m, mask_m, silk_m = raster(cu, mirror), raster(mask, mirror), raster(silk, mirror)
        img = Image.new('RGB', (W, H), (255, 255, 255))
        img.paste((22, 92, 50), mask=board_m)                                   # レジスト (緑)
        img.paste((44, 128, 72), mask=ImageChops.multiply(cu_m, board_m))       # レジストの下の銅
        pad = ImageChops.multiply(cu_m, mask_m)
        img.paste((214, 172, 80), mask=pad)                                     # むき出しのパッド
        img.paste((245, 245, 240), mask=ImageChops.multiply(silk_m, ImageChops.invert(mask_m)))  # シルク
        d = ImageDraw.Draw(img)
        for (x, y), dia in holes + [((hx, hy), 3.2) for (hx, hy) in UNO_HOLES]:
            cx, cy = px(x, y); r = dia / 2 * sx
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(40, 40, 40))
        path = os.path.join(SH, '%s_%s.png' % (NAME, side))
        m = 12                                       # 基板の周りだけ残す
        img.crop((max(bb[0] - m, 0), max(bb[1] - m, 0), min(bb[2] + m, W), min(bb[3] + m, H))).save(path, optimize=True)
        out.append(path)
    shutil.rmtree(tmp)
    return out

def retext():
    """配線はそのままで、シルクの文字だけ書き直して出力し直す (python3 gen_pcb.py --retext)"""
    import pcbnew
    board = pcbnew.LoadBoard(PCB)
    old = [d for d in board.GetDrawings() if d.GetClass() == 'PCB_TEXT' and d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS)]
    finish_text(board)   # 先に書く (Remove の後だと FindFootprintByReference が型の無いオブジェクトを返すことがある)
    for d in old: board.Remove(d)
    pcbnew.SaveBoard(PCB, board)
    board = pcbnew.LoadBoard(PCB)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    return board

def main():
    import pcbnew
    os.makedirs(SH, exist_ok=True)
    if '--retext' in sys.argv:
        board = retext()
        viol, unc, _t = run_drc(board)
        print('DRC violations: %d, unconnected: %d' % (viol, unc))
        ok, out = check_board_nets(board)
        print('board nets vs docs/hardware.md:', out.splitlines()[-1])
        if not ok or viol or unc: raise SystemExit(out)
        print('gerber:', os.path.relpath(export_outputs(), HERE))
        for f in render_png(board): print('image:', os.path.relpath(f, HERE))
        return
    make_arduino_footprint()
    make_socket_footprint()
    comps, pinnet = read_netlist()
    write_project()
    # Freerouting は実行ごとに結果が少し変わり、まれに 1 本残すことがあるので、全部つながるまでやり直す
    for attempt in range(5):
        board, nets = build_board(comps, pinnet)
        edge_cuts(board)
        finish_text(board)
        n = autoroute(board, passes=60 + 40 * attempt)
        _v, unc, _t = run_drc(board)
        if unc == 0: break
        print('  %d connection(s) left unrouted, routing again' % unc)
    else:
        raise SystemExit('autorouting did not finish')
    gnd_zones(board, nets)
    pcbnew.SaveBoard(PCB, board)
    board = pcbnew.LoadBoard(PCB)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    viol, unc, _t = run_drc(board)
    print('routed items: %d, DRC violations: %d, unconnected: %d' % (n, viol, unc))
    ok, out = check_board_nets(board)
    print('board nets vs docs/hardware.md:', out.splitlines()[-1])
    if not ok: raise SystemExit(out)
    print('gerber:', os.path.relpath(export_outputs(), HERE))
    for f in render_png(board): print('image:', os.path.relpath(f, HERE))

if __name__ == '__main__':
    main()
