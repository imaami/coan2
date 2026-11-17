#!/usr/bin/env python3
"""Project-header and ordinary-source preprocessing regressions.

COAN_EXE selects coan; CPP optionally selects an independent C preprocessor.
Compare coan's emitted tokens directly: preprocessing its output alone would
hide a failure to expand ordinary source in the first place.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from coan_c23_tester import COAN, CPP, run, tokens


def ordinary_tokens(text):
    """Ignore retained directive lines, including continued definitions."""
    ordinary = []
    continuation = False
    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        directive = stripped.startswith('#') or (stripped.startswith('%:') and
                                                 not stripped.startswith('%:%:'))
        if continuation or directive:
            continuation = line.rstrip('\r\n').endswith('\\')
        else:
            ordinary.append(line)
    return tokens(''.join(ordinary))


def root_preprocessing_tokens(text, path):
    """Read the compiler's include nesting, retaining only the root's tokens.

    A user #line can change the presumed filename without entering a header,
    so filename equality alone cannot distinguish root code from header code.
    GCC and Clang mark include entry/return with flags 1 and 2 respectively.
    """
    ordinary = []
    depth = 0
    started = False
    marker = re.compile(r'^#\s+[0-9]+\s+("(?:\\.|[^"\\])*")(.*)$')
    for line in text.splitlines(keepends=True):
        match = marker.match(line)
        if match:
            filename = json.loads(match.group(1))
            flags = match.group(2).split()
            if '1' in flags:
                depth += 1
            elif '2' in flags:
                depth -= 1
            if filename == str(path) and not started:
                started = True
                depth = 0
        elif started and depth == 0:
            ordinary.append(line)
    return ordinary_tokens(''.join(ordinary))


def compiler_available():
    if not CPP or not shutil.which(CPP[0]):
        return False
    try:
        result = run(CPP + ['-E', '-P', '-std=c23', '-x', 'c', '-'],
                     input='#define OPTIONAL(...) __VA_OPT__(ready)\nOPTIONAL(x)\n')
        return result.returncode == 0 and result.stdout.strip() == 'ready'
    except (OSError, subprocess.TimeoutExpired):
        return False


def main():
    checks = 0
    failures = []
    compiler = compiler_available()

    def check(name, success, detail=''):
        nonlocal checks
        checks += 1
        if not success:
            failures.append(name)
            print(f'FAIL {name}\n{detail[:3500]}')

    with tempfile.TemporaryDirectory(prefix='coan-source-') as temporary:
        root = Path(temporary)

        def fixture(name, files):
            directory = root / name
            directory.mkdir()
            for relative, contents in files.items():
                path = directory / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            return directory

        def coan(path, args=()):
            return run([COAN, 'source', '-E', '-DCOAN_TEST=1', *args, path], cwd=root)

        def preprocess(path, args=(), root_only=False):
            return run(CPP + ['-E', *([] if root_only else ['-P']),
                       '-std=c23', '-x', 'c', *args, path], cwd=root)

        def rewritten_file(path, output):
            # Coan must preserve the input identity itself. Adding a location
            # prefix here would mask broken __FILE__/__LINE__ in its output.
            generated = path.with_name('generated.c')
            generated.write_text(output)
            return generated

        def compilable(result):
            # Default output is an expanded view of the selected file. These
            # explicit options retain context needed to preprocess unresolved
            # choices and preserve their original presumed source locations.
            return run([*result.args[:-1], '--keep-defines', '--line', result.args[-1]], cwd=root)

        def permutations(name, directory, result, configurations=None):
            if not compiler:
                return
            result = compilable(result)
            generated = rewritten_file(directory / 'main.c', result.stdout)
            if configurations is None:
                configurations = [(str(value), ['-DFEATURE=' + str(value)]) for value in (0, 1)]
            for label, options in configurations:
                original = preprocess(directory / 'main.c', options)
                rewritten = preprocess(generated, options)
                check(name + ' permutation ' + label,
                      result.returncode >= 0 and not result.returncode & 12 and
                      original.returncode == rewritten.returncode == 0 and
                      tokens(original.stdout) == tokens(rewritten.stdout),
                      'coan:\n' + result.stdout + '\noriginal:\n' + original.stdout +
                      '\nrewritten:\n' + rewritten.stdout + rewritten.stderr)

        def expanded(name, files, expected, args=(), source='main.c', reprocess=False, cpp_args=None):
            directory = fixture(name, files)
            if callable(expected):
                expected = expected(directory)
            options = [arg.replace('@ROOT@', str(directory)) for arg in args]
            oracle_options = options if cpp_args is None else [
                arg.replace('@ROOT@', str(directory)) for arg in cpp_args]
            path = directory / source
            result = coan(path, options)
            actual = ordinary_tokens(result.stdout)
            check(name, result.returncode >= 0 and not result.returncode & 12 and
                  actual == tokens(expected), result.stdout + result.stderr)
            if compiler:
                oracle = preprocess(path, oracle_options, root_only=True)
                check(name + ' compiler', oracle.returncode == 0 and
                      actual == root_preprocessing_tokens(oracle.stdout, path),
                      'coan:\n' + result.stdout + '\ncompiler:\n' + oracle.stdout + oracle.stderr)
                if reprocess:
                    oracle = preprocess(path, oracle_options)
                    preserved = compilable(result)
                    generated = rewritten_file(path, preserved.stdout)
                    rewritten = preprocess(generated, oracle_options)
                    check(name + ' downstream preprocessing',
                          preserved.returncode >= 0 and not preserved.returncode & 12 and
                          oracle.returncode == rewritten.returncode == 0 and
                          tokens(oracle.stdout) == tokens(rewritten.stdout),
                          'coan:\n' + preserved.stdout + '\noriginal:\n' + oracle.stdout +
                          '\nrewritten:\n' + rewritten.stdout + rewritten.stderr)
            return result

        machinery = '''\
#define CAT_RAW(a,b) a##b
#define CAT(a,b) CAT_RAW(a,b)
#define ID(x) x
#define APPLY(fn,...) fn(__VA_ARGS__)
#define DECL(name,value) int CAT(item_,name) = value;
#define DECL_ALIAS DECL
#define FIELDS(X) X(alpha,1) X(beta,2)
#define FIELDS_ALIAS FIELDS
#define MAKE_NAME() DECL_ALIAS
'''
        root_text = '''\
#include "helpers.h"
/* The arguments: X(ID, name). */
#define CODE_ARG_LIST(X) X(REPO, "repo") X(PATH, "path")
#define CODE_ARG_ENUM(id, ...) CODE_ARG_##id,
enum code_arg { CODE_ARG_LIST(CODE_ARG_ENUM) CODE_ARGS };
#undef CODE_ARG_ENUM
#define CODE_ARG_NAME(id, name) WRAP(name),
char const *names[] = { CODE_ARG_LIST(CODE_ARG_NAME) };
#undef CODE_ARG_NAME
#undef CODE_ARG_LIST
'''
        directory = fixture('clean-root-output', {
            'main.c': root_text,
            'headers/helpers.h': '#include "detail.h"\nstruct header_declaration { int hidden; };\n',
            'headers/detail.h': '#define WRAP(x) x\nint header_function(void) { return 42; }\n'})
        options = ['-I' + str(directory / 'headers')]
        result = coan(directory / 'main.c', options)
        expected = 'enum code_arg { CODE_ARG_REPO, CODE_ARG_PATH, CODE_ARGS }; '
        expected += 'char const *names[] = { "repo", "path", };'
        check('default source emits expanded root code only',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('/* The arguments: X(ID, name). */\n' + expected),
              result.stdout + result.stderr)
        check('default source removes consumed macro directives and generated locations',
              '#define' not in result.stdout and '#undef' not in result.stdout and
              '#line' not in result.stdout and '#include "helpers.h"\n' in result.stdout and
              'header_declaration' not in result.stdout and 'header_function' not in result.stdout and
              'detail.h' not in result.stdout, result.stdout + result.stderr)
        check('normal included definitions and X-macro cleanup are quiet',
              result.stderr == '', result.stderr)
        definitions = coan(directory / 'main.c', [*options, '--keep-defines'])
        check('keep-defines retains only root macro context',
              definitions.returncode >= 0 and not definitions.returncode & 12 and
              '#define CODE_ARG_LIST(X)' in definitions.stdout and '#undef CODE_ARG_LIST' in definitions.stdout and
              '#define WRAP' not in definitions.stdout and '#line' not in definitions.stdout,
              definitions.stdout + definitions.stderr)
        located = coan(directory / 'main.c', [*options, '--line'])
        check('line directives are opt-in and identify only the root input',
              located.returncode >= 0 and not located.returncode & 12 and '#line 1 ' in located.stdout and
              str(directory / 'main.c') in located.stdout and
              str(directory / 'headers') not in located.stdout and
              ordinary_tokens(located.stdout) == ordinary_tokens(result.stdout),
              located.stdout + located.stderr)
        if compiler:
            oracle = preprocess(directory / 'main.c', options, root_only=True)
            # Comments are retained by coan but are absent from compiler output.
            body = result.stdout.replace('/* The arguments: X(ID, name). */', '')
            check('clean root X-macro expansion agrees with compiler attribution',
                  oracle.returncode == 0 and ordinary_tokens(body) ==
                  root_preprocessing_tokens(oracle.stdout, directory / 'main.c'),
                  result.stdout + oracle.stdout + oracle.stderr)
        permutations('root-only X-macro downstream preprocessing', directory, result,
                     [('default', options)])

        expanded('inline-xmacro', {'main.c': machinery +
                 'APPLY(ID(FIELDS_ALIAS),ID(DECL_ALIAS))\n'},
                 'int item_alpha = 1; int item_beta = 2;')
        expanded('call-name-from-argument', {'main.c': machinery +
                 'APPLY(MAKE_NAME(),gamma,3)\nID(MAKE_NAME())(delta,4)\n'},
                 'int item_gamma = 3; int item_delta = 4;')
        expanded('ordinary-call-arguments', {'main.c': '''\
#define VALUE 42
#define APPLY(fn,x) fn(x)
int value = func(VALUE);
int other = APPLY(func,VALUE);
'''}, 'int value=func(42); int other=func(42);')
        expanded('multiline-calls', {'main.c': machinery +
                 'APPLY(\nMAKE_NAME(),\nalpha, 1\n)\nDECL_ALIAS\n(beta,2)\n'},
                 'int item_alpha = 1; int item_beta = 2;')
        expanded('replacement-token-boundaries', {'main.c': '''\
#define EMPTY()
#define F() x
+EMPTY()+
F()y
'''}, '+ + x y')
        text = '#define ID(x) x\nID(\n17\n)\nint line=__LINE__;\n'
        result = expanded('multiline-source-positions', {'main.c': text}, '17 int line=5;')
        check('multiline invocation retains physical newlines',
              result.stdout.count('\n') == text.count('\n') - 1 and
              '#line ' not in result.stdout, result.stdout + result.stderr)
        expanded('line-macro-context', {'main.c': '''\
#define PAIR(a,b) a,b
PAIR(
__LINE__,
__LINE__)
#define L __LINE__
L
L
'''}, '3,4 6 7')
        expanded('line-directive-context', {'main.c': '#line 200 "virt.h"\n__LINE__ __FILE__\n'},
                 '200 "virt.h"')
        expanded('conditional-line-context', {'main.c':
                 '#if __LINE__ == 1\nLINE_ONE\n#endif\n#if \\\n__LINE__ == 5\nLINE_FIVE\n#else\nBAD\n#endif\n'},
                 'LINE_ONE LINE_FIVE')
        expanded('conditional-macro-line-context', {'main.c':
                 '#define L __LINE__\n#if L == 2\nLINE_TWO\n#endif\n'}, 'LINE_TWO')
        expanded('include-line-directive-context', {
                 'main.c': '#include "renamed.h"\n__LINE__ __FILE__\n',
                 'renamed.h': '#line 90 "virtual_header.h"\n__LINE__ __FILE__\n'},
                 lambda directory: '2 ' + json.dumps(str(directory / 'main.c')))
        expanded('conditional-include-line-context', {
                 'main.c': '#include "renamed.h"\n#if __LINE__ == 2\nPARENT_LINE\n#endif\n',
                 'renamed.h': '#line 200 "virtual.h"\n#if __LINE__ == 200\nHEADER_LINE\n#endif\n'},
                 'PARENT_LINE')
        expanded('directive-call-boundary', {'main.c': '#define F(x) x\nF\n#if 1\n(5)\n#endif\n'},
                 'F(5)', reprocess=True)
        expanded('include-call-boundary', {
                 'main.c': '#define F(x) x\n#include "trailing.h"\n(5)\n',
                 'trailing.h': 'F\n'}, '(5)', reprocess=True)
        expanded('paste-indirection', {'main.c': machinery + '''\
#define PREFIX OP_
#define NAME increment
#define OP_increment(x) ((x)+1)
#define CALL(prefix,name,x) CAT(prefix,name)(x)
CAT_RAW(PREFIX,NAME)
CALL(PREFIX,NAME,4)
'''}, 'PREFIXNAME ((4)+1)')
        expanded('variadic-callbacks', {'main.c': machinery + '''\
#define EMPTY
#define OPTIONAL(...) __VA_OPT__(__VA_ARGS__)
#define VALUES(head,...) head __VA_OPT__(, __VA_ARGS__)
OPTIONAL(EMPTY)
APPLY(FIELDS_ALIAS, DECL_ALIAS)
VALUES(1)
VALUES(1, 2, 3)
'''}, 'int item_alpha=1; int item_beta=2; 1 1,2,3')
        expanded('literal-and-stringification', {'main.c': '''\
#define X 42
#define STR_RAW(x) #x
#define STR(x) STR_RAW(x)
"X" 'X' X STR_RAW(X) STR(X) STR_RAW(one /* gap */ two)
'''}, '"X" \'X\' 42 "X" "42" "one two"')
        expanded('recursive-source', {'main.c': '''\
#define SELF SELF
#define LEFT RIGHT
#define RIGHT LEFT
SELF LEFT RIGHT
'''}, 'SELF LEFT RIGHT')
        expanded('recursive-source-idempotence', {'main.c': '''\
#define A (A+1)
A
'''}, '(A+1)', reprocess=True)
        large_body = ''.join(f'int value_{index}=0;\n' for index in range(600))
        expanded('large-ordinary-source', {'main.c': '#define VALUE 83\n' + large_body +
                 'int selected=VALUE;\n'}, large_body + 'int selected=83;')
        expanded('many-small-expansions', {'main.c': '#define VALUE 123\n' + 'VALUE\n' * 2000},
                 '123\n' * 2000, ['--max-expansion=4'], cpp_args=[])
        expanded('large-flat-mixed-expansions', {'main.c':
                 '#define OBJECT 1+2\n#define FN(x) ((x)+3)\n#define ALIAS FN\n' +
                 'OBJECT ALIAS(OBJECT)\n' * 4096}, '1+2 ((1+2)+3)\n' * 4096)

        text = '/* unchanged */ int  value = func(1); // keep spacing\nchar const *s = "NAME";\n'
        directory = fixture('unchanged-source-trivia', {'main.c': text})
        result = coan(directory / 'main.c')
        ordinary = ''.join(line for line in result.stdout.splitlines(keepends=True)
                           if not line.startswith('#line '))
        check('untouched source preserves comments and spacing', result.returncode >= 0 and
              not result.returncode & 12 and ordinary == text, result.stdout + result.stderr)
        text = '#define VALUE 7\n/* before */ int  value = VALUE; // trailing\n'
        directory = fixture('expanded-source-trivia', {'main.c': text})
        result = coan(directory / 'main.c')
        check('expansion preserves surrounding comments and spacing', result.returncode >= 0 and
              not result.returncode & 12 and
              result.stdout.endswith('/* before */ int  value = 7; // trailing\n'),
              result.stdout + result.stderr)
        expanded('quoted-xmacro', {'machinery.h': machinery,
                 'main.c': '#include "machinery.h"\nAPPLY(FIELDS_ALIAS,DECL_ALIAS)\n'},
                 'int item_alpha=1; int item_beta=2;')
        expanded('quoted-deferred-foreach', {
                 'main.c': '#include "foreach.h"\nFOR_EACH(SELECT())\nFOR_EACH(SELECT(),one,two,three)\n',
                 'foreach.h': '''\
#define PARENS ()
#define EVAL(...) EVAL4(EVAL4(EVAL4(__VA_ARGS__)))
#define EVAL4(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define EVAL2(...) __VA_ARGS__
#define FOR_EACH(m,...) __VA_OPT__(EVAL(FOR_EACH_HELPER(m,__VA_ARGS__)))
#define FOR_EACH_HELPER(m,a,...) m(a) __VA_OPT__(FOR_EACH_AGAIN PARENS (m,__VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER
#define DECL(name) int item_##name;
#define DECL_ALIAS DECL
#define SELECT() DECL_ALIAS
'''}, 'int item_one; int item_two; int item_three;')
        expanded('digraph-directives', {
                 'main.c': '%:define HEADER "defs.h"\n%:include HEADER\nVALUE\n',
                 'defs.h': '%:define VALUE 97\n'}, '97')
        expanded('include-search-order', {
                 'project/main.c': '#include "value.h"\nLOCAL_VALUE\n#include "fallback.h"\nSEARCH_VALUE\n',
                 'project/value.h': '#define LOCAL_VALUE 7\n',
                 'first/value.h': '#define LOCAL_VALUE 99\n',
                 'first/fallback.h': '#define SEARCH_VALUE 11\n',
                 'second/fallback.h': '#define SEARCH_VALUE 22\n'},
                 '7 11', ['-I@ROOT@/first', '-I', '@ROOT@/second'], 'project/main.c')
        expanded('include-path-with-spaces', {
                 'main.c': '#include "values.h"\nVALUE\n',
                 'include path/values.h': '#define VALUE 23\n'},
                 '23', ['-I', '@ROOT@/include path'])
        expanded('nested-relative-includes', {
                 'main.c': '#include "project/outer.h"\nOUTER\n',
                 'project/outer.h': '#include "detail/inner.h"\n#define OUTER INNER\n',
                 'project/detail/inner.h': '#include "../sibling.h"\n#define INNER SIBLING\n',
                 'project/sibling.h': '#define SIBLING 37\n'}, '37')
        expanded('header-without-final-newline', {
                 'main.c': '#include "value.h"\nint root=VALUE;\n',
                 'value.h': '#define VALUE 13\nint header_without_newline;'}, 'int root=13;')
        expanded('macro-include-operand', {
                 'main.c': '#define ID(x) x\n#define HEADER "values.h"\n#include ID(HEADER)\nVALUE\n',
                 'values.h': '#define VALUE 41\n'}, '41')
        expanded('include-guards', {
                 'main.c': '#include "guarded.h"\n#include "guarded.h"\nVALUE\n',
                 'guarded.h': '#ifndef PROJECT_GUARDED_H\n#define PROJECT_GUARDED_H\nint declaration;\n#define VALUE 43\n#endif\n'},
                 '43')
        expanded('pragma-once-path-alias', {
                 'main.c': '#include "once.h"\n#include "./once.h"\nVALUE\n',
                 'once.h': '#pragma once\nint once_declaration;\n#define VALUE 47\n'},
                 '47')
        for name, literal in (('pragma-once-operator', '"once"'),
                              ('pragma-once-wide-operator', 'L"once"')):
            expanded(name, {
                     'main.c': '#include "once.h"\n#include "./once.h"\nVALUE\n',
                     'once.h': '#define ONCE _Pragma(' + literal + ')\nONCE\n'
                               'int once_declaration;\n#define VALUE 47\n'},
                     '47', reprocess=True)
        expanded('guarded-include-cycle', {
                 'main.c': '#include "a.h"\nA_VALUE B_VALUE\n',
                 'a.h': '#ifndef PROJECT_A_H\n#define PROJECT_A_H\n#include "b.h"\n#define A_VALUE 53\n#endif\n',
                 'b.h': '#ifndef PROJECT_B_H\n#define PROJECT_B_H\n#include "a.h"\n#define B_VALUE 59\n#endif\n'},
                 '53 59')
        expanded('unguarded-xmacro-reinclude', {
                 'items.def': 'X(alpha,1)\nX(beta,2)\n',
                 'main.c': '''\
#define X(name,value) item_##name = value,
enum {
#include "items.def"
};
#undef X
#define X(name,value) #name,
char const *names[] = {
#include "items.def"
};
'''}, 'enum { }; char const *names[] = { };', reprocess=True)
        for value in (0, 1):
            expanded('configured-header-branch-' + str(value), {
                     'main.c': '#include "choice.h"\nSELECTED\n',
                     'choice.h': '#if FEATURE\n#define SELECTED 61\n#else\n#define SELECTED 67\n#endif\n'},
                     '61' if value else '67', ['-DFEATURE=' + str(value)])

        for name, text, headers in (
                ('conditional-definition', '#if FEATURE\n#define PICK(x) x##_yes\n#else\n#define PICK(x) x##_no\n#endif\nint PICK(tag);\n', {}),
                ('conditional-helper-use', '#define DECL(x) int x;\n#if FEATURE\nDECL(enabled)\n#else\nDECL(disabled)\n#endif\n', {}),
                ('conditional-deferred-line', '#if FEATURE\n#define M() __LINE__\n#else\n#define M() 0\n#endif\n#if 0\ndiscard\n#endif\nM()\n', {}),
                ('conditional-opaque-recursion', '#define A (A+1)\n#if FEATURE\n#define USE(x) x\n#else\n#define USE(x) #x\n#endif\nA\nUSE(A)\n', {}),
                ('conditional-forward-stringification', '''\
#define A 1
#if FEATURE
#define U(x) x
#else
#define U(x) other
#endif
#define STR0(x) #x
#define STR(x) STR0(x)
STR(U(A))
''', {}),
                ('conditional-forward-paste', '''\
#define A alpha
#if FEATURE
#define U(x) x
#else
#define U(x) beta
#endif
#define CAT0(a,b) a##b
#define CAT(a,b) CAT0(a,b)
CAT(U(A),_suffix)
''', {}),
                ('conditional-call-name', '''\
#define A 99
#define ID(x) x
#define QUOTE(x) #x
#define VALUE(x) x
#if FEATURE
#define U QUOTE
#else
#define U VALUE
#endif
ID(U)(A)
''', {}),
                ('conditional-call-barrier-location',
                 '#define F(x) x\n#if FEATURE\nF\n#if 1\n(5)\n#endif\n#line 30\n#endif\n__LINE__\n', {}),
                ('conditional-recursive-location',
                 '#define A (A+1)\n#if FEATURE\nA\n#line 30\n#endif\n__LINE__\n', {}),
                ('conditional-produced-call-name', '''\
#define A 99
#define QUOTE(x) #x
#define VALUE(x) x
#if FEATURE
#define U() QUOTE
#else
#define U() VALUE
#endif
U()(A)
''', {}),
                ('conditional-unused-and-quoted', '''\
#define DROP(x) 0
#define RAW(x) #x
#if FEATURE
#define U(x) x
#else
#define U(x) other
#endif
DROP(U(A)) RAW(U(A))
''', {}),
                ('conditional-entry-restore', '#define VALUE 0\n#if FEATURE\n#undef VALUE\n#define VALUE 1\nint yes=VALUE;\n#else\nint no=VALUE;\n#endif\nint after=VALUE;\n', {}),
                ('conditional-equal-merge', '#if FEATURE\n#define VALUE 7\n#else\n#define VALUE 7\n#endif\nint after=VALUE;\n', {}),
                ('elif-entry-restore', '#define X 0\n#if FEATURE\n#undef X\n#define X 1\n#elif X == 0\n#define Y 2\n#endif\nint x=X;\nY\n', {}),
                ('conditional-include', '#if FEATURE\n#include "enabled.h"\n#endif\nVALUE\n',
                 {'enabled.h': '#define VALUE 71\n'}),
                ('conditional-include-invalidation', '#define VALUE 0\n#if FEATURE\n#include "enabled.h"\n#endif\nint after=VALUE;\n',
                 {'enabled.h': '#undef VALUE\n#define VALUE 1\n'}),
                ('nested-conditional-include', '#define VALUE 0\n#if FEATURE\n#if 1\n#if 1\n#include "enabled.h"\n#endif\n#endif\n#endif\nint after=VALUE;\n',
                 {'enabled.h': '#undef VALUE\n#define VALUE 1\n'})):
            directory = fixture(name, {'main.c': text, **headers})
            result = coan(directory / 'main.c')
            check(name + ' retains choice', result.returncode >= 0 and not result.returncode & 12 and
                  '#if FEATURE' in result.stdout, result.stdout + result.stderr)
            fragments = {
                'conditional-entry-restore': ('int yes=1;', 'int no=0;', 'int after=VALUE;'),
                'conditional-equal-merge': ('int after=7;',),
                'conditional-include-invalidation': ('int after=VALUE;',),
                'conditional-unused-and-quoted': ('0 "U(A)"',),
            }.get(name, ())
            if fragments:
                emitted = ' '.join(ordinary_tokens(result.stdout))
                check(name + ' branch knowledge', all(' '.join(tokens(fragment)) in emitted
                      for fragment in fragments), result.stdout + result.stderr)
            permutations(name, directory, result)

        for undefined_branch in ('first', 'second'):
            branch = '#undef X\n' if undefined_branch == 'first' else ''
            alternative = '#undef X\n' if undefined_branch == 'second' else ''
            directory = fixture('unknown-include-branch-merge-' + undefined_branch, {
                'main.c': '#if FLAG\n#include "config.h"\n#endif\n#ifdef X\n#endif\n'
                          '#if CHOICE\n' + branch + '#else\n' + alternative + '#endif\n'
                          '#ifdef X\nPRESENT\n#else\nABSENT\n#endif\n',
                'config.h': '#define X 1\n'})
            result = coan(directory / 'main.c')
            check('unknown include merge preserves unspecified alternative ' + undefined_branch,
                  result.returncode >= 0 and not result.returncode & 12 and
                  'PRESENT' in result.stdout and 'ABSENT' in result.stdout,
                  result.stdout + result.stderr)
            permutations('unknown include branch merge ' + undefined_branch, directory, result,
                         [(f'FLAG={flag},CHOICE={choice}', ['-DFLAG=' + str(flag), '-DCHOICE=' + str(choice)])
                          for flag in (0, 1) for choice in (0, 1)])

        for name, operation, system_definition, second_value, expected in (
                ('opaque-once-operation', 'ONCE()', '#define ONCE() _Pragma("once")\n', 2, 'VALUE'),
                ('opaque-empty-operation', 'ONCE()', '#define ONCE()\n', 2, 'VALUE'),
                ('opaque-discarded-once', 'IGNORE(_Pragma("once"))', '#define IGNORE(x)\n', 2, 'VALUE'),
                ('opaque-once-equal-definitions', 'ONCE()', '#define ONCE() _Pragma("once")\n', 1, '1')):
            directory = fixture(name, {
                'main.c': '#define STEP 1\n#include "state.h"\n#undef STEP\n#define STEP 2\n'
                          '#include "state.h"\nVALUE\n',
                'state.h': '#include <opaque.h>\n' + operation + '\n#undef VALUE\n#if STEP == 1\n#define VALUE 1\n'
                           '#else\n#define VALUE ' + str(second_value) + '\n#endif\n',
                'opaque.h': system_definition})
            options = ['-I' + str(directory)]
            result = coan(directory / 'main.c', options)
            check(name + ' retains possible header executions',
                  result.returncode >= 0 and not result.returncode & 12 and
                  ordinary_tokens(result.stdout) == tokens(expected), result.stdout + result.stderr)
            permutations(name, directory, result, [('default', options)])

        for once in (False, True):
            directory = fixture('opaque-nested-once-' + str(once), {
                'main.c': '#define MODE 1\n#include "outer.h"\n#undef MODE\n#define MODE 2\n'
                          '#include "outer.h"\n#undef CHILD_VALUE\n#define CHILD_VALUE 0\n'
                          '#undef MODE\n#define MODE 3\n#include "child.h"\nCHILD_VALUE\n',
                'outer.h': '#include <opaque.h>\nONCE()\n#if MODE == 2\n#include "child.h"\n#endif\n',
                'child.h': '#pragma once\n#undef CHILD_VALUE\n#if MODE == 2\n#define CHILD_VALUE 2\n'
                           '#else\n#define CHILD_VALUE 3\n#endif\n',
                'opaque.h': '#define ONCE()' + (' _Pragma("once")' if once else '') + '\n'})
            options = ['-I' + str(directory)]
            result = coan(directory / 'main.c', options)
            check('nested once state remains conditional ' + str(once),
                  result.returncode >= 0 and not result.returncode & 12 and
                  ordinary_tokens(result.stdout) == ['CHILD_VALUE'], result.stdout + result.stderr)
            permutations('nested possible once ' + str(once), directory, result, [('default', options)])

        for transitive in (False, True):
            first_header = 'outer.h' if transitive else 'child.h'
            directory = fixture('conditional-unread-once-' + str(transitive), {
                'main.c': '#define MODE 1\n#if FEATURE\n#include "' + first_header + '"\n#endif\n'
                          '#undef MODE\n#define MODE 2\n#include "child.h"\nVALUE\n',
                'outer.h': '#include "child.h"\n',
                'child.h': '#pragma once\n#if MODE == 1\n#define VALUE 1\n#else\n#define VALUE 2\n#endif\n'})
            result = coan(directory / 'main.c')
            check('unread conditional header can already be once-only ' + str(transitive),
                  result.returncode >= 0 and not result.returncode & 12 and
                  ordinary_tokens(result.stdout) == ['VALUE'], result.stdout + result.stderr)
            permutations('conditional unread once ' + str(transitive), directory, result)
            for feature in (0, 1):
                selected = coan(directory / 'main.c', ['-DFEATURE=' + str(feature)])
                check('selected conditional once remains decidable ' + str((transitive, feature)),
                      selected.returncode >= 0 and not selected.returncode & 12 and
                      ordinary_tokens(selected.stdout) == [str(1 if feature else 2)],
                      selected.stdout + selected.stderr)

        directory = fixture('conditional-pragma-once', {
            'main.c': '#define X(name) int name;\n#include "conditional.h"\n#include "conditional.h"\n',
            'conditional.h': '#if FEATURE\n#pragma once\n#endif\nX(entry)\n'})
        result = coan(directory / 'main.c')
        check('conditional pragma once remains valid', result.returncode >= 0 and
              not result.returncode & 12, result.stdout + result.stderr)
        permutations('conditional pragma once', directory, result)

        for name, main, header in (
                ('conditional-pragma-operator', '',
                 '#if FEATURE\n_Pragma("once")\n#endif\nint entry;\n'),
                ('opaque-pragma-operator',
                 '#if FEATURE\n#define U(x) x\n#else\n#define U(x)\n#endif\n',
                 'U(_Pragma("once"))\nint entry;\n'),
                ('conditional-pragma-alias', '',
                 '#if FEATURE\n#define ONCE _Pragma("once")\n#else\n#define ONCE\n#endif\n'
                 'ONCE\nint entry;\n'),
                ('conditional-pragma-indirect-alias', '',
                 '#define P _Pragma("once")\n#if FEATURE\n#define ONCE P\n#else\n'
                 '#define ONCE\n#endif\nONCE\nint entry;\n'),
                ('opaque-pragma-alias-argument', '',
                 '#define ONCE _Pragma("once")\n#if FEATURE\n#define U(x) x\n#else\n'
                 '#define U(x)\n#endif\nU(ONCE)\nint entry;\n')):
            directory = fixture(name, {
                'main.c': main + '#include "conditional.h"\n#include "conditional.h"\n',
                'conditional.h': header})
            result = coan(directory / 'main.c')
            check(name + ' defers header', result.returncode >= 0 and
                  not result.returncode & 12 and '#include ' in result.stdout,
                  result.stdout + result.stderr)
            permutations(name, directory, result)

        directory = fixture('nested-retained-include', {
            'main.c': '#define VALUE 0\n#include "sub/parent.h"\nint value=VALUE;\n',
            'sub/parent.h': '#if FEATURE\n#include "sibling.h"\n#endif\n',
            'sub/sibling.h': '#undef VALUE\n#define VALUE 101\n'})
        result = coan(directory / 'main.c')
        check('nested retained include remains valid', result.returncode >= 0 and
              not result.returncode & 12, result.stdout + result.stderr)
        permutations('nested retained include search context', directory, result)

        directory = fixture('nested-symbolic-include', {
            'main.c': '#include "sub/parent.h"\nint value=VALUE;\n',
            'sub/parent.h': '#include HEADER_NAME\n',
            'sub/one.h': '#define VALUE 103\n',
            'sub/two.h': '#define VALUE 107\n'})
        result = coan(directory / 'main.c')
        check('nested symbolic include remains valid', result.returncode >= 0 and
              not result.returncode & 12, result.stdout + result.stderr)
        permutations('nested symbolic include search context', directory, result,
                     [(name, ['-DHEADER_NAME="' + name + '.h"']) for name in ('one', 'two')])

        directory = fixture('conditional-line-directive', {
            'main.c': '#if FEATURE\n#line 300 "conditional.c"\n#endif\n__LINE__ __FILE__\n'})
        result = coan(directory / 'main.c')
        check('uncertain line directives preserve location macros', result.returncode >= 0 and
              not result.returncode & 12 and '__LINE__' in result.stdout and '__FILE__' in result.stdout,
              result.stdout + result.stderr)
        permutations('conditional line directive', directory, result)

        directory = fixture('explicit-line-option-location', {
            'main.c': '#include "virtual.h"\n#if FEATURE\n#line 300 "conditional.c"\n#endif\n'
                      '__LINE__ __FILE__\n',
            'virtual.h': '#line 20 "virtual.h"\nint hline=__LINE__;\n'})
        result = coan(directory / 'main.c', ['--line'])
        check('explicit line option preserves header and conditional locations',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('__LINE__ __FILE__'),
              result.stdout + result.stderr)
        permutations('explicit line option locations', directory, result)

        directory = fixture('conditional-file-known-line', {
            'main.c': '#if FEATURE\n#line 10 "a.c"\n#else\n#line 20 "b.c"\n#endif\n'
                      '#line 100\n#if 1\nint a=__LINE__;\n#endif\nint b=__LINE__;\n'})
        result = coan(directory / 'main.c', ['--line'])
        check('known line with conditional filename retains location directives',
              result.returncode >= 0 and not result.returncode & 12 and
              '#line 101\n' in result.stdout and '#line 103\n' in result.stdout,
              result.stdout + result.stderr)
        permutations('known line with conditional filename', directory, result)

        directory = fixture('retained-conditional-location', {
            'main.c': '#if 0\ndiscard\n#endif\n#if FEATURE\n#line 30\n#endif\nint v=__LINE__;\n'})
        result = coan(directory / 'main.c', ['--line'])
        check('retained conditional after dropped source preserves location',
              result.returncode >= 0 and not result.returncode & 12 and
              '#line 4 ' in result.stdout, result.stdout + result.stderr)
        permutations('retained conditional source location', directory, result)

        directory = fixture('opaque-line-operand-location', {
            'main.c': '#if 0\ndiscard\n#endif\n#line NEXT\nint v=__LINE__;\n'})
        result = coan(directory / 'main.c', ['--line'])
        check('opaque line directive operand preserves location',
              result.returncode >= 0 and not result.returncode & 12 and
              '#line 4 ' in result.stdout, result.stdout + result.stderr)
        permutations('opaque line directive operand source location', directory, result,
                     [('line macro', ['-DNEXT=__LINE__'])])

        directory = fixture('conditional-include-implicit', {
            'main.c': '#define VALUE 0\n#if __has_include(<coan_test_optional.h>)\n#include "selected.h"\n#endif\n#if VALUE\nint selected;\n#else\nint omitted;\n#endif\n#if defined(NEW_NAME)\nint new_name;\n#endif\n#if NEW_VALUE\nint new_value;\n#endif\n#undef NEW_VALUE\n#define NEW_VALUE 3\n#if NEW_VALUE == 3\nint known_again;\n#endif\n',
            'selected.h': '#undef VALUE\n#define VALUE 1\n#define NEW_NAME 1\n#define NEW_VALUE 2\n'})
        result = coan(directory / 'main.c', ['--implicit', '-I' + str(directory)])
        check('uncertain include survives implicit-undefined policy', result.returncode >= 0 and
              not result.returncode & 12 and '#if VALUE' in result.stdout and
              '#if defined(NEW_NAME)' in result.stdout and '#if NEW_VALUE' in result.stdout,
              result.stdout + result.stderr)
        check('explicit definition recovers after uncertain include',
              '#if NEW_VALUE ==' not in result.stdout and 'int known_again;' in result.stdout,
              result.stdout + result.stderr)
        if compiler:
            preserved = compilable(result)
            generated = rewritten_file(directory / 'main.c', preserved.stdout)
            for present in (False, True):
                if present:
                    (directory / 'coan_test_optional.h').write_text('')
                original = preprocess(directory / 'main.c', ['-I' + str(directory)])
                rewritten = preprocess(generated, ['-I' + str(directory)])
                check('implicit unresolved include permutation ' + str(present),
                      original.returncode == rewritten.returncode == 0 and
                      tokens(original.stdout) == tokens(rewritten.stdout),
                      result.stdout + '\noriginal:\n' + original.stdout +
                      '\nrewritten:\n' + rewritten.stdout + rewritten.stderr)

        directory = fixture('system-include', {
            'main.c': '#include <coan_test_system.h>\nSYSTEM_VALUE\n',
            'coan_test_system.h': '#error System header must not be opened\n#define SYSTEM_VALUE 73\n'})
        result = coan(directory / 'main.c', ['-I' + str(directory)])
        check('system include preserved', result.returncode >= 0 and not result.returncode & 12 and
              '#include <coan_test_system.h>' in result.stdout and
              ordinary_tokens(result.stdout) == ['SYSTEM_VALUE'], result.stdout + result.stderr)
        directory = fixture('system-include-implicit', {
            'main.c': '#define LOCAL 17\n#include <coan_test_system.h>\n#if defined(NEW_NAME)\nint absent;\n#endif\nint value=LOCAL;\n'})
        result = coan(directory / 'main.c', ['--implicit'])
        check('system includes leave configured and implicit assumptions intact',
              result.returncode >= 0 and not result.returncode & 12 and
              '#include <coan_test_system.h>' in result.stdout and
              ordinary_tokens(result.stdout) == tokens('int value=17;'), result.stdout + result.stderr)

        directory = fixture('opaque-system-call-arguments', {
            'main.c': '#include <stdint.h>\n#define N 2\nUINT64_C(N)\nfunc(N)\n' + machinery +
                      '#define FUNCTION(name,value) int CAT(item_,name)(void) { return value; }\n'
                      'APPLY(FIELDS_ALIAS,FUNCTION)\n'})
        result = coan(directory / 'main.c')
        check('opaque system calls retain raw arguments while project macros expand',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('UINT64_C(N) func(N) '
                  'int item_alpha(void) { return 1; } int item_beta(void) { return 2; }'),
              result.stdout + result.stderr)
        permutations('opaque system macro paste and ordinary calls', directory, result,
                     [('default', [])])

        directory = fixture('opaque-system-returned-call', {
            'main.c': '#include <opaque.h>\n#define N 2\n#define WRAP(x) x\nU()(N)\nWRAP(U()(N))\n',
            'opaque.h': '#define STR_RAW(x) #x\n#define U() STR_RAW\n'})
        result = coan(directory / 'main.c', ['-I' + str(directory)])
        check('opaque system call chains retain raw argument tokens',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('U()(N) WRAP(U()(N))'), result.stdout + result.stderr)
        permutations('opaque system returned macro name', directory, result,
                     [('default', ['-I' + str(directory)])])

        for name, header, definitions, invocation in (
                ('opaque-system-vaopt', '#define U()\n',
                 '#define V(...) __VA_OPT__(int present;)\n', 'V(U())'),
                ('opaque-system-stringification', '#define U() 100\n',
                 '#define STR_RAW(x) #x\n#define STR(x) STR_RAW(x)\n', 'STR(U())')):
            directory = fixture(name, {
                'main.c': '#include <opaque.h>\n' + definitions + invocation + '\n',
                'opaque.h': header})
            result = coan(directory / 'main.c', ['-I' + str(directory)])
            check(name + ' preserves deferred prescan', result.returncode >= 0 and
                  not result.returncode & 12 and ordinary_tokens(result.stdout) == tokens(invocation),
                  result.stdout + result.stderr)
            permutations(name, directory, result, [('default', ['-I' + str(directory)])])

        directory = fixture('opaque-system-known-function', {
            'main.c': '#include <stdint.h>\n#define N 2\n#undef func\nfunc(N)\n'})
        result = coan(directory / 'main.c')
        check('explicitly undefined function still expands arguments after system include',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('func(2)'), result.stdout + result.stderr)
        permutations('known nonmacro function after system include', directory, result,
                     [('default', [])])

        directory = fixture('system-state-per-file', {
            'first.c': '#include <stdint.h>\n#define N 2\nfunc(N)\n',
            'second.c': '#define N 2\nfunc(N)\n'})
        result = run([COAN, 'source', '-E', '-DCOAN_TEST=1', '--replace',
                      directory / 'first.c', directory / 'second.c'], cwd=root)
        check('opaque system state resets between translation units',
              result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens((directory / 'first.c').read_text()) == tokens('func(N)') and
              ordinary_tokens((directory / 'second.c').read_text()) == tokens('func(2)'),
              result.stdout + result.stderr + (directory / 'first.c').read_text() +
              (directory / 'second.c').read_text())

        directory = fixture('dead-include', {'main.c': '#if 0\n#include "absent.h"\n#endif\nint live;\n'})
        result = coan(directory / 'main.c')
        check('unreachable include is not opened', result.returncode >= 0 and not result.returncode & 12 and
              ordinary_tokens(result.stdout) == tokens('int live;'), result.stdout + result.stderr)

        text = '#define VALUE 42\n#define CALL(x) x\n#include "absent.h"\nint value=CALL(VALUE);\n'
        directory = fixture('directive-only-compatibility', {'main.c': text})
        result = coan(directory / 'main.c', ['--no-expand'])
        check('explicit directive-only compatibility mode', result.returncode >= 0 and
              not result.returncode & 12 and result.stdout == text, result.stdout + result.stderr)

        for name, header, options in (('stdin-local', 'defs.h', []),
                                      ('stdin-search', 'headers/defs.h', ['-I', 'headers'])):
            directory = fixture(name, {header: '#define VALUE 89\n'})
            result = run([COAN, 'source', '-E', '-DCOAN_TEST=1', *options],
                         input='#include "defs.h"\nVALUE\n', cwd=directory)
            check(name, result.returncode >= 0 and not result.returncode & 12 and
                  ordinary_tokens(result.stdout) == ['89'], result.stdout + result.stderr)

        directory = fixture('multiple-roots', {
            'once.h': '#pragma once\nint header_declaration;\n#define VALUE 79\n',
            'first.c': '#include "once.h"\nVALUE\n',
            'second.c': '#include "once.h"\nVALUE\n'})
        header = (directory / 'once.h').read_text()
        result = run([COAN, 'source', '-E', '-DCOAN_TEST=1', '--replace',
                      directory / 'first.c', directory / 'second.c'], cwd=root)
        check('include state resets between translation units', result.returncode >= 0 and
              not result.returncode & 12 and all(
                  ordinary_tokens((directory / name).read_text()) == tokens('79')
                  for name in ('first.c', 'second.c')) and (directory / 'once.h').read_text() == header,
              result.stdout + result.stderr)

        directory = fixture('replace-included-undef', {
            'main.c': '#include "undef.h"\n#ifdef FOO\nBAD\n#else\nGOOD\n#endif\n',
            'undef.h': '#undef FOO\n'})
        result = run([COAN, 'source', '-E', '-DFOO=1', '--replace', directory / 'main.c'], cwd=root)
        rewritten = (directory / 'main.c').read_text()
        check('included undef uses replacement output stream', result.returncode >= 0 and
              not result.returncode & 12 and result.stdout == '' and '#undef FOO' not in rewritten and
              ordinary_tokens(rewritten) == ['GOOD'], result.stdout + result.stderr + rewritten)

        for name, first in (
                ('global-redefinition-reset', '#undef X\n#define X 9\n'),
                ('opaque-include-reset', '#if FEATURE\n#include "choice.h"\n#endif\n')):
            directory = fixture(name, {
                'first.c': first,
                'second.c': '#if X == 7\nRESTORED_GLOBAL\n#else\nBAD\n#endif\n',
                'choice.h': '#undef X\n#define X 9\n'})
            result = run([COAN, 'source', '-E', '-DX=7', '--replace',
                          directory / 'first.c', directory / 'second.c'], cwd=root)
            check(name, result.returncode >= 0 and not result.returncode & 12 and
                  ordinary_tokens((directory / 'second.c').read_text()) == ['RESTORED_GLOBAL'],
                  result.stdout + result.stderr + (directory / 'second.c').read_text())

        for name, files, diagnostic in (
                ('missing-header', {'main.c': '#include "absent.h"\n'}, 'absent.h'),
                ('unguarded-include-cycle', {'main.c': '#include "cycle.h"\n',
                                            'cycle.h': '#include "cycle.h"\n'}, 'cycle.h'),
                ('included-error-location', {'main.c': '#include "failure.h"\n',
                                             'failure.h': '#if (\n#endif\n'}, 'failure.h')):
            directory = fixture(name, files)
            result = coan(directory / 'main.c')
            check(name, result.returncode >= 0 and bool(result.returncode & 12) and
                  diagnostic in result.stderr, result.stdout + result.stderr)

    print(f'{checks - len(failures)}/{checks} source/include checks passed' +
          ('' if compiler else ' (compiler comparisons skipped: CPP unavailable or lacks C23 support)'))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
