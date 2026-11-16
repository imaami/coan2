#ifndef SOURCE_INCLUSION_H
#define SOURCE_INCLUSION_H

#include <string>

/// Quoted headers form part of a source command's translation unit.
struct source_inclusion {
	/// Begin a new top-level input, clearing pragma-once state.
	static void reset();

	/// Find and process a quoted header; return true if its include is consumed.
	static bool read(std::string const & filename);

	/// Preserve an include for downstream preprocessing, resolving its path
	/// before flattening its containing header changes the search directory.
	static void retain(std::string const & filename);

	/// Fall back to the original header when an operation needs file identity.
	/// At top level the source already retains its original file identity.
	static void defer_current();

	/// Whether the current input was entered through a quoted include.
	static bool in_header();

	/// Whether text or its known macro aliases can perform a pragma operation.
	/// Used before uncertain definitions lose their replacement lists.
	static bool may_expand_pragma(std::string const & text);

	/// Record an active #pragma once for the current input file.
	static void once();
};

#endif
