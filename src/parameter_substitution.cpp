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

#include "parameter_substitution.h"
#include "symbol.h"
#include "diagnostic.h"
#include "options.h"

namespace parameter_substitution {
namespace {
void validate(pp::tokens const & ts, std::size_t begin, std::size_t end,
	symbol const & sym, bool in_va_opt = false) {
	if (begin == end) { return; }
	if (pp::is_paste(ts[begin]) || pp::is_paste(ts[end - 1])) {
		error_misplaced_token_paste() << "##-operator cannot begin or end the replacement list of \""
			<< sym.signature() << '\"' << emit();
	}
	for (auto i = begin; i < end; ++i) {
		auto const & t = ts[i];
		if (t.text == "__VA_ARGS__" || t.text == "__VA_OPT__") {
			if (!sym.parameters().variadic()) {
				error_malformed_macro() << t.text << " requires a variadic macro: "
					<< sym.signature() << emit();
			}
		}
		if (sym.parameters() && pp::is_hash(t) &&
			(i + 1 == end || (ts[i + 1].parameter == std::string::npos &&
				ts[i + 1].text != "__VA_OPT__"))) {
			error_stringify_non_param() << "#-operator must precede a macro parameter in \""
				<< sym.signature() << '\"' << emit();
		}
		if (t.text != "__VA_OPT__") { continue; }
		if (in_va_opt || i + 1 == end || ts[i + 1].text != "(") {
			error_malformed_macro() << "Malformed or nested __VA_OPT__ in "
				<< sym.signature() << emit();
			return;
		}
		auto first = i + 2;
		unsigned depth = 1;
		i = first;
		for (; i < end; ++i) {
			if (ts[i].text == "(") { ++depth; }
			else if (ts[i].text == ")" && !--depth) { break; }
		}
		if (i == end) {
			error_malformed_macro() << "Unclosed __VA_OPT__ in "
				<< sym.signature() << emit();
			return;
		}
		validate(ts,first,i,sym,true);
	}
}
}

format::format(symbol & sym)
: _tokens(pp::lex(*sym.defn(),!options::plaintext())) {
	auto const & params = sym.parameters();
	for (auto & tok : _tokens) {
		tok.paste = pp::is_paste(tok);
		if (tok.type == pp::kind::identifier) {
			tok.parameter = params.which(tok.text);
			if (params.variadic() && tok.text == "__VA_ARGS__") {
				tok.parameter = params.size() - 1;
			}
		}
	}
	validate(_tokens,0,_tokens.size(),sym);
}
}
