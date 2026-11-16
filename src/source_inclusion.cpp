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
#include <set>
#include <sstream>
#include <vector>

namespace {

std::set<std::string> once_headers;
std::set<std::string> deferred_headers;
unsigned include_depth = 0;
unsigned const max_include_depth = 200;

struct deferred_include {};

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
	deferred_headers.clear();
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

void source_inclusion::retain(std::string const & filename)
{
	std::string path = find_header(filename);
	if (path.empty()) {
		// The file might not be needed in the selected downstream permutation.
		// Its original containing directory is nevertheless required to search.
		defer_current();
		return;
	}
	// Header names are not string literals: backslashes remain literal path
	// characters rather than acquiring C escape-sequence interpretation.
	std::string const replacement = "#include \"" + path + "\"\n";
	if (replacement != line_despatch::cur_line().str()) {
		line_despatch::cur_line().replace(replacement);
		++line_despatch::lines_changed();
	}
}

void source_inclusion::once()
{
	std::string const filename = io::in_file_name();
	if (filename != io::_stdin_name_) {
		once_headers.insert(fs::real_path(filename));
	}
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
	if (deferred_headers.count(identity)) {
		retain(filename);
		return false;
	}
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
	// An uncertain pragma-once operation must continue to refer to the real
	// header. Buffer its output and macro effects so that we can retain that
	// include without emitting any partially transformed header contents.
	symbol::configuration const definitions = symbol::save_configuration();
	auto const saved_once = once_headers;
	unsigned const saved_changes = line_despatch::lines_changed();
	unsigned const saved_suppressed = line_despatch::lines_suppressed();
	std::ostringstream output;
	try {
		nesting_scope nesting;
		io::input_scope input_state(input,path);
		io::output_scope output_state(output);
		line_despatch::input_scope line_state;
		if_control::input_scope condition_state;
		idempotence::input_scope guard_state;
		line_despatch::cur_line().write_line_marker(1);
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
		line_despatch::lines_changed() = saved_changes;
		line_despatch::lines_suppressed() = saved_suppressed;
		deferred_headers.insert(identity);
		retain(filename);
		return false;
	}
	*io::output() << output.str();
	line_despatch::cur_line().schedule_line_marker();
	return true;
}
