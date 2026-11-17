#ifndef SOURCE_INCLUSION_H
#define SOURCE_INCLUSION_H

#include <string>

/// Quoted headers contribute preprocessing state without contributing output.
struct source_inclusion {
	/// Begin a new top-level input, clearing pragma-once state.
	static void reset();

	/// Find and process a quoted header; return true if its macro state is known.
	/// The calling file's include directive remains part of its source output.
	static bool read(std::string const & filename);

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

	/// An opaque macro call may apply pragma once to the current input file.
	/// A later include merges its skipped and executed macro configurations.
	static void uncertain_once();

	/// An unread quoted include may have applied pragma once transitively.
	static void uncertain_include();
};

#endif
