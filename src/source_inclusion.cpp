#include "source_inclusion.h"
#include "diagnostic.h"
#include "filesys.h"
#include "idempotence.h"
#include "if_control.h"
#include "io.h"
#include "line_despatch.h"
#include "options.h"
#include "path.h"
#include "pp_token.h"
#include "symbol.h"
#include <fstream>
#include <ostream>
#include <set>
#include <streambuf>
#include <vector>

namespace {

std::set<std::string> once_headers;
std::set<std::string> possible_once_headers;
std::set<std::string> deferred_headers;
bool unread_include_seen = false;
unsigned include_depth = 0;
unsigned const max_include_depth = 200;

struct deferred_include {};

// Headers update preprocessing state, but none of their source belongs to the
// selected file's output. Avoid retaining their discarded text in memory.
struct discard_buffer : std::streambuf {
	std::streamsize xsputn(char const *, std::streamsize count) override { return count; }
	int_type overflow(int_type ch) override { return traits_type::not_eof(ch); }
};

struct nesting_scope {
	nesting_scope() { ++include_depth; }
	~nesting_scope() { --include_depth; }
};

std::string find_header(std::string const & filename)
{
	std::vector<std::string> candidates;
	if (fs::is_absolute(filename)) {
		candidates.push_back(filename);
	} else {
		path_t local(io::in_file_name() == io::_stdin_name_ ?
			fs::cwd() : io::in_file_name());
		if (io::in_file_name() != io::_stdin_name_) { local.pop_back(); }
		local += filename;
		candidates.push_back(local.str());
		for (auto const & directory : options::include_directories()) {
			path_t candidate(directory);
			candidate += filename;
			candidates.push_back(candidate.str());
		}
	}
	for (auto const & candidate : candidates) {
		if (fs::is_file(fs::obj_type(candidate))) { return candidate; }
	}
	return std::string();
}

}

void source_inclusion::reset()
{
	once_headers.clear();
	possible_once_headers.clear();
	deferred_headers.clear();
	unread_include_seen = false;
	include_depth = 0;
}

void source_inclusion::defer_current()
{
	if (include_depth) { throw deferred_include(); }
}

bool source_inclusion::in_header()
{
	return include_depth != 0;
}

bool source_inclusion::may_expand_pragma(std::string const & text)
{
	std::set<std::string> visited;
	std::vector<std::string const *> pending{&text};
	while (!pending.empty()) {
		auto current = pending.back();
		pending.pop_back();
		for (auto const & token : pp::lex(*current)) {
			if (token.type != pp::kind::identifier) { continue; }
			if (token.text == "_Pragma") { return true; }
			if (!visited.insert(token.text).second) { continue; }
			auto macro = symbol::lookup(token.text);
			// Definitions remain owned by the symbol table during this read-only walk.
			if (macro->defn()) { pending.push_back(macro->defn().get()); }
		}
	}
	return false;
}

void source_inclusion::once()
{
	std::string const filename = io::in_file_name();
	if (filename != io::_stdin_name_) {
		once_headers.insert(fs::real_path(filename));
	}
}

void source_inclusion::uncertain_once()
{
	std::string const filename = io::in_file_name();
	if (filename != io::_stdin_name_) {
		possible_once_headers.insert(fs::real_path(filename));
	}
}

void source_inclusion::uncertain_include()
{
	// An unread project header may include other headers transitively. Their
	// pragma-once identities cannot be limited to the original include name.
	unread_include_seen = true;
}

bool source_inclusion::read(std::string const & filename)
{
	std::string const path = find_header(filename);
	if (path.empty()) {
		error_include_file() << "Cannot find quoted header \"" << filename
			<< "\" relative to the including file or in the -I paths" << emit();
		return false;
	}
	// Canonical paths identify aliases for #pragma once. The spelling used to
	// open a header remains the basis for its own relative include search.
	std::string const identity = fs::real_path(path);
	if (once_headers.count(identity)) { return true; }
	if (deferred_headers.count(identity)) { return false; }
	bool const may_skip = unread_include_seen || possible_once_headers.count(identity) != 0;
	if (include_depth == max_include_depth) {
		error_include_depth() << "Maximum quoted include depth ("
			<< max_include_depth << ") exceeded while including \""
			<< path << "\"; check for an unguarded include cycle" << emit();
		return false;
	}
	std::ifstream input(path.c_str());
	if (!input) {
		error_include_file() << "Cannot open quoted header \"" << path
			<< "\" for reading" << emit();
		return false;
	}
	// An uncertain pragma-once operation can change whether later includes
	// execute. Roll back this header's macro effects rather than committing a
	// state that is valid in only some of the retained configurations.
	symbol::configuration const definitions = symbol::save_configuration();
	auto const saved_once = once_headers;
	auto const saved_possible_once = possible_once_headers;
	bool const saved_unread_include = unread_include_seen;
	unsigned const saved_changes = line_despatch::lines_changed();
	unsigned const saved_suppressed = line_despatch::lines_suppressed();
	discard_buffer discarded;
	std::ostream output(&discarded);
	try {
		nesting_scope nesting;
		io::input_scope input_state(input,path);
		io::output_scope output_state(output);
		line_despatch::input_scope line_state;
		if_control::input_scope condition_state;
		idempotence::input_scope guard_state;
		line_type type;
		while ((type = line_despatch::next()) != LT_EOF) {
			if (type == LT_DIRECTIVE_DROP) {
				line_despatch::drop();
			} else {
				if_control::transition(type);
			}
		}
	} catch (deferred_include const &) {
		symbol::restore_configuration(definitions);
		once_headers = saved_once;
		possible_once_headers = saved_possible_once;
		unread_include_seen = saved_unread_include;
		line_despatch::lines_changed() = saved_changes;
		line_despatch::lines_suppressed() = saved_suppressed;
		deferred_headers.insert(identity);
		return false;
	}
	if (may_skip) {
		// An opaque earlier call may have applied pragma once. Only facts
		// shared by skipping and executing this include are established now.
		symbol::merge_configurations({definitions,symbol::save_configuration()});
		// Nested headers likewise execute only on the non-skipped path.
		// Previously definite once flags remain definite; new ones are possible.
		possible_once_headers.insert(once_headers.begin(),once_headers.end());
		once_headers = saved_once;
	}
	line_despatch::lines_changed() = saved_changes;
	line_despatch::lines_suppressed() = saved_suppressed;
	return true;
}
