#!/usr/bin/env python3
"""C23 preprocessing regressions, with optional GCC/Clang differential checks.

COAN_EXE selects the binary; CPP selects the comparison compiler (default cc).
The compiler is used only as an independent test oracle, never by coan itself.
"""
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
TOKEN = re.compile(r'''(?:u8|[uUL])?R"([^ ()\\\t\n]*)\(.*?\)\1"|(?:u8|[uUL])?(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|[A-Za-z_\u0080-\uffff][\w\u0080-\uffff]*|(?:[0-9]|\.[0-9])(?:[\w.']|[eEpP][+-])*|%:%:|>>=|<<=|\.\.\.|##|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[+*/%&^|!=-]=|<:|:>|<%|%>|%:|::|[^\s]''', re.S)

def tokens(text):
    return [m.group(0) for m in TOKEN.finditer(text)]

def run(argv, **kw):
    return subprocess.run([str(x) for x in argv], text=True, capture_output=True, timeout=15, **kw)

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
    ('', "'+' == 43 && '(' == 40 && ',' == 44"),
    ('', 'defined(ID) && defined ZERO && !defined(ABSENT)'),
    ('', 'SELF == 0 && LEFT == 0'),
    ('#define FN(x) x\n', 'FN == 0'),
]


def main():
    failures = []
    checks = 0
    compiler = shutil.which(CPP[0])
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
            return run([COAN, command, '-E', '-DCOAN_TEST=1', *args, path])
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
        check('line macro and filename', result.stdout == text and not result.returncode & 12, result.stdout + result.stderr)
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
    print(f'{checks - len(failures)}/{checks} C23/macro checks passed' + ('' if compiler else ' (compiler comparison skipped: CPP unavailable)'))
    return bool(failures)

if __name__ == '__main__':
    raise SystemExit(main())
