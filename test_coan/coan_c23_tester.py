#!/usr/bin/env python3
"""C23 preprocessing regressions, with optional GCC/Clang differential checks.

COAN_EXE selects the binary; CPP selects the comparison compiler (default cc).
The compiler is used only as an independent test oracle, never by coan itself.
"""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(os.environ.get('COAN_PKGDIR', Path(__file__).resolve().parents[1])).resolve()
BUILD = Path(os.environ.get('COAN_BUILDDIR', ROOT)).resolve()
COAN = Path(os.environ.get('COAN_EXE', BUILD / 'src/coan')).resolve()
CPP = shlex.split(os.environ.get('CPP', 'cc'))

# Preserve literal contents while ignoring only inter-token whitespace.
TOKEN = re.compile(r'''(?:u8|[uUL])?R"([^ ()\\\t\n]*)\(.*?\)\1"|(?:u8|[uUL])?(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|[A-Za-z_\u0080-\uffff][\w\u0080-\uffff]*|(?:[0-9]|\.[0-9])(?:[eEpP][+-]|[\w.'])*|%:%:|>>=|<<=|\.\.\.|##|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[+*/%&^|!=-]=|<:|:>|<%|%>|%:|::|[^\s]''', re.S)

def tokens(text):
    return [m.group(0) for m in TOKEN.finditer(text)]

def run(argv, **kw):
    return subprocess.run([str(x) for x in argv], text=True, capture_output=True, timeout=15, **kw)

def compiler_available(cxx=False):
    if not CPP or not shutil.which(CPP[0]):
        return False
    probe = 'COAN_ORACLE_OK\n' if cxx else r'''
#define HAS(...) (0 __VA_OPT__(+ 1))
#if !true || false || 0b1'010 != 10 || 1wb != 1 || 2uwb != 2 || u8'A' != 65
#error C23 literals unavailable
#endif
#if HAS() || !HAS(x)
#error C23 variadics unavailable
#endif
COAN_ORACLE_OK
'''
    try:
        mode = ['-std=c++11', '-x', 'c++'] if cxx else ['-std=c23', '-x', 'c']
        result = run(CPP + ['-E', '-P', *mode, '-'], input=probe)
        return result.returncode == 0 and result.stdout.strip() == 'COAN_ORACLE_OK'
    except (OSError, subprocess.TimeoutExpired):
        return False

MACROS = r'''
#define x 2
#define f(a) f(x * (a))
#define g f
#define z z[0]
#define h g(~
#define m(a) a(w)
#define w 0,1
#define t(a) a
#define PARENS ()
#define EVAL(...) EVAL4(EVAL4(EVAL4(__VA_ARGS__)))
#define EVAL4(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define EVAL2(...) __VA_ARGS__
#define FOR_EACH(m, ...) __VA_OPT__(EVAL(FOR_EACH_HELPER(m,__VA_ARGS__)))
#define FOR_EACH_HELPER(m,a,...) m(a) __VA_OPT__(FOR_EACH_AGAIN PARENS (m,__VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER
#define WRAP(x) [x]
#define A 17
#define B 4
#define EMPTY
#define ZERO() 23
#define ID(x) x
#define APPLY(f, ...) f(__VA_ARGS__)
#define ALIAS ID
#define CAT(a,b) a ## b
#define XCAT(a,b) CAT(a,b)
#define STR(x) #x
#define XSTR(x) STR(x)
#define MIX(x) #x, x, x ## suffix
#define NUMsuffix 33
#define NUM 12
#define VA(...) __VA_ARGS__
#define OPTIONAL(...) start __VA_OPT__(, __VA_ARGS__) end
#define FIXED(a, ...) a __VA_OPT__(+ __VA_ARGS__)
#define HAS(...) (0 __VA_OPT__(+1))
#define VS(...) #__VA_ARGS__
#define VOS(...) #__VA_OPT__(A __VA_ARGS__)
#define VO_CAT(...) prefix ## __VA_OPT__(suffix)
#define VO_CAT2(...) __VA_OPT__(pre) ## fix
#define SELF SELF
#define RECUR(x) RECUR(x + 1)
#define LEFT RIGHT
#define RIGHT LEFT
#define foo(x) bar x
#define LPAREN (
#define PASTE_EMPTY(x,y,z) x ## y ## z
#define PUNCT(x,y) x ## y
#define NOEXPAND(x) 1
#define BAD(x,y) x+y
#define SPLIT 1, 2
#define PAIR(a,b) a+b
#define RESCAN(x) PAIR(x)
#define OPASTE a ## b
#define ab 91
#define DIGRAPH(x,y) x %:%: y
#define DSTRING(x) %: x
#define HIDDEN(x) x
#define MARKER(...) __VA_OPT__()
#define H3(X,...) #__VA_OPT__(X##X X##X)
#define H4(X,...) __VA_OPT__(a X ## X) ## b
#define H5A(...) __VA_OPT__()/**/__VA_OPT__()
#define H5B(X) X
#define VEMPTY(...) x ## __VA_OPT__() ## y
#define VEMPTY2(...) __VA_OPT__(EMPTY) ## y
#define VQUOTE(...) #__VA_OPT__(EMPTY)
#define HASHHASH # ## #
#define LITERAL_PASTE(x) x
'''

EXPRESSIONS = [
    'f(y+1) + f(f(z))', 'g(x + (3,4) - w) | h 5) & m(f) ^ m(m)',
    't(t(g)(0) + t)(1)',
    'FOR_EACH(WRAP)', 'FOR_EACH(WRAP,a,b,c,d,e)', 'ZERO()', 'ID()', 'ID(ID(3))', 'APPLY(ID, A)', 'APPLY(ZERO)',
    'ALIAS(A)', 'ID(ALIAS)(B)', 'ID(ID)(3)', 'foo(foo) (2)',
    'foo(foo(2))', 'ID(SELF)', 'LEFT RIGHT', 'RECUR(1)',
    'CAT(A,B)', 'XCAT(A,B)', 'CAT(,A)', 'CAT(A,)', 'CAT(,)',
    'PASTE_EMPTY(a,,b)', 'PASTE_EMPTY(,,)', 'PUNCT(+,=)',
    'PUNCT(.,5)', 'PUNCT(1,e3)', 'PUNCT(L,"a")',
    'STR(A)', 'XSTR(A)', 'MIX(A)', 'MIX(NUM)', 'MIX()',
    'STR(a +  b)', 'STR(a/**/b)', 'STR(a/**/+b)',
    'STR("a  b,(c),\\\"d\\\\e")', "STR('(')",
    'ID("a,b)(c")', 'ID((1,2))',
    'VA()', 'VA(,)', 'VA(A,B,)', 'VA((1,2), A)',
    'OPTIONAL()', 'OPTIONAL(EMPTY)', 'OPTIONAL(A)', 'OPTIONAL(A,B)',
    'OPTIONAL(,)', 'FIXED(5)', 'FIXED(5,)', 'FIXED(5,EMPTY)', 'FIXED(5,A)',
    'HAS()', 'HAS(EMPTY)', 'HAS(EMPTY EMPTY)', 'HAS(EMPTY,EMPTY)',
    'VS()', 'VS(A, B)', 'VS(A , B)', 'VS( , )', 'VOS()', 'VOS(EMPTY)', 'VOS(B)',
    'VO_CAT()', 'VO_CAT(x)', 'VO_CAT2()', 'VO_CAT2(x)',
    'NOEXPAND(BAD(1))', 'STR(BAD(1))', 'RESCAN(SPLIT)',
    'ID(EMPTY) ZERO ()', 'ID(LPAREN) A )', 'OPASTE',
    'DIGRAPH(a,b)', 'DSTRING(A)', 'VA(HASHHASH)',
    'LITERAL_PASTE(HASHHASH)', 'PUNCT(<,<)', 'PUNCT(%,:)',
    'ID(1e+A)', 'STR(1e+A)', 'STR(0x1p+B)', "STR(1'000)",
    'ID(+)+', 'ID(/)*', 'XSTR(ID(.)..)', 'ID(L)"x"',
    'HIDDEN(MARKER(x))', 'H3(,0)', 'H3(A,1)', 'H4(,0)', 'H4(A,1)', 'H5A()', 'H5A(x)', 'H5B(H5A(x))', 'VEMPTY()', 'VEMPTY(a)', 'VEMPTY2(a)', 'VEMPTY2()', 'VQUOTE(x)',
]

CONDITIONS = [
    ('#define SUM 1 + 2\n', '3 * SUM == 5'),
    ('#define OPEN (\n#define CLOSE )\n', 'OPEN 3 + 4 CLOSE == 7'),
    ('', 'ZERO() == 23 && ID(ID(A)) == 17'),
    ('', 'FIXED(2) == 2 && FIXED(2,3) == 5'),
    ('', '!HAS() && !HAS(EMPTY) && HAS(,)'),
    ('', 'true && !false'),
    ('', "0b1010 == 10 && 0B1'010 == 10 && 1'000 == 1000"),
    ('', "0xA'B == 171 && 0'77 == 63"),
    ('', '1wb == 1 && 2WB == 2 && 3uwb == 3 && 4WBU == 4'),
    ('', "u8'A' == 65 && u'a' == 97 && U'+' == 43"),
    ('', r"u8'\xFF' == 255 && u8'\377' == 255 && u8'\200' == 128"),
    ('', "PAIR(u8'a',u8'b') == 195"),
    ('', "'+' == 43 && '(' == 40 && ',' == 44"),
    ('', 'defined(ID) && defined ZERO && !defined(ABSENT)'),
    ('', 'SELF == 0 && LEFT == 0'),
    ('#define FN(x) x\n', 'FN == 0'),
]


def review_regressions(coan, check, compiler, cxx_compiler):
    # Keep explicit regressions for the inline findings and the additional
    # findings recorded only in the PR review summary.
    text = '#define A (A + X) && 1\n#if A\nYES\n#else\nNO\n#endif\n'
    result = coan(text)
    check('retain partially simplified recursive expansion', result.stdout == text,
          result.stdout + result.stderr)
    if compiler:
        for x in (-1, 0, 1):
            before = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-DA=0', f'-DX={x}', '-'], input=text)
            after = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-DA=0', f'-DX={x}', '-'], input=result.stdout)
            check(f'partial expansion preserves branch with X={x}',
                  before.returncode == after.returncode == 0 and before.stdout == after.stdout,
                  before.stdout + after.stdout + after.stderr)

    text = '#define ARG ) || 1 || (\n#if U(ARG 0)\nYES\n#else\nNO\n#endif\n'
    result = coan(text)
    check('unconfigured call keeps arguments opaque', result.stdout == text and not result.returncode & 12,
          result.stdout + result.stderr)
    if compiler:
        oracle = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-DU(x)=0', '-'], input=result.stdout)
        check('external macro may discard opaque arguments', oracle.returncode == 0 and oracle.stdout.strip() == 'NO',
              oracle.stdout + oracle.stderr)

    for query in ('__has_include', '__has_include_next', '__has_embed'):
        for header in ('<foo/bar.h>', '<dir/foo(bar.h>', '<dir/foo)bar.h>', '<dir/a,b.h>'):
            call = query + '(' + header + ')'
            text = '#define ONE 1\n#if ONE && ' + call + '\nYES\n#endif\n'
            result = coan(text)
            check('retain header adjacency ' + call, call in result.stdout and not result.returncode & 12,
                  result.stdout + result.stderr)
            result = coan(text, 'symbols', ['--ifs', '--expand'])
            check('report opaque header ' + call, call in result.stdout and not result.returncode & 12,
                  result.stdout + result.stderr)
            result = coan(text, args=['-U' + query])
            check('consume disabled header query ' + call,
                  '#if' not in result.stdout and 'YES' not in result.stdout and not result.returncode & 12,
                  result.stdout + result.stderr)
        result = coan('#if ' + query + '(x\nYES\n#endif\n', args=['-U' + query, '--gag=error'])
        check('gagged malformed query ' + query, result.returncode >= 0 and
              'runtime error:' not in result.stderr and 'AddressSanitizer' not in result.stderr,
              result.stdout + result.stderr)

    for newline in ('\n', '\r\n'):
        for expression in ("1\\" + newline + "'000", "1'\\" + newline + '000',
                           "0\\" + newline + "'1750", "0'\\" + newline + '1750',
                           "0x3'\\" + newline + 'e8', "0b11'\\" + newline + '11101000'):
            result = coan('#if ' + expression + ' == 1000\nYES\n#endif\n')
            check('spliced separator ' + repr(expression), result.stdout == 'YES\n' and not result.returncode & 12,
                  result.stdout + result.stderr)
            result = coan('#define F(x,y) x+y\n#if F(' + expression + ",2'000) == 3000\nYES\n#endif\n",
                          'symbols', ['--ifs', '--expand'])
            check('spliced argument boundaries ' + repr(expression), 'evaluates to 3000' in result.stdout and
                  not result.returncode & 12, result.stdout + result.stderr)

    definitions = '#define CAT(a,b) a##b\n#define DROP(x) 1\n#define PASS(x) DROP(x)\n'
    for expression in (r'CAT(1,\u00E9)', r'CAT(1,\U000000E9)'):
        text = definitions + '#if PASS(' + expression + ')\nYES\n#endif\n'
        result = coan(text)
        check('UCN preprocessing number ' + expression, result.stdout.endswith('YES\n') and
              not result.returncode & 12, result.stdout + result.stderr)
        if compiler:
            oracle = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-'], input=text)
            check('compiler UCN preprocessing number ' + expression,
                  oracle.returncode == 0 and oracle.stdout.strip() == 'YES', oracle.stderr)
    for literal in ('"x"', "'x'", 'R"(x)"', 'u8R"(x)"'):
        text = definitions + '#if PASS(CAT(' + literal + ',_tag))\nYES\n#endif\n'
        result = coan(text)
        check('paste literal suffix ' + literal, result.stdout.endswith('YES\n') and not result.returncode & 12,
              result.stdout + result.stderr)
        if cxx_compiler:
            oracle = run(CPP + ['-E', '-P', '-std=c++11', '-x', 'c++', '-'], input=text)
            check('compiler literal suffix ' + literal, oracle.returncode == 0 and oracle.stdout.strip() == 'YES', oracle.stderr)
        for expression, expected in ((literal + '_tag', literal + '_tag'),
                                     ('ID(' + literal + ')_tag', literal + '99')):
            result = coan('#define _tag 99\n#define ID(x) x\n#define RESULT ' + expression + '\n',
                          'symbols', ['--expand', '--select=RESULT'])
            check('preserve suffix token boundary ' + expression, '>>' + expected + '<<' in result.stdout and
                  not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#define ID(x) x\n#define RESULT ID(' + literal + ')_tag\n',
                      'symbols', ['--expand', '--select=RESULT'])
        check('insert separator before unexpanded suffix ' + literal,
              '>>' + literal + ' _tag<<' in result.stdout and not result.returncode & 12,
              result.stdout + result.stderr)

    for flags in (['--expand'], ['--explain']):
        result = coan('#define A defined(A)\n#if A\nYES\n#endif\n', 'symbols', flags)
        check('defined operand avoids recursive reporting ' + str(flags), 'evaluates to 1' in result.stdout and
              not result.returncode & 12, result.stdout + result.stderr)
        text = '#define OPEN (\n#define CLOSE )\n#if OPEN 3 + 4 CLOSE == 7\nYES\n#endif\n'
        result = coan(text, 'symbols', flags)
        check('fragment diagnostics stay isolated ' + str(flags), 'OPEN:' in result.stdout and 'CLOSE:' in result.stdout and
              not result.returncode & 12, result.stdout + result.stderr)
        result = coan(text.replace('3 + 4', '3 +'), 'symbols', flags)
        check('whole expression still diagnoses errors ' + str(flags), bool(result.returncode & 4), result.stderr)
    result = coan('#define F(x) +##x\n#define OUT F(x)\n', 'symbols', ['--expand', '--select=F'])
    check('invalid preview cache cannot hide real paste error', bool(result.returncode & 4) and
          'does not form a preprocessing token' in result.stderr, result.stdout + result.stderr)
    result = coan('#define F(x) +##x\n#if 0\n#include F(x)\n#endif\n', 'symbols', ['--expand', '--select=F'])
    check('invalid preview cache cannot substitute for actual include arguments', bool(result.returncode & 4) and
          'does not form a preprocessing token' in result.stderr, result.stdout + result.stderr)
    result = coan('#define F(x) x+x\n#define OUT F(x)\n', 'symbols', ['--expand', '--select=F'])
    check('valid preview cache remains reusable', 'expands unchanged' in result.stdout and
          not result.returncode & 12, result.stdout + result.stderr)

    for command, flags in (('symbols', ['--includes', '--expand']), ('includes', ['--local'])):
        result = coan('#define H "long.h"\n#include H\n', command, [*flags, '--max-expansion=2'])
        check('include expansion warning once ' + command,
              result.stderr.count('#include macro expansion exceeds resource limit') == 1,
              result.stdout + result.stderr)
    text = '#define STR(x) #x\n#define DROP(x) "h"\n#define BAD(x,y) x+y\n#include STR(HEADER)\n#include DROP(BAD(1))\n'
    result = coan(text, 'symbols', ['--includes', '--expand', '--select=HEADER,BAD'])
    check('include discovers quoted and discarded references', 'HEADER:' in result.stdout and
          'BAD(1):' in result.stdout and 'not expanded' in result.stdout and not result.returncode & 12,
          result.stdout + result.stderr)
    result = coan('#define H "a.h"\n#include H\n#undef H\n#define H "b.h"\n#include H\n', 'includes')
    check('include cache is per directive', '>>"a.h"<<' in result.stdout and '>>"b.h"<<' in result.stdout,
          result.stdout + result.stderr)
    result = coan('#define P +\n#define RESULT P+\n', 'symbols', ['--expand', '--select=RESULT', '--max-expansion=2'])
    check('expansion limit includes inserted separators', 'macro expansion too large' in result.stdout,
          result.stdout + result.stderr)
    text = '#define H (' + ' + '.join(['1'] * 32) + ')\n#if H\nYES\n#endif\n#if !H && OTHER && defined(K)\nNO\n#endif\n'
    result = coan(text, 'symbols', ['--ifs', '--expand', '--max-expansion=16'])
    check('expansion limit retains all source reports', result.stdout.count('H:') == 2 and
          result.stdout.count('macro expansion too large') == 2 and 'OTHER:' in result.stdout and 'K:' in result.stdout,
          result.stdout + result.stderr)

    text = '#define A B\n#define B A\n#if B\nYES\n#endif\n#undef A\n#if B\nNO\n#endif\n'
    result = coan(text, 'symbols', ['--expand', '--verbose'])
    check('cycle retains reverse dirty notification',
          'Undefining "A" retrospectively affects the meaning of "B"' in result.stderr and not result.returncode & 12,
          result.stdout + result.stderr)
    result = coan(text.replace('#undef A', '#undef B\n#define B 1\n#undef A'), 'symbols', ['--expand', '--verbose'])
    check('cycle notification edges are removed on redefine',
          'Undefining "A" retrospectively affects the meaning of "B"' not in result.stderr and
          'B: transient: expands afresh as >>1<<' in result.stdout and not result.returncode & 12,
          result.stdout + result.stderr)

    # Dependencies defined later must not close a snapshot/reporting cycle.
    # An already-seen guard in subscribe_to alone leaves these walks cyclic.
    for names in (('A', 'B', 'C'), ('A', 'B', 'C', 'E')):
        definitions = dict(zip(names, names[1:] + names[:1]))
        for order in (names, names[::-1]):
            bindings = [(name, definitions[name]) for name in order] + [('D', 'A')]
            condition = '#if D\nBAD_CYCLE\n#else\nCYCLE_ZERO\n#endif\n'
            for global_definitions in (False, True):
                label = str(order) + (' global' if global_definitions else ' local')
                args = ['-D' + name + '=' + value for name, value in bindings] if global_definitions else []
                text = '' if global_definitions else ''.join(
                    '#define ' + name + ' ' + value + '\n' for name, value in bindings)
                text += condition
                if not global_definitions:
                    text += '#undef C\n#define C 1\n#if D\nCYCLE_ONE\n#else\nBAD_REDEFINED\n#endif\n'
                result = coan(text, args=args)
                check('delayed dependency cycle source ' + label,
                      result.returncode >= 0 and not result.returncode & 12 and
                      'CYCLE_ZERO\n' in result.stdout and 'BAD_' not in result.stdout and
                      (global_definitions or 'CYCLE_ONE\n' in result.stdout), result.stdout + result.stderr)
                if compiler:
                    before = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', *args, '-'], input=text)
                    after = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', *args, '-'], input=result.stdout)
                    check('compiler delayed dependency cycle ' + label,
                          before.returncode == after.returncode == 0 and before.stdout == after.stdout,
                          before.stdout + after.stdout + after.stderr)
                for flag in ('--expand', '--explain'):
                    result = coan(text, 'symbols', [*args, flag, '--select=D'])
                    check('delayed dependency cycle reporting ' + label + ' ' + flag,
                          result.returncode >= 0 and not result.returncode & 12 and
                          re.search(r'^D:.*evaluates to 0', result.stdout, re.M) and
                          (global_definitions or re.search(r'^D:.*evaluates to 1', result.stdout, re.M)),
                          result.stdout + result.stderr)

    for selector, text in (
            ('--must', '#if UNKNOWN\n#warning X\n#endif\n#warning X\n'),
            ('--cant', '#warning X\n#if 0\n#warning X\n#endif\n')):
        result = coan(text, 'directives', [selector, '--once-only'])
        check('warning once honors selector ' + selector, result.stdout.count('#warning X') == 1,
              result.stdout + result.stderr)
    text = '#if 0\n#define X 1\n#undef Y\n#include "x.h"\n#line 10\n#warning hi\n#unknown hi\nDEAD\n#endif\n'
    result = coan(text, args=['--cant'])
    check('cant drops unreachable directives', result.stdout == 'DEAD\n', result.stdout + result.stderr)


def main():
    failures = []
    checks = 0
    compiler = compiler_available()
    cxx_compiler = compiler and compiler_available(cxx=True)
    with tempfile.TemporaryDirectory(prefix='coan-c23-') as directory:
        path = Path(directory) / 'case.c'
        def check(name, ok, detail=''):
            nonlocal checks
            checks += 1
            if not ok:
                failures.append(name)
                print(f'FAIL {name}\n{detail[:2500]}')
        def coan(text, command='source', args=()):
            path.write_text(text)
            result = run([COAN, command, '-E', '-DCOAN_TEST=1', *args, path])
            # These fixtures never set a user #line to their temporary input
            # pathname. Ignore generated anchors to that exact path for their
            # spelling checks; source_tester checks verbatim output locations.
            if command == 'source':
                result.stdout = re.sub(r'^#line [0-9]+ ' +
                    re.escape(json.dumps(str(path))) + r'\n', '', result.stdout, flags=re.M)
            return result
        review_regressions(coan, check, compiler, cxx_compiler)
        check('oracle pp-number boundaries',
              tokens('1e+A 1e + A 0x1p-B') == ['1e+A', '1e', '+', 'A', '0x1p-B'])
        for expression in EXPRESSIONS:
            result = coan(MACROS + '\n#define COAN_RESULT ' + expression + '\n',
                          'symbols', ['--expand', '--select=COAN_RESULT'])
            match = re.search(r'^COAN_RESULT:.*?as >>(.*)<<', result.stdout, re.M)
            check('expand ' + expression, match is not None and not result.returncode & 12,
                  result.stdout + result.stderr)
            if compiler and match:
                oracle = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-'],
                             input=MACROS + '\n' + expression + '\n')
                check('compiler ' + expression,
                      oracle.returncode == 0 and tokens(match[1]) == tokens(oracle.stdout),
                      f'coan: {match[1]}\ncompiler: {oracle.stdout}\n{oracle.stderr}')
        result = coan(MACROS + '\n#define COAN_RESULT ID(.)..\n', 'symbols', ['--expand', '--select=COAN_RESULT'])
        check('three distinct dots', '>>. . .<<' in result.stdout, result.stdout + result.stderr)
        for extra, expression in CONDITIONS:
            text = MACROS + extra + '\n#if ' + expression + '\nPASS\n#else\nFAIL\n#endif\n'
            result = coan(text, args=['--implicit'])
            check('condition ' + expression,
                  not result.returncode & 12 and '\nPASS\n' in result.stdout and '\nFAIL\n' not in result.stdout,
                  result.stdout[-1500:] + result.stderr[-1000:])
            if compiler:
                oracle = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-'], input=text)
                check('compiler condition ' + expression,
                      oracle.returncode == 0 and oracle.stdout.strip() == 'PASS', oracle.stderr)
        for expression in ('1', 'COAN_TEST', '0\nNO\n#elif COAN_TEST'):
            result = coan('#if 0\nNO\n#elif ' + expression + '\nYES\n#else\nNO\n#endif\n')
            check('eligible elif ' + expression, result.stdout == 'YES\n' and not result.returncode & 12,
                  result.stdout + result.stderr)
        for prefix in ('1', "1'000", "u8'a'", '"a"'):
            text = 'int n = ' + prefix + '/*\n#if FLAG\n*/;\n'
            result = coan(text)
            check('comment after ' + prefix, result.stdout == text and not result.returncode & 12,
                  result.stdout + result.stderr)
        for prefix in ('R', 'LR', 'u8R'):
            literal = prefix + '"tag(a\\\nb) x)tag"'
            quoted = '"' + literal.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
            for expression, expected in ((literal, literal), ('ID(' + literal + ')', literal),
                                         ('STR(' + literal + ')', quoted)):
                definitions = '#define ID(x) x\n#define STR(x) #x\n'
                text = definitions + '#define COAN_RESULT ' + expression + '\n'
                result = coan(text, 'symbols', ['--expand', '--select=COAN_RESULT'])
                check('raw literal ' + expression, '>>' + expected + '<<' in result.stdout and not result.returncode & 12,
                      result.stdout + result.stderr)
                if cxx_compiler:
                    oracle = run(CPP + ['-E', '-P', '-std=c++11', '-x', 'c++', '-'],
                                 input=definitions + expression + '\n')
                    check('compiler raw literal ' + expression,
                          oracle.returncode == 0 and tokens(oracle.stdout) == tokens(expected), oracle.stderr)
            text = 'auto s = ' + prefix + '"(a\\\n#if FLAG\nb)";\n'
            result = coan(text)
            check('raw ordinary source ' + prefix, result.stdout == text and not result.returncode & 12,
                  result.stdout + result.stderr)
        text = '#define A 1 + F\n#define F(x) x\n#if A(UNKNOWN) && 1\nYES\n#endif\n'
        result = coan(text)
        check('overlapping source spans', result.stdout == text and not result.returncode & 12,
              result.stdout + result.stderr)
        text = '#define CAT(a,b) a##b\n#define XCAT(a,b) CAT(a,b)\n#define AB 7\n#if XCAT(A,B) == 7\nYES\n#endif\n'
        result = coan(text, args=['-UA'])
        check('undefined argument before paste', result.stdout.endswith('YES\n') and '#if' not in result.stdout,
              result.stdout + result.stderr)
        for query in ('__has_include("x.h")', '__has_embed("x.bin" limit(3))', '__has_c_attribute(gnu::unused)'):
            result = coan('#if ' + query + '\nNO\n#else\nYES\n#endif\n', args=['-U' + query.split('(')[0]])
            check('disabled query ' + query, result.stdout == 'YES\n' and not result.returncode & 12,
                  result.stdout + result.stderr)
        text = '#define F(x) 1\n#define ALIAS F\n#define BAD(x,y) x+y\n#if ALIAS(BAD(1))\nYES\n#endif\n'
        for extra in ([], ['--expand'], ['--explain']):
            result = coan(text, 'symbols', ['--ifs', *extra])
            check('discarded call reporting ' + str(extra), 'BAD(1):' in result.stdout and not result.returncode & 12,
                  result.stdout + result.stderr)
        text = '#define F(x) 1\n#if F(BAR)\nYES\n#endif\n'
        result = coan(text, 'symbols', ['--ifs', '--select=BAR'])
        check('discarded argument discovery', 'BAR:' in result.stdout and not result.returncode & 12,
              result.stdout + result.stderr)
        text = '#define S(...) #__VA_ARGS__\n#include S(a , b)\n#include S(a,b)\n#include S(a , b)\n'
        result = coan(text, 'symbols', ['--includes', '--expand', '--select=S'])
        check('stringified comma whitespace and cache', result.stdout.count('>>"a , b"<<') == 2 and
              result.stdout.count('>>"a,b"<<') == 1 and not result.returncode & 12, result.stdout + result.stderr)
        result = coan("#define F(x,y) x+y\n#if F(u8'a',u8'b') == 195\nYES\n#endif\n",
                      'symbols', ['--ifs', '--expand'])
        check('UTF-8 argument reporting', 'evaluates to 195' in result.stdout and not result.returncode & 12,
              result.stdout + result.stderr)
        result = coan("#define F(x,y) x+y\n#if F(u\\\n8'a',u8'b') == 195\nYES\n#endif\n",
                      'symbols', ['--ifs', '--expand'])
        check('spliced UTF-8 argument reporting', 'evaluates to 195' in result.stdout and not result.returncode & 12,
              result.stdout + result.stderr)
        for literal in (r"u8'\u00ff'", "u8'é'", r"u8'\x100'", r"u8'\0377'"):
            text = '#if ' + literal + ' == 255\nYES\n#endif\n'
            result = coan(text)
            check('UTF-8 unit overflow ' + literal, result.stdout == text and result.returncode & 2,
                  result.stdout + result.stderr)
        text = '#define F(x) 1\n#define S(...) #__VA_ARGS__\n#if F(S(a , b))\nYES\n#endif\n#include S(a , b)\n'
        result = coan(text, 'symbols', ['--expand', '--select=S'])
        check('discovered reference is later expanded', 'not expanded' in result.stdout and '>>"a , b"<<' in result.stdout
              and not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#if 0\nNO\n#elifdef UNKNOWN\nYES\n#elifndef OTHER\nMAYBE\n#endif\n')
        check('elifdef promotion', '#ifdef UNKNOWN\nYES\n#elifndef OTHER' in result.stdout, result.stdout)
        result = coan('#if 0\nNO\n#elifndef UNKNOWN\nYES\n#endif\n')
        check('elifndef promotion', '#ifndef UNKNOWN\nYES' in result.stdout, result.stdout)
        result = coan('#if 0\nNO\n#elifdef COAN_TEST\nYES\n#else\nNO\n#endif\n')
        check('elifdef known', result.stdout == 'YES\n', result.stdout + result.stderr)
        result = coan('#if UNKNOWN\nMAYBE\n#elifndef ABSENT\nYES\n#else\nNO\n#endif\n', args=['-UABSENT'])
        check('elifndef becomes else', result.stdout == '#if UNKNOWN\nMAYBE\n#else\nYES\n#endif\n', result.stdout)
        for query in ('__has_include(<some/file.h>)', '__has_embed("asset.bin" limit(3))', '__has_c_attribute(gnu::unused)'):
            text = '#if ' + query + '\nYES\n#else\nNO\n#endif\n'
            result = coan(text, args=['--implicit'])
            check('preserve ' + query, result.stdout == text and not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#if __has_include("thing.h")\nYES\n#endif\n', 'symbols', ['--expand', '--implicit'])
        check('report opaque feature query', '__has_include' in result.stdout and not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#if __has_include("thing.h")\nYES\n#endif\n', args=['-D__has_include(x)=1'])
        check('configured feature query', result.stdout == 'YES\n', result.stdout + result.stderr)
        result = coan('#define F(x) +##x\n#define RESULT F(+)\n', 'symbols', ['--expand'])
        check('formal argument preview is not a real call', 'without actual macro arguments' in result.stdout and '>>++<<' in result.stdout and not result.returncode & 12, result.stdout + result.stderr)
        text = '#define LINE(n) n "file.c"\n#line LINE(42)\n#line 9 "other.c"\n'
        result = coan(text)
        expected = text.replace('#line LINE(42)', '#line 42 "file.c"')
        anchored = expected.replace('#line 9 "other.c"', '#line 42 "file.c"\n#line 9 "other.c"')
        check('line macro and filename', result.stdout in (expected, anchored) and
              not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#define F(x,y) x+y\n#if COAN_TEST\nYES\n#elif F(1)\nNO\n#endif\n')
        check('skip already selected invalid call', result.stdout.endswith('YES\n') and not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#if COAN_TEST\nYES\n#elifdef THIS IS NOT VALID\nNO\n#endif\n')
        check('skip already selected elifdef', result.stdout == 'YES\n' and not result.returncode & 12, result.stdout + result.stderr)
        result = coan('#define QUOTE(x) #x\n#define HEADER QUOTE\n#include HEADER(file.h)\n', 'includes')
        check('include alias and invocation', 'expands as >>"file.h"<<' in result.stdout and not result.returncode & 12, result.stdout + result.stderr)
        text = '#warning hello\n#embed "asset.bin" prefix(1,) suffix(,2)\n'
        result = coan(text)
        check('passive C23 directives', result.stdout == text and not result.returncode & 12, result.stdout + result.stderr)
        result = coan(text, 'directives')
        check('report C23 directives', result.stdout == text, result.stdout)
        text = '#define CAT(a,b) a##b\n#define WRAP CAT(V,ALUE)\n#define VALUE 1\n#if WRAP == 1\nONE\n#endif\n#undef VALUE\n#define VALUE 2\n#if WRAP == 2\nTWO\n#endif\n'
        result = coan(text)
        check('pasted dependency invalidation', '\nONE\n' in result.stdout and '\nTWO\n' in result.stdout and '#if' not in result.stdout, result.stdout + result.stderr)
        text = '#define COAN_RESULT "' + 'x' * 5000 + '"\n'
        result = coan(text, 'symbols', ['--expand', '--select=COAN_RESULT'])
        check('expansion budget', 'macro expansion too large' in result.stdout, result.stdout)
        result = coan(text, 'symbols', ['--expand', '--select=COAN_RESULT', '--max-expansion=8K'])
        check('configured expansion budget', 'x' * 5000 in result.stdout and not result.returncode & 12, result.stderr)
        for definition in ('F(a,a) a', 'F(a,) a', 'F(...,a) a', 'F() #x',
                           'F(x) ##x', 'F(x) x##', 'F(x) __VA_ARGS__',
                           'F(...) __VA_OPT__ x', 'F(...) __VA_OPT__((x)',
                           'F(...) __VA_OPT__(__VA_OPT__(x))',
                           'F(...) __VA_OPT__(##x)'):
            result = coan('#define ' + definition + '\n')
            check('reject ' + definition, bool(result.returncode & 4), result.stdout + result.stderr)
        for expression in ('ID(1,2)', 'CAT(1)', 'PUNCT(+,*)', 'RECUR(1)'):
            result = coan(MACROS + '\n#if ' + expression + '\nYES\n#endif\n')
            check('reject call ' + expression, bool(result.returncode & 4), result.stdout + result.stderr)
    print(f'{checks - len(failures)}/{checks} C23/macro checks passed' +
          ('' if compiler else ' (compiler comparisons skipped: CPP unavailable or lacks required C23 support)'))
    return bool(failures)

if __name__ == '__main__':
    raise SystemExit(main())
