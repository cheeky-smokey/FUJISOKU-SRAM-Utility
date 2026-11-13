#!/usr/bin/env python3
"""回路図の結線を確かめる。KiCad でネットリストを書き出し、docs/hardware.md の結線表と比べる。

使い方 (KiCad 7 以降の kicad-cli が要る):
    kicad-cli sch export netlist --format kicadsexpr -o /tmp/fsu.net FUJISOKU_SRAM_Utility.kicad_sch
    python3 check_netlist.py /tmp/fsu.net
シールド基板 (shield/FSU_Shield.kicad_sch) は --shield を付ける:
    python3 check_netlist.py --shield /tmp/shield.net
問題が無ければ OK と出る。
"""
import sys, re
TOK = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')
def parse(text):
    pos, stack = 0, [[]]
    while True:
        m = TOK.match(text, pos)
        if not m or m.end() == pos: break
        pos = m.end()
        if m.group(1): stack.append([])
        elif m.group(2): lst = stack.pop(); stack[-1].append(lst)
        elif m.group(3): stack[-1].append(m.group(3)[1:-1])
        else: stack[-1].append(m.group(4))
    return stack[0]
def find(lst, key): return [e for e in lst if isinstance(e, list) and e and e[0] == key]
SHIELD = '--shield' in sys.argv[1:]
root = parse(open([a for a in sys.argv[1:] if not a.startswith('--')][0]).read())[0]

def F(f):
    """カードの F 番号 f につながる J1 のピン。シールドは 40 ピンヘッダ (1 = F2, 2 = F1, 3 = F4 …)"""
    if not SHIELD: return ('J1', str(f))
    return ('J1', str(f - 1 if f % 2 == 0 else f + 1))
def U4(*pins):
    """62256 のピン (シールドには無い)"""
    return [] if SHIELD else [('U4', p) for p in pins]
nets = {}
for n in find(find(root, 'nets')[0], 'net'):
    name = find(n, 'name')[0][1]
    nodes = {(find(nd, 'ref')[0][1], find(nd, 'pin')[0][1]) for nd in find(n, 'node')}
    nets[name] = nodes
where = {}
for name, nodes in nets.items():
    for nd in nodes: where[nd] = name
errors = []
def same(*pins, net=None, exact=False):
    ns = {where.get(p) for p in pins}
    if None in ns or len(ns) != 1: errors.append(('split', pins, [where.get(p) for p in pins])); return
    nm = ns.pop()
    if net and nm.lstrip('/') != net: errors.append(('name', net, nm))
    if exact and nets[nm] != set(pins): errors.append(('extra', nm, sorted(nets[nm] - set(pins))))
# データ・アドレス
for i in range(8): same(F(8 + i), ('U2', str(21 + i)), *U4(['11','12','13','15','16','17','18','19'][i]), net='DATA%d' % i, exact=True)
u4a = ['10','9','8','7','6','5','4','3','25','24','21','23','2','26','1']
for i in range(16):
    pins = [F(16 + i), ('U1', str(21 + i) if i < 8 else str(1 + i - 8))]
    if i < 15: pins += U4(u4a[i])
    same(*pins, net='ADDR%d' % i, exact=True)
same(F(32), ('U2', '7'), net='ADDR16', exact=True)
same(F(33), ('U2', '8'), net='ADDR17', exact=True)
# 制御線
same(F(5), ('U2', '1'), *U4('20'), net='CE2', exact=True)
same(F(7), ('U2', '2'), exact=True)               # card /OE
if not SHIELD: same(('U2', '3'), ('U4', '22'), exact=True)   # 62256 /OE
same(F(6), ('U2', '4'), *U4('27'), exact=True)    # /WE
same(F(4), ('U2', '5'), exact=True)               # /CE1
same(F(37), ('A1', '17'), exact=True)             # /CARD DET - D2
same(F(2), ('A1', '11'), ('R1', '1'), net='BATT_DET', exact=True)
# SPI と I2C
same(('A1', '28'), ('U1', '12'), ('U2', '12'), ('U3', '6'), ('J2', '5'), net='SCK', exact=True)
same(('A1', '26'), ('U1', '13'), ('U2', '13'), ('U3', '5'), ('J2', '4'), net='MOSI', exact=True)
same(('A1', '27'), ('U1', '14'), ('U2', '14'), ('U3', '2'), ('J2', '3'), net='MISO', exact=True)
same(('A1', '19'), ('U1', '11'), ('U2', '11'), exact=True)   # D4 /CS MCP
same(('A1', '20'), ('U1', '18'), ('U2', '18'), exact=True)   # D5 /RESET
same(('A1', '21'), ('U3', '1'), exact=True)                  # D6 /CS SRAM
same(('A1', '22'), ('J2', '6'), exact=True)                  # D7 /CS SD
same(('A1', '13'), ('J3', '3'), net='SDA', exact=True)
same(('A1', '14'), ('J3', '4'), net='SCL', exact=True)
# 電源
p5 = [('A1','5'),('U1','9'),('U1','16'),('U1','17'),('U2','9'),('U2','15'),('U2','16'),('U2','17'),('U3','8'),('U3','7'),
      *U4('28'),F(3),F(36),('J2','2'),('J3','2'),('C1','1'),('C2','1'),('C3','1'),('C4','1')]
gnd = [('A1','6'),('A1','7'),('A1','29'),('U1','10'),('U1','15'),('U2','10'),('U3','4'),*U4('14'),F(1),F(38),
       ('J2','1'),('J3','1'),('R1','2'),('C1','2'),('C2','2'),('C3','2'),('C4','2')]
same(*p5, net='+5V')
same(*gnd, net='GND')
extra5 = {p for p in nets[where[p5[0]]] if p not in p5 and not p[0].startswith('#')}
extrag = {p for p in nets[where[gnd[0]]] if p not in gnd and not p[0].startswith('#')}
if extra5: errors.append(('extra +5V', sorted(extra5)))
if extrag: errors.append(('extra GND', sorted(extrag)))
# 未接続のはずのピン (MCP の INT、GPB5、カードの A18/A19 など) が他とつながっていないこと
nc = [('U1','19'),('U1','20'),('U2','19'),('U2','20'),('U2','6'),F(34),F(35),('A1','18'),('U3','3')]
if SHIELD: nc += [('U2','3'),('J1','39'),('J1','40')]   # 62256 /OE は使わない、ヘッダの 39/40 は空き
for p in nc:
    if p in where and len(nets[where[p]]) > 1: errors.append(('should be unconnected', p, nets[where[p]]))
print('nets:', len(nets))
print('OK' if not errors else '\n'.join(map(str, errors)))
sys.exit(1 if errors else 0)
