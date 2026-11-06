#!/usr/bin/env python3
"""Arduino IDE と同じ手順でスケッチを 1 つの .cpp にまとめる。

  1. メインの .ino を先頭に、残りの .ino をアルファベット順に連結
  2. 先頭に #include <Arduino.h>
  3. 最初の関数定義の直前に、すべての関数のプロトタイプを挿入
     (#if で囲まれた関数は、同じ条件で囲んだプロトタイプにする。Arduino IDE はプリプロセス後の
      ソースからプロトタイプを作るので、#if で外れた関数のプロトタイプは作られない)
  4. エラー位置が元のファイル名・行番号で出るよう #line を入れる
"""
import os, re, sys

def strip_code(src):
    """コメントと文字列を空白に置き換えた同じ長さの文字列を返す"""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', src[i:j])); i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + ' ' * (j - i - 2) + c if j - i >= 2 else c); i = j
        else:
            out.append(c); i += 1
    return ''.join(out)

KEYWORDS = {'if', 'for', 'while', 'switch', 'else', 'return', 'struct', 'class', 'typedef', 'enum', 'namespace', 'do', 'union'}
FUNC = re.compile(r'^((?:static\s+|inline\s+|const\s+|unsigned\s+|signed\s+)*[A-Za-z_][\w:]*[\s\*&]+[\*&]*)([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{', re.M | re.S)

COND = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b.*$', re.M)

def cond_stack_at(code, pos):
    """pos の位置で有効な条件ディレクティブの積み重ね (各段は ['#if X', '#else' ...] の並び)"""
    stack = []
    for m in COND.finditer(code):
        if m.start() >= pos:
            break
        kind, line = m.group(1), m.group(0).strip()
        if kind in ('if', 'ifdef', 'ifndef'):
            stack.append([line])
        elif kind in ('elif', 'else') and stack:
            stack[-1].append(line)
        elif kind == 'endif' and stack:
            stack.pop()
    return stack

def wrap(proto, stack):
    if not stack:
        return proto
    head = '\n'.join(l for level in stack for l in level)
    return head + '\n' + proto + '\n' + '\n'.join('#endif' for _ in stack)

def prototypes(src):
    code = strip_code(src)
    protos = []
    depth = 0
    pos = 0
    # 深さ 0 の位置にある関数定義だけを拾う
    for m in FUNC.finditer(code):
        # m.start() までの深さを数える
        depth = code[:m.start()].count('{') - code[:m.start()].count('}')
        if depth != 0:
            continue
        if m.start() > 0 and code[m.start() - 1] not in '\n':
            continue
        head = m.group(1).strip()
        first = head.split()[0] if head.split() else ''
        if first in KEYWORDS or m.group(2) in KEYWORDS:
            continue
        proto = '%s %s(%s);' % (head, m.group(2), re.sub(r'\s+', ' ', m.group(3).strip()))
        protos.append(wrap(proto, cond_stack_at(code, m.start())))
    return protos

def first_function_offset(src):
    code = strip_code(src)
    for m in FUNC.finditer(code):
        depth = code[:m.start()].count('{') - code[:m.start()].count('}')
        if depth == 0 and (m.start() == 0 or code[m.start() - 1] == '\n'):
            head = m.group(1).strip()
            if head.split()[0] in KEYWORDS or m.group(2) in KEYWORDS:
                continue
            return m.start()
    return None

def main():
    d = sys.argv[1]
    base = os.path.basename(os.path.abspath(d))
    main_ino = base + '.ino'
    names = sorted(f for f in os.listdir(d) if f.endswith('.ino') and f != main_ino)
    files = [main_ino] + names
    parts = []
    for f in files:
        with open(os.path.join(d, f), encoding='utf-8') as fh:
            parts.append((f, fh.read()))
    all_src = ''.join(p[1] + '\n' for p in parts)
    protos = []
    for f, s in parts:
        protos += prototypes(s)
    # 最初の関数 (たいていは別のファイルのどこか) の直前に入れる
    out = ['#include <Arduino.h>\n']
    inserted = False
    for f, s in parts:
        off = first_function_offset(s)
        if off is not None and not inserted:
            out.append('#line 1 "%s"\n' % f)
            out.append(s[:off])
            out.append('\n// ---- auto-generated prototypes ----\n' + '\n'.join(protos) + '\n// -----------------------------------\n')
            line = s[:off].count('\n') + 1
            out.append('#line %d "%s"\n' % (line, f))
            out.append(s[off:] + '\n')
            inserted = True
        else:
            out.append('#line 1 "%s"\n' % f)
            out.append(s + '\n')
    sys.stdout.write(''.join(out))

main()
