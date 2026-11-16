#ifndef PARSED_LINE_H
#define PARSED_LINE_H
#pragma once
/***************************************************************************
 *   Copyright (C) 2013 Mike Kinghan, imk@burroingroingjoing.com           *
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

/** \file parsed_line.h
 *	This file defines `struct parsed_line`
 */
#include "parse_buffer.h"
#include "directive_type.h"

/** `struct parsed_line` is the coan parser's
 *	representation of a parsed line of input read from a file.
 */

struct parsed_line : parse_buffer
{
	/// Type of base class.
	using base_type = parse_buffer;

	/// Construct given pointers to input and output streams.
	explicit parsed_line(std::istream * in, std::ostream * out);

	~parsed_line() override {}

	/** \brief Get a pointer to the input stream from which this `parsed_line`
	 *	reads.
	 */
	std::istream * file() {
		return _in;
	}

	/// Get the greatest source line number spanned by this line.
	unsigned num() const {
		return _lineno;
	}

	/// Presumed source location after active #line directives.
	unsigned logical_num() const { return _logical_lineno; }
	unsigned logical_first_line() const { return _logical_first_line; }
	std::string const & logical_file_name() const { return _logical_filename; }
	bool logical_line_known() const { return _logical_line_known; }
	bool logical_file_known() const { return _logical_file_known; }
	void forget_logical_location(bool filename = true) {
		_logical_line_known = false;
		if (filename) { _logical_file_known = false; }
	}
	void set_logical_location(unsigned next_line, std::string const * filename = nullptr) {
		_logical_lineno = next_line - 1;
		_logical_line_known = true;
		if (filename) {
			_logical_filename = *filename;
			_logical_file_known = true;
		}
	}
	void write_line_marker(unsigned logical_line);
	void schedule_line_marker() { _line_marker_pending = true; }

	/// Get the number of linefeeds embedded in this line
	unsigned extensions() const {
		return _extensions;
	}

	/// \brief Record the offset and length of a directive keyword in the line
	void mark_keyword(size_t off, size_t len) {
		_is_directive = true;
		_keyword_posn = off;
		_keyword_len = len;
	}

	/** \brief Try to read a line of input.
	 *   \return \e true iff a line is read.
	 */
	bool get();

	/// Replace the line with another string.
	void replace(std::string const & replacement) {
		_text = replacement;
	}

	/** \brief Replace the directive in the line
     *
	 *	\tparam DirectiveType The type of directive that is to
	 *	replace the existing one.
     *
	 *  The member function edits the line to subsitute a directive of
	 *	type `DirectiveType` for the existing directive in the line
	 *	to rectify the \c #if-logic of the file in the light of
	 *	lines that have been dropped.
     *
	 *	If `DirectiveType` denotes a type of directive that
	 *	has no argument then the existing directive keyword is
	 *	replaced the keyword corresponding to `DirectiveType`
	 *	and the line is then terminated with a linefeed.
     *
	 *	Otherwise the existing directive keyord is simply
	 *	replaced the keyword corresponding to `DirectiveType`.
	 */
	template<directive_type DirectiveType>
	void keyword_edit();

	/// Classifiy the line as simplified or not
	void set_simplified(bool value = true) {
		_simplified = value;
	}

	/// Say whether the line has been simplified.
	bool is_simplified() const {
		return _simplified;
	}

	/// Is the line reportable for the operative command
	bool reportable() {
		return _reportable;
	}

	/// Set the directive type of the line
	void set_directive_type(directive_type dtype) {
		_dtype = dtype;
		set_reportable();
	}

	/// Get Set the directive type of the line
	directive_type get_directive_type() const {
		return _dtype;
	}

	/// Output the line
	void output();

	/// Drop the line
	void drop();

	// Expand consecutive ordinary lines together: a macro name, its opening
	// parenthesis and its arguments may occur on different physical lines.
	void flush_source();

	/// Get a reference to the line's indentation amount.
	unsigned & indent() {
		return _indent;
	}

protected:

    /// Enumeration of reachability statuses of the line
    enum class reach_status {
        may, ///< May be reached consistent with the configuration
        must, /// Must be reached under the configuration
        cant /// Cannot be reached under the configuration
    };

    /// Say whether a line extension is pending at an offset in the line.
	size_t extension_pending(size_t off) const override {
		if (at(off) == '\\') {
			auto advance = eol(++off);
			if (advance && overshoot(advance + off)) {
				return ++advance;
			}
		}
		return 0;
	}

    /// \brief Try to extend the line past a new-line sequence.
    /// \param skip The length of the newline-sequence at the scanning position.
    /// \return The net length of the extension.
	size_t extend(size_t skip) override;

	/// Try to read another line of input, returning the number of bytes read.
	size_t extend() override;

	/** \brief Convert the directive in the line into a
	 *	another one that has no argument.
     *
	 *  \param keyword The keyword of the replacement directive.
     *
	 *	The existing directive keyword in the line, if any,
	 *	is replaced the `keyword` and the line is then terminated
	 *	with a linefeed.
	 */
	void keyword_lop(std::string const & keyword);

	/** \brief Replace the directive keyword in the line with another
	 *	one.
     *
	 *	\param directive The keyword of the replacement directive.
	 */
	void keyword_swap(std::string const & directive);

    /// Output the line
	void write();
	/// Output the line commented out
	void write_commented_out();
	/// Output the line fast, when possible
	void write_fast();
	/// Output the line slowely, when necessary
	void write_slow();
	/** \brief Output the line or else discard it according to options
	 *  \param keep Whether to retain or drop the line
	 *
	 *  `keep` is negated if `options::complement()` is true.
	 *  Then, if keep is true, the line is output. Otherwise
	 *  a blank line or a commented-out line, or nothing, is
	 *  output depending on the specified or default `--discard` option.
	 */
	void write(bool keep);

	/** \brief Record whether the line is reportable for the operative
     *command
	 */
	void set_reportable();

	/// The number of linefeeds embedded in the line
	unsigned _extensions = 0;
	/// The greatest source line number spanned by this line
	unsigned _lineno = 0;
	unsigned _first_line = 1;
	unsigned _logical_lineno = 0;
	unsigned _logical_first_line = 1;
	std::string _logical_filename;
	bool _logical_line_known = true;
	bool _logical_file_known = true;
	unsigned _source_first_line = 1;
	unsigned _source_last_line = 0;
	std::string _source_text;
	bool _is_directive = false;
	bool _line_marker_pending = false;
	bool _source_boundary_pending = false;
	/// The input stream from which this line is read
	std::istream * _in;
	/// The output stream to which this line is written
	std::ostream * _out;
	/// Offset to directive keyword, if any.
	size_t	_keyword_posn = 0;
	/// Length of directive keyword, if any
	size_t _keyword_len = 0;
	/// The directive type of this line.
	directive_type _dtype = HASH_UNKNOWN;
	/// Is this line reportable?
	bool _reportable = false;
	/// Has the line been simplified?
	bool _simplified = false;
	/// Count of contiguous lines that are dropped together.
	unsigned _drop_run_length = 0;
	/// Amount by which the line is indented
	unsigned _indent = 0;
};

#endif //EOF
