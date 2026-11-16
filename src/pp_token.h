#ifndef PP_TOKEN_H
#define PP_TOKEN_H

#include <set>
#include <string>
#include <vector>
#include <utility>

// Preprocessing tokens, before conversion to C/C++ language tokens. Whitespace
// belongs to the following token; placemarkers exist only during substitution.
namespace pp {
enum class kind { identifier, number, literal, punctuation, placemarker };

struct token {
	std::string text;
	kind type = kind::punctuation;
	bool space = false;
	bool paste = false;
	std::size_t first = 0, last = 0;
	std::size_t parameter = std::string::npos;
	std::set<std::string> hide;

	token() = default;
	token(std::string spelling, kind k, bool spaced = false)
	: text(std::move(spelling)), type(k), space(spaced) {}
};

using tokens = std::vector<token>;
tokens lex(std::string const & text, bool comments = true);
using source_map = std::vector<std::pair<std::size_t,std::size_t>>;
std::string spell(tokens const & sequence, source_map * origins = nullptr);
std::string stringify(tokens const & sequence);
bool is_hash(token const & tok);
bool is_paste(token const & tok);
bool feature_query(std::string const & name);
}
#endif
