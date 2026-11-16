/***************************************************************************
 *   Copyright (C) 2007-2014 Mike Kinghan, imk@burroingroingjoing.com      *
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
#include "directive.h"
#include "expression_parser.h"
#include "integer_constant.h"
#include "diagnostic.h"
#include "lexicon.h"
#include "io.h"
#include "hash_include.h"
#include "if_control.h"
#include "contradiction.h"
#include "canonical.h"
#include "symbol.h"
#include "idempotence.h"
#include "expansion_base.h"
#include <iostream>

/** \file directive.cpp
 *	This file implements specializations of `template struct directive<Type>`
 */

using namespace std;

/// \cond NO_DOXYGEN

template<> std::string const directive<HASH_IF>::_keyword_ = TOK_IF;
template<> std::string const directive<HASH_IFDEF>::_keyword_ = TOK_IFDEF;
template<> std::string const directive<HASH_IFNDEF>::_keyword_  = TOK_IFNDEF;
template<> std::string const directive<HASH_ELSE>::_keyword_  = TOK_ELSE;
template<> std::string const directive<HASH_ELIF>::_keyword_  = TOK_ELIF;
template<> std::string const directive<HASH_ELIFDEF>::_keyword_  = TOK_ELIFDEF;
template<> std::string const directive<HASH_ELIFNDEF>::_keyword_  = TOK_ELIFNDEF;
template<> std::string const directive<HASH_ENDIF>::_keyword_  = TOK_ENDIF;
template<> std::string const directive<HASH_DEFINE>::_keyword_  = TOK_DEFINE;
template<> std::string const directive<HASH_UNDEF>::_keyword_  = TOK_UNDEF;
template<> std::string const directive<HASH_INCLUDE>::_keyword_  = TOK_INCLUDE;
template<> std::string const directive<HASH_EMBED>::_keyword_  = TOK_EMBED;
template<> std::string const directive<HASH_PRAGMA>::_keyword_  = TOK_PRAGMA;
template<> std::string const directive<HASH_WARNING>::_keyword_ = TOK_WARNING;
template<> std::string const directive<HASH_ERROR>::_keyword_  = TOK_ERROR;
template<> std::string const directive<HASH_LINE>::_keyword_  = TOK_LINE;

template<>
map<string,bool> directive<HASH_IF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_IFDEF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_IFNDEF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ELSE>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ELIF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ELIFDEF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ELIFNDEF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ENDIF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_DEFINE>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_UNDEF>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_INCLUDE>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_EMBED>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_PRAGMA>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_WARNING>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_ERROR>::_directives_tab_ = map<string,bool>();
template<>
map<string,bool> directive<HASH_LINE>::_directives_tab_ = map<string,bool>();

vector<directive_base::evaluator> directive_base::_evaluator_tab_ = {
	&directive<HASH_UNKNOWN>::eval,
	&directive<HASH_IF>::eval,
	&directive<HASH_IFDEF>::eval,
	&directive<HASH_IFNDEF>::eval,
	&directive<HASH_ELSE>::eval,
	&directive<HASH_ELIF>::eval,
	&directive<HASH_ELIFDEF>::eval,
	&directive<HASH_ELIFNDEF>::eval,
	&directive<HASH_ENDIF>::eval,
	&directive<HASH_DEFINE>::eval,
	&directive<HASH_UNDEF>::eval,
	&directive<HASH_INCLUDE>::eval,
	&directive<HASH_EMBED>::eval,
	&directive<HASH_PRAGMA>::eval,
	&directive<HASH_WARNING>::eval,
	&directive<HASH_ERROR>::eval,
	&directive<HASH_LINE>::eval
};
map<string, directive_type>
directive_base::_keyword_to_type_map_ = {
	{	string(TOK_IF),         	HASH_IF },
	{	string(TOK_IFDEF),      	HASH_IFDEF },
	{	string(TOK_IFNDEF),     	HASH_IFNDEF},
	{	string(TOK_ELSE),       	HASH_ELSE },
	{	string(TOK_ELIF),       	HASH_ELIF },
	{	string(TOK_ELIFDEF),    	HASH_ELIFDEF },
	{	string(TOK_ELIFNDEF),   	HASH_ELIFNDEF },
	{	string(TOK_ENDIF),      	HASH_ENDIF },
	{	string(TOK_DEFINE),     	HASH_DEFINE },
	{	string(TOK_UNDEF),      	HASH_UNDEF },
	{	string(TOK_INCLUDE),    	HASH_INCLUDE },
	{	string(TOK_EMBED),      	HASH_EMBED },
	{	string(TOK_PRAGMA),     	HASH_PRAGMA },
	{ string(TOK_WARNING), HASH_WARNING },
	{	string(TOK_ERROR),      	HASH_ERROR },
	{	string(TOK_LINE),			HASH_LINE }
};

directive_type directive_base::get_type(std::string const & keyword)
{
	auto where = _keyword_to_type_map_.find(keyword);
	if (where == _keyword_to_type_map_.end()) {
		warning_unknown_directive() << "Unknown directive #"
		                            << keyword << emit();
		return HASH_UNKNOWN;
	}
	return where->second;
}

void directive_base::report(std::string const & keyword,
                            std::string const & arg)
{
	if (!line_despatch::cur_line().reportable() ||
			options::get_command() == CMD_SYMBOLS) {
		return;
	}
	std::cout << '#' << keyword << ' ' << arg;
	if (options::list_location()) {
		std::cout << ": " << io::in_file_name()
			<< '(' << line_despatch::cur_line().num() << ')';
	}
	std::cout << '\n';
}

void directive_base::report(bool seen,
                            std::string const & keyword,
                            std::string const & arg)
{
	if (!seen) {
		report(keyword,arg);
	} else if (!options::list_only_once()) {
		report(keyword,arg);
	}
}

line_type
directive_base::eval_ifdef_or_ifndef(
    directive_type type,
    chewer<parse_buffer> & chew)
{
	chew(greyspace);
	symbol::locator sloc(chew);
	chew(greyspace);
	line_type retval = LT_IF;
	if (sloc->configured()) {
		if (sloc->defined()) {
			/* symbol is -Ded*/
			retval = type == HASH_IFDEF ?
			         LT_TRUE : LT_FALSE;
		} else {
			/* symbol is -Ued*/
			retval = type == HASH_IFNDEF ?
			         LT_TRUE : LT_FALSE;
		}
	} else {
	    idempotence::at_symbol(sloc->id());
		if (options::implicit()) {
			retval = type == HASH_IFDEF ?
			         LT_FALSE : LT_TRUE;
		}
	}
	sloc->report();
	return retval;
}

line_type directive_base::eval(directive_type type, chewer<parse_buffer> & chew)
{
	if (if_control::cannot_reach_line()) {
		contradiction::forget();
	} else if (type != HASH_UNDEF && type != HASH_DEFINE) {
		contradiction::flush();
	}
	chew(greyspace);
	line_despatch::cur_line().set_directive_type(type);
	bool source = options::get_command() == CMD_SOURCE || options::get_command() == CMD_SPIN;
	bool is_elif = type == HASH_ELIF || type == HASH_ELIFDEF || type == HASH_ELIFNDEF;
	bool is_if = type == HASH_IF || type == HASH_IFDEF || type == HASH_IFNDEF;
	if (source && ((is_elif && !if_control::can_test_elif()) ||
		(!is_elif && type != HASH_ELSE && type != HASH_ENDIF && if_control::cannot_reach_line()))) {
		canonical<string> ignored(chew);
		idempotence::at_start();
		if (is_elif) { return LT_ELIF; }
		if (is_if) { return LT_IF; }
		return LT_DIRECTIVE_DROP;
	}
	line_type retval = _evaluator_tab_[type](chew);
	chew(greyspace);
	if (chew) {
		if (retval != LT_PLAIN &&
			type != HASH_IF && type != HASH_ELIF) {
			string pretty = line_despatch::pretty();
			warning_garbage_after_directive() <<
				"Superfluous characters after directive \""
				<< pretty << '\"' << emit();
		}
	}
	return retval;
}

void directive_base::erase_all() {
	auto command = options::get_command();
	if (command == CMD_DIRECTIVES) {
		directive<HASH_WARNING>::_directives_tab_.clear();
		directive<HASH_EMBED>::_directives_tab_.clear();
	}
	if (command == CMD_INCLUDES || command == CMD_DIRECTIVES) {
		directive<HASH_INCLUDE>::_directives_tab_.clear();
	}
	if (command == CMD_PRAGMAS || command == CMD_DIRECTIVES) {
		directive<HASH_PRAGMA>::_directives_tab_.clear();
	}
	if (command == CMD_DEFS || command == CMD_DIRECTIVES) {
		directive<HASH_DEFINE>::_directives_tab_.clear();
		directive<HASH_UNDEF>::_directives_tab_.clear();
	}
	if (command == CMD_ERRORS || command == CMD_DIRECTIVES) {
		directive<HASH_ERROR>::_directives_tab_.clear();
	}
	if (command == CMD_LINES || command == CMD_DIRECTIVES) {
		directive<HASH_LINE>::_directives_tab_.clear();
	}
}

template<>
line_type directive<HASH_UNKNOWN>::eval(chewer<parse_buffer> &)
{
    idempotence::at_start();
	return LT_PLAIN;
}


template<>
line_type directive<HASH_IF>::eval(chewer<parse_buffer> & chew)
{
	line_type lineval = LT_IF;
	chew(greyspace);
	if (!chew) {
		error_if_without_cond() << "#if/elif has no argument." << emit();
	}
	if (!options::eval_wip()) {
		size_t mark = size_t(chew);
		integer i = integer_constant::read_numeral(chew);
		if (i.type() == INT_INT &&
				size_t(chew) == mark + 1 && (i.raw() == 1 || i.raw() == 0)) {
			chew(greyspace);
			if (!chew) {
				return lineval;
			}
		}
		chew = mark;
	}
	idempotence::at_if();
	auto start = size_t(chew);
	std::string expression = canonical<std::string>(chew);
	std::string original = expression;
	pp::source_map origins;
	unsigned edits = 0;
	try {
		edits = expansion_base::expand_text(expression,true,expansion_base::observer(),&origins);
	} catch (expansion_base::limit_exceeded const &) {
		warning_incomplete_expansion() << "Macro expansion exceeds resource limit" << emit();
		return LT_IF;
	}
	if (expression.empty()) {
		error_if_without_cond() << "#if/elif expands to an empty expression" << emit();
	}
	// Expand before parsing operators: a macro is a token sequence, not an
	// implicitly parenthesized expression. Keep the source spelling if possible.
	parse_buffer expanded(chew.buf().substr(0,start) + expression + '\n');
	chewer<parse_buffer> expanded_chew(!options::plaintext(),expanded,start);
	chew = start;
	chewer<parse_buffer> & input = edits ? expanded_chew : chew;
	expression_parser<parse_buffer> ep(input,nullptr,true);
	if (edits) { chew = chew.buf().size(); }
	evaluation ev = ep.result();
	if (ev.resolved()) {
		lineval = ev.is_true() ? LT_TRUE : LT_FALSE;
	}
	if (ep.is_simplified() && lineval == LT_IF) {
		std::string simplified;
		if (!edits) { simplified = ep.simplified(); }
		else {
			// Rescanning can consume source following a replacement: e.g.
			// A -> 1 + F followed by (X). These spans overlap, so restoring
			// them independently would duplicate A. Keep the original line.
			std::size_t previous_end = 0;
			pp::source_map::value_type previous(0,0);
			for (auto const & origin : origins) {
				if (origin == previous) { continue; }
				if (origin.first < previous_end) { return lineval; }
				previous = origin;
				previous_end = origin.second;
			}
			simplified = chew.buf().substr(0,start);
			std::size_t last_source_end = 0;
			bool first = true;
			for (std::size_t i = 0; i < origins.size();) {
				auto origin = origins[i];
				auto begin = i;
				while (i < origins.size() && origins[i] == origin) { ++i; }
				// spell() associates an inserted separator with the following
				// token. Removing that separator does not partially remove it.
				while (begin < i && isspace(static_cast<unsigned char>(expression[begin]))) { ++begin; }
				std::size_t retained = 0;
				for (auto pos = begin; pos < i; ++pos) { retained += !ep.deleted(start + pos); }
				if (!retained) { continue; }
				// A partial expansion can contain recursion-suppressed names.
				// Emitting it as source would make those names expandable again.
				if (retained != i - begin) { return lineval; }
				if ((!first && origin.first != last_source_end) ||
					(first && !simplified.empty() && !isspace(static_cast<unsigned char>(simplified.back())))) {
					simplified += ' ';
				}
				simplified += original.substr(origin.first,origin.second - origin.first);
				last_source_end = origin.second;
				first = false;
			}
			simplified += '\n';
		}
		line_despatch::cur_line().replace(simplified);
		line_despatch::cur_line().set_simplified(true);
	}
	return lineval;
}

template<>
line_type directive<HASH_IFDEF>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	return eval_ifdef_or_ifndef(_type_,chew);
}

template<>
line_type directive<HASH_IFNDEF>::eval(chewer<parse_buffer> & chew)
{
	idempotence::at_ifndef();
	return eval_ifdef_or_ifndef(_type_,chew);
}

template<>
line_type directive<HASH_ELSE>::eval(chewer<parse_buffer> &)
{
    idempotence::at_start();
	return LT_ELSE;
}

template<>
line_type directive<HASH_ELIF>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	auto result = eval_body(HASH_IF,chew);
	return static_cast<line_type>(result - LT_IF + LT_ELIF);
}

template<>
line_type directive<HASH_ELIFDEF>::eval(chewer<parse_buffer> & chew)
{
	idempotence::at_start();
	return static_cast<line_type>(eval_ifdef_or_ifndef(HASH_IFDEF,chew)
		- LT_IF + LT_ELIF);
}

template<>
line_type directive<HASH_ELIFNDEF>::eval(chewer<parse_buffer> & chew)
{
	idempotence::at_start();
	return static_cast<line_type>(eval_ifdef_or_ifndef(HASH_IFNDEF,chew)
		- LT_IF + LT_ELIF);
}

template<>
line_type directive<HASH_ENDIF>::eval(chewer<parse_buffer> &)
{
    idempotence::at_start();
	return LT_ENDIF;
}

template<>
line_type directive<HASH_DEFINE>::eval(chewer<parse_buffer> & chew)
{
	line_type retval = if_control::cannot_reach_line() ?
	                   LT_DIRECTIVE_DROP : LT_DIRECTIVE_KEEP;
	chew(greyspace);
	symbol::locator sloc(chew);
	string definition;
	formal_parameter_list macro_params;
	chew(continuation);
	if (chew) {
		if (*chew == '(') {
			macro_params.read(chew);
			if (!macro_params.well_formed()) {
				error_malformed_macro()
				<< "Malformed macro parameter for symbol \""
				<< sloc.id() << "\"" << emit();
				return LT_DIRECTIVE_KEEP;
			}
		}
		definition = canonical<string>(chew);
	}
	if (line_despatch::cur_line().reportable()) {
		string arg = sloc.id();
		if (definition.length()) {
			arg += ' ';
			arg += definition;
		}
		directive<HASH_DEFINE>(arg).report();
	}
	if (retval == LT_DIRECTIVE_KEEP) {
        idempotence::at_define();
        retval = sloc->digest_transient_define(macro_params,definition);
	}
	sloc->report();
	diagnostic_base::discard(severity::error);
	return retval;
}

template<>
line_type directive<HASH_UNDEF>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	line_type retval = if_control::cannot_reach_line() ?
	                   LT_DIRECTIVE_DROP : LT_DIRECTIVE_KEEP;

	chew(greyspace);
	symbol::locator sloc(chew);
	directive<HASH_UNDEF>(sloc.id()).report();
	if (retval == LT_DIRECTIVE_KEEP ) {
		retval = sloc->digest_transient_undef();
	}
	if (!contradiction::pending()) {
        sloc->report();
	}
	chew(greyspace);
	return retval;
}

template<>
line_type directive<HASH_INCLUDE>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	line_type retval = if_control::cannot_reach_line() ?
	                   LT_DIRECTIVE_DROP : LT_DIRECTIVE_KEEP;
	chew(greyspace);
	if (!chew) {
		warning_no_argument() << "#include has no argument" << emit();
	} else {
		hash_include hi(chew);
		chew(greyspace);
		if (retval == LT_DIRECTIVE_KEEP && !hi.valid()) {
			warning_invalid_include()
				<< "#include expects <FILENAME> or \"FILENAME\""
				<< emit();
		}
		// filename() expands and discovers all symbolic-argument references.
		hi.filename();
		hi.report();
	}
	return retval;
}

template<>
line_type directive<HASH_EMBED>::eval(chewer<parse_buffer> & chew)
{
	idempotence::at_start();
	if (line_despatch::cur_line().reportable()) {
		string str = canonical<string>(chew);
		directive<HASH_EMBED>(str).report();
	}
	return LT_PLAIN;
}

template<>
line_type directive<HASH_WARNING>::eval(chewer<parse_buffer> & chew)
{
	idempotence::at_start();
	if (line_despatch::cur_line().reportable()) {
		directive<HASH_WARNING>(canonical<string>(chew)).report();
	}
	return LT_PLAIN;
}

template<>
line_type directive<HASH_PRAGMA>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	if (line_despatch::cur_line().reportable()) {
		string str = canonical<string>(chew);
		directive<HASH_PRAGMA>(str).report();
	}
	return LT_PLAIN;
}

template<>
line_type directive<HASH_ERROR>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	if (line_despatch::cur_line().reportable()) {
		string str = canonical<string>(chew);
		directive<HASH_ERROR>(str).report();
	}
    if (if_control::must_reach_line()) {
        warning_unconditional_error_directive() <<
            "An #error directive must be reached" << emit();
    }
	return LT_PLAIN;
}

template<>
line_type directive<HASH_LINE>::eval(chewer<parse_buffer> & chew)
{
    idempotence::at_start();
	chew(greyspace);
	if (!chew) {
		warning_no_argument() << "#line has no argument." << emit();
		return LT_PLAIN;
	}
	string str = canonical<string>(chew);
	if (line_despatch::cur_line().reportable()) {
		directive<HASH_LINE>(str).report();
	}
	try { expansion_base::expand_text(str,true); }
	catch (expansion_base::limit_exceeded const &) {
		warning_incomplete_expansion() << "#line macro expansion exceeds resource limit" << emit();
		return LT_PLAIN;
	}
	// The optional filename is a string literal, not part of the line-number
	// expression. Keep coan's existing acceptance of numeric expressions.
	auto sequence = pp::lex(str,false);
	if (!sequence.empty() && sequence.back().type == pp::kind::literal &&
		sequence.back().text.front() == '"') {
		sequence.pop_back();
		str = pp::spell(sequence);
	}
	chewer<string> input(chew_mode::plaintext,str);
	expression_parser<string> ep(input,nullptr,true);
	evaluation ev = ep.result();
	if (!ev.resolved() || int(ev.value().raw()) < 1) {
		warning_not_a_line_number() <<
			"#line expects a line-number" << emit();
	}
	return LT_PLAIN;
}

/// \endcond NO_DOXYGEN


// EOF
