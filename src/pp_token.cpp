#include "pp_token.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace pp {
namespace {
bool start(unsigned char ch) {
	return std::isalpha(ch) || ch == '_' || ch >= 0x80;
}
bool digit(unsigned char ch) { return std::isdigit(ch); }
bool part(unsigned char ch) { return start(ch) || digit(ch); }

std::size_t ucn_end(std::string const & s, std::size_t i) {
	if (i == s.size() || s[i] != '\\' || i + 1 == s.size() ||
		(s[i + 1] != 'u' && s[i + 1] != 'U')) {
		return i;
	}
	std::size_t n = s[i + 1] == 'u' ? 4 : 8;
	std::size_t j = i + 2;
	if (j < s.size() && s[j] == '{') {
		auto end = s.find('}',j + 1);
		return end == std::string::npos ? i : end + 1;
	}
	for (std::size_t k = 0; k < n; ++k, ++j) {
		if (j == s.size() || !std::isxdigit(static_cast<unsigned char>(s[j]))) {
			return i;
		}
	}
	return j;
}
}

bool is_hash(token const & tok) { return tok.text == "#" || tok.text == "%:"; }
bool is_paste(token const & tok) { return tok.text == "##" || tok.text == "%:%:"; }
bool feature_query(std::string const & name) {
	return name == "__has_include" || name == "__has_include_next" || name == "__has_embed" ||
		name == "__has_c_attribute";
}

tokens lex(std::string const & input, bool comments) {
	// Translation phase 2 precedes token recognition (including comments).
	std::string s;
	std::vector<std::size_t> positions;
	for (std::size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			if (input[i + 1] == '\n') { ++i; continue; }
			if (input[i + 1] == '\r' && i + 2 < input.size() &&
				input[i + 2] == '\n') { i += 2; continue; }
		}
		positions.push_back(i);
		s += input[i];
	}
	positions.push_back(input.size());
	tokens out;
	bool space = false;
	for (std::size_t i = 0; i < s.size();) {
		if (std::isspace(static_cast<unsigned char>(s[i]))) {
			space = true; ++i; continue;
		}
		if (comments && s.compare(i,2,"/*") == 0) {
			auto end = s.find("*/",i + 2);
			i = end == std::string::npos ? s.size() : end + 2;
			space = true; continue;
		}
		if (comments && s.compare(i,2,"//") == 0) {
			auto end = s.find('\n',i + 2);
			i = end == std::string::npos ? s.size() : end;
			space = true; continue;
		}
		auto begin = i;
		auto type = kind::punctuation;
		std::string spelling;
		std::size_t quote = i;
		if (s.compare(i,2,"u8") == 0) { quote += 2; }
		else if (s[i] == 'u' || s[i] == 'U' || s[i] == 'L') { ++quote; }
		bool raw = quote < s.size() && s[quote] == 'R';
		if (raw) { ++quote; }
		if (s[i] == '<' && out.size() >= 2 && out.back().text == "(" &&
			feature_query(out[out.size() - 2].text)) {
			type = kind::header_name;
			auto end = s.find('>',i + 1);
			i = end == std::string::npos ? s.size() : end + 1;
		} else if (quote < s.size() && (s[quote] == '"' || (!raw && s[quote] == '\''))) {
			type = kind::literal;
			if (raw) {
				// The raw literal's physical contents survive phase 2, including
				// its delimiter. Only its encoding/R prefix can be spliced.
				auto physical_quote = positions[quote];
				auto open = input.find('(',physical_quote + 1);
				if (open == std::string::npos) { i = s.size(); }
				else {
					auto close = ")" + input.substr(physical_quote + 1,open - physical_quote - 1) + '"';
					auto end = input.find(close,open + 1);
					auto physical_end = end == std::string::npos ? input.size() : end + close.size();
					spelling = s.substr(begin,quote - begin) +
						input.substr(physical_quote,physical_end - physical_quote);
					i = std::lower_bound(positions.begin(),positions.end(),physical_end) - positions.begin();
				}
			} else {
				i = quote + 1;
				while (i < s.size()) {
					auto ch = s[i++];
					if (ch == s[quote]) { break; }
					if (ch == '\\' && i < s.size()) { ++i; }
				}
			}
			// Adjacent ud-suffixes belong to the literal token, including raw
			// literals whose body has been restored to its physical spelling.
			auto suffix = i;
			if (i < s.size() && (start(s[i]) || ucn_end(s,i) != i)) {
				while (i < s.size()) {
					if (part(s[i])) { ++i; }
					else {
						auto end = ucn_end(s,i);
						if (end == i) { break; }
						i = end;
					}
				}
			}
			if (!spelling.empty()) { spelling += s.substr(suffix,i - suffix); }
		} else if (digit(s[i]) || (s[i] == '.' && i + 1 < s.size() && digit(s[i + 1]))) {
			type = kind::number;
			for (++i; i < s.size();) {
				if (part(s[i]) || s[i] == '.') { ++i; }
				else if (ucn_end(s,i) != i) { i = ucn_end(s,i); }
				else if (s[i] == '\'' && i + 1 < s.size() && part(s[i + 1])) { i += 2; }
				else if ((s[i] == '+' || s[i] == '-') &&
					std::strchr("eEpP",s[i - 1])) { ++i; }
				else { break; }
			}
		} else if (start(s[i]) || ucn_end(s,i) != i) {
			type = kind::identifier;
			while (i < s.size()) {
				if (part(s[i])) { ++i; }
				else {
					auto end = ucn_end(s,i);
					if (end == i) { break; }
					i = end;
				}
			}
		} else {
			static char const * const punctuators[] = {
				"%:%:", ">>=", "<<=", "...", "->*", "<=>", "##", "->", "++", "--",
				"<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=",
				"+=", "-=", "&=", "^=", "|=", "<:", ":>", "<%", "%>", "%:", "::", ".*"
			};
			std::size_t len = 1;
			for (auto punct : punctuators) {
				if (s.compare(i,std::strlen(punct),punct) == 0) {
					len = std::strlen(punct); break;
				}
			}
			i += len;
		}
		out.emplace_back(spelling.empty() ? s.substr(begin,i - begin) : spelling,type,space);
		out.back().first = positions[begin];
		out.back().last = positions[i];
		space = false;
	}
	return out;
}

bool needs_space(token const & left, token const & right) {
	auto joined = lex(left.text + right.text,false);
	// A third dot could complete an ellipsis even when each pair lexes
	// separately. Also never accidentally introduce a comment.
	return right.space || joined.size() != 2 || joined[0].text != left.text ||
		(left.text == "." && right.text == ".") ||
		(left.text == "/" && (right.text == "*" || right.text == "/"));
}

std::string spell(tokens const & sequence, source_map * origins) {
	if (origins) { origins->clear(); }
	std::string out;
	token const * previous = nullptr;
	for (auto const & tok : sequence) {
		if (tok.type == kind::placemarker) { continue; }
		if (previous) {
			if (needs_space(*previous,tok)) {
				out += ' ';
				if (origins) { origins->emplace_back(tok.first,tok.last); }
			}
		}
		out += tok.text;
		if (origins) { origins->insert(origins->end(),tok.text.size(),{tok.first,tok.last}); }
		previous = &tok;
	}
	return out;
}

std::string stringify(tokens const & sequence) {
	std::string out = "\"";
	bool first = true;
	for (auto const & tok : sequence) {
		if (tok.type == kind::placemarker) { continue; }
		if (!first && tok.space) { out += ' '; }
		for (char ch : tok.text) {
			if (tok.type == kind::literal && (ch == '\n' || ch == '\r')) {
				out += ch == '\n' ? "\\n" : "\\r";
				continue;
			}
			if (tok.type == kind::literal && (ch == '\\' || ch == '"')) { out += '\\'; }
			out += ch;
		}
		first = false;
	}
	out += '"';
	return out;
}
}
