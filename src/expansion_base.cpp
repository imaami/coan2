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
#include "line_despatch.h"
#include <algorithm>
#include <iterator>
#include <map>

namespace {
using pp::token;
using pp::tokens;
using pp::kind;

// Rescanning only advances through a run. Keep its untouched suffix reversed
// so replacing the next invocation never moves unrelated following tokens.
// The public token sequence remains a vector for lexing and rendering.
class token_cursor {
public:
	explicit token_cursor(tokens & input) : _input(input), _after(std::move(input)) {
		_before.reserve(_after.size());
		std::reverse(_after.begin(),_after.end());
	}
	~token_cursor() {
		advance(size());
		_input = std::move(_before);
	}
	std::size_t size() const { return _before.size() + _after.size(); }
	token const & operator[](std::size_t index) const {
		return index < _before.size() ? _before[index] : _after[size() - index - 1];
	}
	token & operator[](std::size_t index) {
		return index < _before.size() ? _before[index] : _after[size() - index - 1];
	}
	void advance(std::size_t index) {
		while (_before.size() < index) {
			_before.push_back(std::move(_after.back()));
			_after.pop_back();
		}
	}
	void replace(std::size_t first, std::size_t last, tokens replacement) {
		advance(first);
		_after.resize(_after.size() - (last - first));
		for (auto i = replacement.rbegin(); i != replacement.rend(); ++i) {
			_after.push_back(std::move(*i));
		}
	}
	tokens copy(std::size_t first, std::size_t last) const {
		tokens result;
		result.reserve(last - first);
		for (; first < last; ++first) { result.push_back((*this)[first]); }
		return result;
	}
private:
	tokens & _input;
	tokens _before;
	tokens _after;
};

struct macro_expander {
	bool conditional;
	bool preview;
	bool reporting;
	std::string const * source = nullptr;
	bool preserve_source = false;
	unsigned first_line = 1;
	std::string filename;
	bool line_known = true;
	bool file_known = true;
	bool opaque_calls = false;
	std::vector<std::size_t> line_starts;
	std::vector<std::pair<std::size_t,std::size_t>> changes;
	std::map<std::size_t,unsigned> source_edits;
	expansion_base::observer observe;
	unsigned edits = 0;
	unsigned depth = 0;
	std::set<std::pair<std::size_t,std::string>> visited;
	struct uncertain_argument {};

	macro_expander(bool cond, bool preview, expansion_base::observer const & observer, bool reporting)
	: conditional(cond), preview(preview), reporting(reporting), observe(observer) {}

	void check(tokens const & ts) const {
		auto size = pp::spell(ts).size();
		if (size > expansion_base::max_expansion_size() || (!preserve_source && edits > 65536) || depth > 256) {
			throw expansion_base::limit_exceeded();
		}
	}

	void source_context(std::string const & text, unsigned line, bool preserve = true) {
		source = &text;
		preserve_source = preserve;
		first_line = line;
		filename = line_despatch::cur_line().logical_file_name();
		line_known = line_despatch::cur_line().logical_line_known();
		file_known = line_despatch::cur_line().logical_file_known();
		line_starts.push_back(0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			if (text[i] == '\n') { line_starts.push_back(i + 1); }
		}
	}

	void changed(token_cursor const & ts, std::size_t at, std::size_t first, std::size_t last) {
		++edits;
		if (!preserve_source) { check(ts.copy(0,ts.size())); return; }
		changes.emplace_back(first,last);
		if (++source_edits[first] > 65536) { throw expansion_base::limit_exceeded(); }
		// An unrelated long source run must not consume the per-expansion limit.
		// Include siblings produced by this invocation when checking a rescan.
		auto begin = at, end = at;
		while (begin && ts[begin - 1].last > first) { --begin; }
		while (end < ts.size() && ts[end].first < last) { ++end; }
		check(ts.copy(begin,end));
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

	template<class Sequence>
	std::size_t collect(Sequence const & ts, std::size_t open, arguments & args, bool diagnose = true) {
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
		if (diagnose) {
			if (preview) { throw expansion_base::invalid_preview(); }
			error_malformed_macro() << "Unclosed macro argument list" << emit();
		}
		return ts.size();
	}

	bool separated(std::size_t begin, std::size_t end) const {
		for (auto i = begin; i < end;) {
			if ((*source)[i] == '\\' && i + 1 < end) {
				if ((*source)[i + 1] == '\n') { i += 2; continue; }
				if ((*source)[i + 1] == '\r' && i + 2 < end &&
					(*source)[i + 2] == '\n') { i += 3; continue; }
			}
			return true;
		}
		return false;
	}

	std::string render(tokens const & ts) {
		std::sort(changes.begin(),changes.end());
		std::vector<std::pair<std::size_t,std::size_t>> regions;
		for (auto range : changes) {
			if (!regions.empty() && range.first <= regions.back().second) {
				regions.back().second = std::max(regions.back().second,range.second);
			} else { regions.push_back(range); }
		}
		std::string out;
		std::size_t cursor = 0, next = 0;
		for (auto range : regions) {
			out.append(*source,cursor,range.first - cursor);
			while (next < ts.size() && ts[next].last <= range.first) { ++next; }
			auto begin = next;
			while (next < ts.size() && ts[next].first < range.second) { ++next; }
			std::string replacement = pp::spell(tokens(ts.begin() + begin,ts.begin() + next));
			auto lines = std::count(source->begin() + range.first,source->begin() + range.second,'\n');
			auto emitted_lines = std::count(replacement.begin(),replacement.end(),'\n');
			if (emitted_lines < lines) { replacement.append(lines - emitted_lines,'\n'); }
			if (begin != next) {
				if (begin && pp::needs_space(ts[begin - 1],ts[begin]) &&
					!separated(ts[begin - 1].last,range.first)) {
					replacement.insert(replacement.begin(),' ');
				}
				if (next < ts.size() && pp::needs_space(ts[next - 1],ts[next]) &&
					!separated(range.second,ts[next].first) &&
					(replacement.empty() || replacement.back() != '\n')) {
					replacement += ' ';
				}
			} else if (replacement.empty() && begin && next < ts.size() &&
				pp::needs_space(ts[begin - 1],ts[next]) &&
				!separated(ts[begin - 1].last,range.first) && !separated(range.second,ts[next].first)) {
				replacement += ' ';
			}
			out += replacement;
			cursor = range.second;
		}
		out.append(*source,cursor,std::string::npos);
		return out;
	}

	void scan(tokens & sequence, bool report) {
		token_cursor ts(sequence);
		struct depth_scope {
			unsigned & value;
			explicit depth_scope(unsigned & value) : value(value) { ++value; }
			~depth_scope() { --value; }
		} scope(depth);
		if (depth > 256) { throw expansion_base::limit_exceeded(); }
		for (std::size_t i = 0; i < ts.size();) {
			ts.advance(i);
			auto invocation = ts[i];
			if (invocation.type != kind::identifier) { ++i; continue; }
			if (conditional && invocation.text == "defined") {
				++i;
				if (i < ts.size() && ts[i].text == "(") { ++i; }
				if (i < ts.size()) { ++i; }
				if (i < ts.size() && ts[i].text == ")") { ++i; }
				continue;
			}
			if (reporting) {
				visited.emplace(invocation.first,invocation.text);
			}
			auto sym = symbol::lookup(invocation.text);
			if (sym->uncertain()) {
				if (source) { throw uncertain_argument(); }
				++i;
				if (i < ts.size() && ts[i].text == "(") {
					arguments ignored;
					i = collect(ts,i,ignored,!preserve_source) + 1;
				}
				continue;
			}
			if (source && !sym->configured() && !sym->in_progress() &&
				(invocation.text == "__LINE__" || invocation.text == "__FILE__")) {
				if (invocation.text == "__LINE__" ? !line_known : !file_known) {
					if (depth > 1) { throw uncertain_argument(); }
					++i;
					continue;
				}
				if (invocation.text == "__LINE__") {
					auto line = std::upper_bound(line_starts.begin(),line_starts.end(),invocation.first) - line_starts.begin();
					ts[i].text = std::to_string(first_line + line - 1);
					ts[i].type = kind::number;
				} else {
					ts[i].text = pp::stringify(tokens{token(filename,kind::literal)});
					ts[i].type = kind::literal;
				}
				changed(ts,i,invocation.first,invocation.last);
				++i;
				continue;
			}
			if (source && symbol::system_header_seen() &&
				!sym->configured() && !sym->in_progress() &&
				i + 1 < ts.size() && ts[i + 1].text == "(") {
				// A retained system header may supply this function-like macro.
				// Its arguments may be pasted/stringified, so they must stay raw.
				// Prescan cannot commit even a surrounding project invocation.
				if (conditional || depth > 1) { throw uncertain_argument(); }
				opaque_calls = true;
				++i;
				do {
					arguments ignored;
					auto close = collect(ts,i,ignored,false);
					i = close == ts.size() ? close : close + 1;
				} while (i < ts.size() && ts[i].text == "(");
				continue;
			}
			if (conditional && !sym->configured() && !sym->in_progress() &&
				(pp::feature_query(invocation.text) || !options::implicit())) {
				// The target compiler's include paths, resources and attributes are
				// unknown. Likewise, an unconfigured macro might discard or quote
				// its arguments. Feature queries stay opaque even with --implicit.
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
					if (end == ts.size()) { break; }
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
			auto source_last = ts[end].last;
			ts.replace(i,end + 1,std::move(replacement));
			changed(ts,i,invocation.first,source_last);
			if (report && observe) { observe(pp::spell(ts.copy(0,ts.size()))); }
		}
	}
};
}

unsigned expansion_base::expand_text(std::string & text, bool conditional,
	observer const & observe, pp::source_map * origins, bool preview,
	bool report_sources, unsigned first_line, bool * deferred) {
	if (deferred) { *deferred = false; }
	auto sequence = pp::lex(text,!options::plaintext());
	report_sources = (conditional || report_sources) && options::get_command() == CMD_SYMBOLS;
	macro_expander expander(conditional,preview,observe,report_sources);
	if (options::preprocess_source() && !preview) {
		expander.source_context(text,first_line ? first_line : line_despatch::cur_line().logical_first_line(),false);
	}
	auto source = report_sources ? sequence : tokens();
	bool incomplete = false;
	try { expander.scan(sequence,true); }
	catch (macro_expander::uncertain_argument const &) {
		if (origins) { origins->clear(); }
		if (deferred) { *deferred = true; }
		return 0;
	}
	catch (limit_exceeded const &) { incomplete = true; }
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
				if (incomplete && i < source.size() && source[i].type == kind::identifier) {
					reference ref(symbol::locator(source[i].text),argument_list());
					ref.report(false);
				}
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
	if (incomplete) { throw limit_exceeded(); }
	text = pp::spell(sequence,origins);
	return expander.edits;
}

unsigned expansion_base::expand_source(std::string & text, unsigned first_line,
	std::set<std::string> * suppressed) {
	auto sequence = pp::lex(text,!options::plaintext());
	macro_expander expander(false,false,observer(),false);
	expander.source_context(text,first_line);
	if (suppressed) { suppressed->clear(); }
	try { expander.scan(sequence,false); }
	catch (macro_expander::uncertain_argument const &) {
		// The unresolved replacement could quote/paste an argument or consume
		// following tokens. No part of this run is safe to freeze yet.
		return 0;
	}
	std::set<std::string> disabled;
	bool opaque = expander.opaque_calls;
	for (std::size_t i = 0; i < sequence.size(); ++i) {
		auto const & tok = sequence[i];
		if (tok.type != kind::identifier) { continue; }
		auto sym = symbol::lookup(tok.text);
		opaque = opaque || sym->uncertain() || (!sym->configured() &&
			((tok.text == "__LINE__" && !expander.line_known) ||
			 (tok.text == "__FILE__" && !expander.file_known)));
		if (!sym->defn()) { continue; }
		bool function_like = bool(sym->parameters());
		bool called = i + 1 < sequence.size() && sequence[i + 1].text == "(";
		// A removed following directive must not turn this formerly bare name
		// into an invocation when the result goes through another preprocessor.
		bool trailing_function = function_like && i + 1 == sequence.size();
		if ((tok.hide.count(tok.text) && (!function_like || called)) || trailing_function) {
			disabled.insert(tok.text);
		}
	}
	// Disabling a macro around an opaque call could change that call's later
	// expansion, even if it refers to the macro only indirectly. Keep the
	// original run until the unresolved configuration selects its meaning.
	if (opaque && !disabled.empty()) { return 0; }
	if (suppressed) { *suppressed = std::move(disabled); }
	text = expander.render(sequence);
	return expander.edits;
}

std::unique_ptr<expansion_base> expansion_base::factory(bool explain, reference const & ref) {
	if (explain) { return std::unique_ptr<expansion_base>(new explained_expansion(ref)); }
	return std::unique_ptr<expansion_base>(new unexplained_expansion(ref));
}

unsigned expansion_base::max_expansion_size() { return options::max_expansion(); }
