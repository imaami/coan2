/***************************************************************************
 *   Copyright (C) 2013 Mike Kinghan, imk@burroingroingjoing.com           *
 *   All rights reserved.                                                  *
 *                                                                         *
 *   Contributed originally by Mike Kinghan, imk@burroingroingjoing.com    *
 *                                                                         *
 *   Redistribution and use in source and binary forms, with or without    *
 *   modification, are permitted provided that the following conditions    *
 *   are met:                                                              *
 *                                                                         *
 *   Redistributions of source code must retain the above copyright        *
 *   notice, this list of conditions and the following disclaimer.         *
 *                                                                         *
 *   Redistributions in binary form must reproduce the above copyright     *
 *   notice, this list of conditions and the following disclaimer in the   *
 *   documentation and/or other materials provided with the distribution.  *
 *                                                                         *
 *   Neither the name of Mike Kinghan nor the names of its contributors    *
 *   may be used to endorse or promote products derived from this software *
 *   without specific prior written permission.                            *
 *                                                                         *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   *
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     *
 *   LIMITED TO, THE IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS    *
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE        *
 *   COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,   *
 *   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,  *
 *   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS *
 *   OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED    *
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,*
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF *
 *   THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH  *
 *   DAMAGE.                                                               *
 *                                                                         *
 **************************************************************************/

/** \file parsed_line.cpp
 *	This file implements `struct parsed_line`
 */

#include "parsed_line.h"
#include "io.h"
#include "lexicon.h"
#include "if_control.h"
#include "line_despatch.h"
#include "options.h"
#include "diagnostic.h"
#include "citable.h"
#include "expansion_base.h"
#include "source_inclusion.h"
#include <algorithm>
#include "pp_token.h"

/// \cond NO_DOXYGEN

using namespace std;

parsed_line::parsed_line(std::istream * in, std::ostream * out)
	: _logical_filename(io::in_file_name()), _in(in), _out(out) {}

void parsed_line::write_line_marker(unsigned logical_line)
{
	if (_logical_line_known) {
		*_out << "#line " << logical_line;
		if (_logical_file_known) {
			*_out << ' ' << pp::stringify(pp::tokens{pp::token(_logical_filename,pp::kind::literal)});
		}
		*_out << '\n';
	}
}

size_t parsed_line::extend()
{
	string line;
	getline(*_in,line);
	if (_in->fail() && !_in->eof()) {
		abend_cant_read_input() << "Read error on file "
		                        << io::in_file_name() << emit();
		return 0;
	}
	size_t bytes = line.length();
	if (!_in->eof()) {
		line += '\n';
		++bytes;
	} else if (bytes > 0) {
		warning_missing_eof_newline() <<
			"Missing newline at end of file" << emit();
	}
	_text += line;
	_lineno += bytes != 0;
	_logical_lineno += bytes != 0;
	return bytes;
}

size_t parsed_line::extend(size_t skip)
{
	if (skip) {
		++_extensions;
		size_t bytes = extend();
		if (bytes) {
			return skip;
		}
		error_eof_too_soon() << "Unexpected end of file" << emit();
	}
	return skip;
}

bool parsed_line::get()
{
	bool got;
	_extensions = 0;
	_simplified = false;
	_is_directive = false;
	_indent = 0;
	_first_line = _lineno + 1;
	_logical_first_line = _logical_lineno + 1;
	clear();
	got = extend();
	return got;
}


void parsed_line::keyword_lop(std::string const & keyword) {
	_text.resize(_keyword_posn);
	_text += keyword;
	_text += '\n';
	_keyword_len = keyword.length();
	++line_despatch::lines_changed();
}

void parsed_line::keyword_swap(std::string const & directive) {
	_text.replace(_keyword_posn,_keyword_len,directive);
	_keyword_len = directive.length();
	++line_despatch::lines_changed();
}

template<> void parsed_line::keyword_edit<directive_type::HASH_IF>() {
	keyword_swap(TOK_IF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_IFDEF>() {
	keyword_swap(TOK_IFDEF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_IFNDEF>() {
	keyword_swap(TOK_IFNDEF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_ELSE>() {
	keyword_lop(TOK_ELSE);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_ELIF>() {
	keyword_swap(TOK_ELIF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_ELIFDEF>() {
	keyword_swap(TOK_ELIFDEF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_ELIFNDEF>() {
	keyword_swap(TOK_ELIFNDEF);
}

template<> void parsed_line::keyword_edit<directive_type::HASH_ENDIF>() {
	keyword_lop(TOK_ENDIF);
}

void parsed_line::set_reportable()
{
	if (_dtype == COMMANDLINE) {
		_reportable = true;
		return;
	}
	bool verdict;
	switch(options::get_command()) {
	case CMD_SYMBOLS:
		switch(_dtype) {
		case HASH_ELIF:
		case HASH_ELIFDEF:
		case HASH_ELIFNDEF:
		case HASH_IFDEF:
		case HASH_IFNDEF:
		case HASH_IF:
			verdict = options::list_symbols_in_ifs();
			break;
		case HASH_DEFINE:
			verdict = options::list_symbols_in_defs();
			break;
		case HASH_UNDEF:
			verdict = options::list_symbols_in_undefs();
			break;
		case HASH_INCLUDE:
			verdict = options::list_symbols_in_includes();
			break;
		case HASH_LINE:
			verdict = options::list_symbols_in_lines();
			break;
		case HASH_UNKNOWN:
			verdict = false;
			break;
		default:
			verdict = true;
		}
		break;
	case CMD_INCLUDES:
		verdict = _dtype == HASH_INCLUDE;
		break;
	case CMD_DEFS:
		verdict = _dtype == HASH_DEFINE || _dtype == HASH_UNDEF;
		break;
	case CMD_PRAGMAS:
		verdict = _dtype == HASH_PRAGMA;
		break;
	case CMD_ERRORS:
		verdict = _dtype == HASH_ERROR;
		break;
	case CMD_LINES:
		verdict = _dtype == HASH_LINE;
		break;
	case CMD_DIRECTIVES:
		verdict = _dtype != HASH_UNKNOWN;
		break;
	default:
		verdict = false;
	}
	if (verdict) {
		if (options::list_only_cant_reach()) {
			verdict = if_control::cannot_reach_line();
		} else if (options::list_only_must_reach()) {
            verdict = if_control::must_reach_line();
		} else if (options::list_only_may_reach()) {
		    verdict = !if_control::cannot_reach_line();
		}
	}
	_reportable = verdict;
}

void parsed_line::write_fast() {
	*_out << _text;
}

void parsed_line::write_slow()
{
	size_t i = 0;
	for (	;i < _indent; ++i) {
		*_out << _text[i];
	}
	string output = citable(!options::plaintext(),*this,i);
	line_despatch::lines_changed() += _extensions + 1;
	*_out << output << '\n';
	if (options::get_discard_policy() == discard_policy::blank ||
		(options::preprocess_source() && (!logical_line_known() || if_control::uncertain_context()))) {
		for (	; _extensions; --_extensions) {
			*_out << '\n';
		}
	} else if (options::get_discard_policy() == discard_policy::comment) {
		for (	; _extensions; --_extensions) {
			*_out << "//coan <\n";
		}
	}
}

void parsed_line::write_commented_out()
{
	if (!is_simplified()) {
		++line_despatch::lines_changed();
		if (!_extensions) {
			*_out << "//coan < ";
			write_fast();
		} else {
			line_despatch::lines_changed() += _extensions;
			size_t start = 0;
			size_t end = _text.find('\n');
			for (	;end != string::npos;
					start = end + 1,end = _text.find('\n',start)) {
				*_out << "//coan < ";
				*_out << _text.substr(start,1 + end - start);
			}
		}
	} else {
		*_out << "//coan < ";
		write_slow();
	}
}

void parsed_line::write()
{
	if (!is_simplified()) {
		write_fast();
	} else {
		write_slow();
	}
}

void parsed_line::write(bool keep)
{
	if (keep) {
		write();
	} else {
		if (options::get_discard_policy() == discard_policy::blank) {
			line_despatch::lines_changed() += _extensions + 1;
			*_out << '\n';
			for (	; _extensions; --_extensions) {
				*_out << '\n';
			}
		} else if (options::get_discard_policy() == discard_policy::drop) {
			line_despatch::lines_suppressed() += _extensions + 1;
		} else {
			write_commented_out();
		}
	}
}

void parsed_line::output()
{
	command_code cmd = options::get_command();
	if (cmd == CMD_SOURCE || cmd == CMD_SPIN) {
		bool expand = options::preprocess_source() &&
			!_is_directive && !if_control::cannot_reach_line();
		if (!expand) {
			flush_source();
			_source_boundary_pending = false;
		}
		if (options::line_directives() && !options::preprocess_source()) {
			if (_drop_run_length) {
				_drop_run_length = 0;
				if (_extensions) {
					--_lineno;
				}
				*_out << "#line " << _lineno << '\n';
				--line_despatch::lines_suppressed();
				++line_despatch::lines_changed();
			}
		}
		if (expand) {
			if (_source_text.empty()) { _source_first_line = _logical_first_line; }
			_source_last_line = _logical_lineno;
			_source_text += _text;
		} else {
			// An unresolved branch may change #line in only one permutation.
			// Anchor its incoming location before that location becomes unknown.
			// Inspect the emitted keyword: a retained #elif can become #if.
			if (options::preprocess_source() && _is_directive && if_control::parent_must_reach_line() &&
				(_text.compare(_keyword_posn,_keyword_len,TOK_IF) == 0 ||
				 _text.compare(_keyword_posn,_keyword_len,TOK_IFDEF) == 0 ||
				 _text.compare(_keyword_posn,_keyword_len,TOK_IFNDEF) == 0)) {
				write_line_marker(_logical_first_line);
			}
			write(true);
		}
	}
}

void parsed_line::flush_source()
{
	if (_source_text.empty()) { return; }
	std::string original;
	original.swap(_source_text);
	std::string expanded = original;
	std::set<std::string> suppressed;
	try {
		if (logical_line_known() && logical_file_known()) {
			expansion_base::expand_source(expanded,_source_first_line,&suppressed);
		}
	} catch (expansion_base::limit_exceeded const &) {
		warning_incomplete_expansion() << "Source macro expansion exceeds resource limit" << emit();
		expanded = original;
		suppressed.clear();
	}
	// Generated directives are ignored but still counted on skipped paths.
	// Keep the original run if hiding recursive tokens would need directives
	// inside a branch whose later #line operations are not yet known.
	if (!suppressed.empty() && if_control::uncertain_context()) {
		expanded = original;
		suppressed.clear();
	}
	// _Pragma is recognized after macro expansion. Once applies to the
	// current header, not to the translation unit receiving its contents.
	auto pragmas = pp::lex(expanded,!options::plaintext());
	bool uncertain_pragmas = false;
	for (auto const & token : pragmas) {
		if (token.type != pp::kind::identifier) { continue; }
		auto macro = symbol::lookup(token.text);
		uncertain_pragmas = uncertain_pragmas || macro->uncertain();
		// An opaque call can leave a pragma alias unexpanded in its argument.
		// Preserve the header identity until the compiler can decide whether
		// that argument is evaluated or discarded.
		if (source_inclusion::in_header() && macro->defn() &&
			source_inclusion::may_expand_pragma(*macro->defn())) {
			source_inclusion::defer_current();
		}
	}
	std::vector<std::pair<std::size_t,std::size_t>> once_operations;
	for (std::size_t i = 0; i + 3 < pragmas.size(); ++i) {
		if (pragmas[i].text != "_Pragma" || pragmas[i + 1].text != "(" ||
			(pragmas[i + 2].text != "\"once\"" && pragmas[i + 2].text != "L\"once\"") ||
			pragmas[i + 3].text != ")") { continue; }
		if (!if_control::must_reach_line() || uncertain_pragmas) {
			if (source_inclusion::in_header()) { source_inclusion::defer_current(); }
		} else {
			source_inclusion::once();
			if (source_inclusion::in_header()) {
				once_operations.emplace_back(pragmas[i].first,pragmas[i + 3].last);
			}
		}
		i += 3;
	}
	for (auto i = once_operations.rbegin(); i != once_operations.rend(); ++i) {
		auto newlines = std::count(expanded.begin() + i->first,expanded.begin() + i->second,'\n');
		expanded.replace(i->first,i->second - i->first," " + std::string(newlines,'\n'));
	}
	// A removed directive must not join a bare macro name with a following
	// parenthesis. Opaque runs and resource-limit fallbacks need this too.
	bool boundary = false;
	bool deferred = !logical_line_known() || !logical_file_known();
	auto sequence = pp::lex(expanded,!options::plaintext());
	for (std::size_t i = 0; i < sequence.size(); ++i) {
		if (sequence[i].type != pp::kind::identifier) { continue; }
		auto macro = symbol::lookup(sequence[i].text);
		bool call = i + 1 < sequence.size() && sequence[i + 1].text == "(";
		deferred = deferred || macro->uncertain() || macro->defn() || call;
	}
	if (!sequence.empty() && sequence.back().type == pp::kind::identifier) {
		auto macro = symbol::lookup(sequence.back().text);
		boundary = macro->uncertain() || bool(macro->defn());
	}
	if (expanded != original) { ++line_despatch::lines_changed(); }
	// A subsequent compiler run has no token hide sets. Temporarily remove
	// only macros whose remaining tokens were suppressed during this rescan.
	// Keep their definitions available for later unresolved configurations.
	for (auto const & name : suppressed) { *_out << "#undef " << name << '\n'; }
	if ((!suppressed.empty() || deferred) && !if_control::uncertain_context()) {
		write_line_marker(_source_first_line);
	}
	*_out << expanded;
	if (!suppressed.empty() || boundary) {
		if (if_control::uncertain_context()) {
			_source_boundary_pending = true;
			return;
		}
		if (expanded.empty() || expanded.back() != '\n') { *_out << '\n'; }
		for (auto const & name : suppressed) {
			symbol::locator macro(name);
			*_out << "#define " << name << macro->parameters().str()
				<< ' ' << *macro->defn() << '\n';
		}
		if (logical_line_known() && !if_control::uncertain_context()) {
			write_line_marker(_source_last_line + 1);
		} else {
			// This barrier adds no presumed lines, even when an unresolved #line
			// controls the location. Dropped physical lines are retained below.
			*_out << "#line __LINE__\n";
		}
	}
}

void parsed_line::drop()
{
	flush_source();
	if (options::get_command() == CMD_SOURCE
		|| options::get_command() == CMD_SPIN) {
		if (_source_boundary_pending) {
			// Reuse the removed directive's physical line for the barrier.
			// Adding a new line would shift a skipped branch's presumed location.
			*_out << "#\n" << std::string(_lineno - _first_line,'\n');
			_source_boundary_pending = false;
			line_despatch::lines_changed() += _lineno - _first_line + 1;
		} else if (options::preprocess_source() && (!logical_line_known() || if_control::uncertain_context()) &&
				options::get_discard_policy() == discard_policy::drop) {
			*_out << std::string(_lineno - _first_line + 1,'\n');
			line_despatch::lines_changed() += _lineno - _first_line + 1;
		} else {
			write(false);
		}
		if (options::line_directives() && !options::preprocess_source()) {
			++_drop_run_length;
		}
		if (_line_marker_pending) {
			_line_marker_pending = false;
			write_line_marker(_logical_lineno + 1);
		}
	}
}

/// \cond NO_DOXYGEN

// EOF
