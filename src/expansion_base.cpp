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
#include "expansion_base.h"
#include "unexplained_expansion.h"
#include "explained_expansion.h"
#include "diagnostic.h"
#include "options.h"
#include <algorithm>
#include <iterator>

namespace {
using pp::token;
using pp::tokens;
using pp::kind;

struct macro_expander {
	bool conditional;
	bool preview;
	expansion_base::observer observe;
	unsigned edits = 0;
	unsigned depth = 0;
	std::set<std::pair<std::size_t,std::string>> visited;

	macro_expander(bool cond, bool preview, expansion_base::observer const & observer)
	: conditional(cond), preview(preview), observe(observer) {}

	void check(tokens const & ts) const {
		std::size_t size = 0;
		for (auto const & t : ts) { size += t.text.size() + t.space; }
		if (size > expansion_base::max_expansion_size() || edits > 65536 || depth > 256) {
			throw expansion_base::limit_exceeded();
		}
	}

	static token placemarker(bool space = false) {
		return token("",kind::placemarker,space);
	}

	static void append(tokens & out, tokens value, bool space) {
		if (!value.empty()) {
			value.front().space = space;
			out.insert(out.end(),value.begin(),value.end());
		}
	}

	void paste(tokens & ts) {
		tokens out;
		for (std::size_t i = 0; i < ts.size(); ++i) {
			if (!ts[i].paste) { out.push_back(ts[i]); continue; }
			if (out.empty() || i + 1 == ts.size() || ts[i + 1].paste) {
				if (preview) { throw expansion_base::invalid_preview(); }
				error_misplaced_token_paste() << "Missing operand of ##" << emit();
				return;
			}
			auto lhs = out.back();
			auto rhs = ts[++i];
			out.pop_back();
			token result;
			if (lhs.type == kind::placemarker) { result = rhs; }
			else if (rhs.type == kind::placemarker) { result = lhs; }
			else {
				auto spelling = lhs.text + rhs.text;
				auto joined = pp::lex(spelling,false);
				if (joined.size() != 1 || joined[0].text != spelling) {
					if (preview) { throw expansion_base::invalid_preview(); }
					error_bad_token_paste() << "Pasting \"" << lhs.text << "\" and \""
						<< rhs.text << "\" does not form a preprocessing token" << emit();
					return;
				}
				result = joined[0];
				std::set_intersection(lhs.hide.begin(),lhs.hide.end(),rhs.hide.begin(),rhs.hide.end(),
					std::inserter(result.hide,result.hide.end()));
			}
			result.space = lhs.space;
			result.paste = false;
			out.push_back(std::move(result));
		}
		ts.swap(out);
	}

	struct arguments {
		std::vector<tokens> raw;
		tokens commas;
		std::vector<tokens> expanded;
		std::vector<bool> ready;
	};

	tokens const & prescan(arguments & args, std::size_t n) {
		if (!args.ready[n]) {
			args.expanded[n] = args.raw[n];
			scan(args.expanded[n],false);
			// Argument prescan has finished: its expansion contexts are no
			// longer disabled. Only an identifier actually rejected while its
			// own macro was disabled stays unavailable on subsequent rescans.
			// Retaining entire contexts here breaks deferred recursive macros.
			for (auto & t : args.expanded[n]) {
				bool unavailable = t.type == kind::identifier && t.hide.count(t.text);
				t.hide.clear();
				if (unavailable) { t.hide.insert(t.text); }
			}
			args.ready[n] = true;
		}
		return args.expanded[n];
	}

	// Substitution and ## precede rescan. In particular, ## tokens supplied
	// by arguments (or created by pasting) are never interpreted as operators.
	tokens substitute(tokens const & replacement, std::size_t begin, std::size_t end,
		arguments & args, bool variadic, bool function_like) {
		tokens out;
		for (auto i = begin; i < end; ++i) {
			auto t = replacement[i];
			bool space = t.space;
			bool quote = function_like && pp::is_hash(t);
			if (quote) {
				if (++i == end) { break; } // Definition validation diagnoses this.
				t = replacement[i];
			}
			bool adjacent_paste = (i > begin && replacement[i - 1].paste) ||
				(i + 1 < end && replacement[i + 1].paste);
			tokens value;
			if (variadic && t.text == "__VA_OPT__") {
				auto first = i + 2;
				unsigned parens = 1;
				i = first;
				for (; i < end; ++i) {
					if (replacement[i].text == "(") { ++parens; }
					else if (replacement[i].text == ")" && !--parens) { break; }
				}
				if (i == end) { break; }
				if (!prescan(args,args.raw.size() - 1).empty()) {
					value = substitute(replacement,first,i,args,variadic,function_like);
				}
				if (value.empty()) { value.push_back(placemarker()); }
			} else if (t.parameter != std::string::npos) {
				value = quote || adjacent_paste ? args.raw[t.parameter] : prescan(args,t.parameter);
				if (value.empty()) { value.push_back(placemarker()); }
			} else {
				out.push_back(t);
				continue;
			}
			if (quote) { value = tokens{token(pp::stringify(value),kind::literal)}; }
			append(out,std::move(value),space);
			check(out);
		}
		paste(out);
		return out;
	}

	std::size_t collect(tokens const & ts, std::size_t open, arguments & args) {
		unsigned parens = 0;
		args.raw.emplace_back();
		for (auto i = open + 1; i < ts.size(); ++i) {
			auto const & t = ts[i];
			if (t.text == ")") {
				if (!parens) { return i; }
				--parens;
			} else if (t.text == "(") { ++parens; }
			if (t.text == "," && !parens) { args.raw.emplace_back(); args.commas.push_back(t); }
			else { args.raw.back().push_back(t); }
		}
		if (preview) { throw expansion_base::invalid_preview(); }
		error_malformed_macro() << "Unclosed macro argument list" << emit();
		return ts.size();
	}

	void scan(tokens & ts, bool report) {
		++depth;
		if (depth > 256) { throw expansion_base::limit_exceeded(); }
		for (std::size_t i = 0; i < ts.size();) {
			auto invocation = ts[i];
			if (invocation.type != kind::identifier) { ++i; continue; }
			if (conditional && invocation.text == "defined") {
				++i;
				if (i < ts.size() && ts[i].text == "(") { ++i; }
				if (i < ts.size()) { ++i; }
				if (i < ts.size() && ts[i].text == ")") { ++i; }
				continue;
			}
			if (conditional && options::get_command() == CMD_SYMBOLS) {
				visited.emplace(invocation.first,invocation.text);
			}
			auto sym = symbol::lookup(invocation.text);
			if (conditional && pp::feature_query(invocation.text) && !sym->configured()) {
				// The target compiler's include paths, resources and attributes are
				// unknown. Keep the complete feature query opaque, even with -im.
				++i;
				if (i < ts.size() && ts[i].text == "(") {
					arguments ignored;
					i = collect(ts,i,ignored) + 1;
				}
				continue;
			}
			if ((!sym->configured() && !sym->in_progress()) || invocation.hide.count(invocation.text)) {
				++i; continue;
			}
			bool undefined_query = conditional && pp::feature_query(invocation.text) && !sym->defn();
			// -U describes a non-macro. Replacing it during argument prescan
			// changes stringification and pasting; expression evaluation handles
			// remaining identifiers. Explicitly disabled feature queries are the
			// exception and include their whole argument list in the replacement.
			if (!sym->defn() && !undefined_query) { ++i; continue; }
			bool function_like = bool(sym->parameters());
			if (function_like && (i + 1 == ts.size() || ts[i + 1].text != "(")) {
				++i; continue;
			}
			auto end = i;
			arguments args;
			std::set<std::string> hide = invocation.hide;
			if (function_like) {
				end = collect(ts,i + 1,args);
				if (end == ts.size()) { break; }
				auto const & params = sym->parameters();
				if (!params.size() && args.raw.size() == 1 && args.raw[0].empty()) { args.raw.clear(); }
				auto fixed = params.size() - params.variadic();
				if ((!params.variadic() && args.raw.size() != fixed) || args.raw.size() < fixed) {
					if (preview) { throw expansion_base::invalid_preview(); }
					error_mismatched_macro_args() << '\"' << sym->signature() << "\" takes "
						<< fixed << (params.variadic() ? " or more" : "") << " arguments; "
						<< args.raw.size() << " provided" << emit();
					break;
				}
				if (params.variadic()) {
					tokens tail;
					for (auto n = fixed; n < args.raw.size(); ++n) {
						if (n > fixed) { tail.push_back(args.commas[n - 1]); }
						tail.insert(tail.end(),args.raw[n].begin(),args.raw[n].end());
					}
					args.raw.resize(fixed);
					args.raw.push_back(std::move(tail));
				}
				args.expanded.resize(args.raw.size());
				args.ready.resize(args.raw.size(),false);
				hide.clear();
				std::set_intersection(invocation.hide.begin(),invocation.hide.end(),
					ts[end].hide.begin(),ts[end].hide.end(),std::inserter(hide,hide.end()));
			}
			hide.insert(invocation.text);
			tokens replacement;
			if (!sym->defn()) {
				if (i + 1 < ts.size() && ts[i + 1].text == "(") {
					arguments ignored;
					end = collect(ts,i + 1,ignored);
				}
				replacement.emplace_back("0",kind::number);
			} else if (sym->format()) {
				auto const & format = sym->format()->tokens();
				replacement = substitute(format,0,format.size(),args,sym->parameters().variadic(),function_like);
			}
			replacement.erase(std::remove_if(replacement.begin(),replacement.end(),
				[](token const & t) { return t.type == kind::placemarker; }),replacement.end());
			for (auto & t : replacement) {
				t.hide.insert(hide.begin(),hide.end());
			t.first = invocation.first;
			t.last = ts[end].last;
				t.parameter = std::string::npos;
				t.paste = false;
			}
			if (!replacement.empty()) { replacement.front().space = invocation.space; }
			else if (end + 1 < ts.size()) { ts[end + 1].space = ts[end + 1].space || invocation.space; }
			ts.erase(ts.begin() + i,ts.begin() + end + 1);
			ts.insert(ts.begin() + i,replacement.begin(),replacement.end());
			++edits;
			check(ts);
			if (report && observe) { observe(pp::spell(ts)); }
		}
		--depth;
	}
};
}

unsigned expansion_base::expand_text(std::string & text, bool conditional, observer const & observe, pp::source_map * origins, bool preview) {
	auto sequence = pp::lex(text,!options::plaintext());
	macro_expander expander(conditional,preview,observe);
	bool report_sources = conditional && options::get_command() == CMD_SYMBOLS;
	auto source = report_sources ? sequence : tokens();
	expander.scan(sequence,true);
	if (report_sources) {
		// Discover all source references, but expand only those visited during
		// the real rescan. Stringified, pasted and discarded arguments are
		// still visible without diagnosing calls that were never invoked.
		for (std::size_t i = 0; i < source.size(); ++i) {
			auto const & t = source[i];
			if (t.type != kind::identifier) { continue; }
			if (t.text == "defined") {
				++i;
				if (i < source.size() && source[i].text == "(") { ++i; }
				continue;
			}
			symbol::locator sym(t.text);
			std::string rest = text.substr(t.last);
			chewer<std::string> chew(!options::plaintext(),rest);
			reference ref(sym,chew);
			ref.report(expander.visited.count({t.first,t.text}) != 0);
			if (pp::feature_query(t.text) && i + 1 < source.size() && source[i + 1].text == "(") {
				macro_expander::arguments ignored;
				i = expander.collect(source,i + 1,ignored);
			}
		}
	}
	text = pp::spell(sequence,origins);
	return expander.edits;
}

std::unique_ptr<expansion_base> expansion_base::factory(bool explain, reference const & ref) {
	if (explain) { return std::unique_ptr<expansion_base>(new explained_expansion(ref)); }
	return std::unique_ptr<expansion_base>(new unexplained_expansion(ref));
}

unsigned expansion_base::max_expansion_size() { return options::max_expansion(); }
