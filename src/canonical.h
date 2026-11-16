#ifndef CANONICAL_H
#define CANONICAL_H
#pragma once
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

#include "chew.h"
#include "traits.h"
#include <string>

struct symbol;

/** \file canonical.h
 *	This file defines:
 *	- `struct innards::canonical_base`
 *	- `tempplate struct canonical<What>`
 */

/// A tag class for parameterizing template class `canonical<What>`
struct macro_argument {};

/// Encloses internals
namespace innards {

/// A base for classes representing canonical forms of various types.
struct canonical_base {
	/// Implicitly cast the canonical representation to string.
	operator std::string () const {
		return _canonical;
	}

protected:

	/// Canonicalizing C/C++ source?
	bool cxx() const;

	/// String storing the canonical representation of a value.
	std::string _canonical;
};

} // namespace innards

/** \brief `template class canonical<What>` encapsulates the canonical
 *    representation of values of type `What`.
 *
 *   \tparam What The type that is canonically represented.
 */
template<class What>
struct canonical : innards::canonical_base {

    /** \brief Explicitly construct from a character-sequence
     * \tparam CharSeq A character-sequence type
     * \param seq A `CharSeq`
     */
	template<class CharSeq>
	explicit canonical(CharSeq & seq) {
		canonicalize(seq);
	}

    /** \brief Explicitly construct from a `chewer<CharSeq>`
     * \tparam CharSeq A character-sequence type
     * \param chew A `chewer<CharSeq>` positioned at the offset in the
     *  associated `CharSeq` at which canonicalization is to start. The
     *  canonical form of the remainder of the `CharSeq` is constructed.
     */
	template<class CharSeq>
	explicit canonical(chewer<CharSeq> & chew) {
        static_assert(traits::is_random_access_char_sequence<CharSeq>::value,
            ">:[");
		canonicalize(chew);
	}

private:

    /** \brief Canonicalize from a `chewer<CharSeq>`
     * \tparam CharSeq A character-sequence type
     * \param chew A `chewer<CharSeq>` positioned at the offset in the
     *  associated `CharSeq` at which canonicalization is to start. The
     *  canonical form of the remainder of the `CharSeq` is canonicalized.
     */
    template<class CharSeq>
	void canonicalize(chewer<CharSeq> & chew);

    /** \brief Canonicalize from a `CharSeq>`
     * \tparam CharSeq A character-sequence type
     * \param seq A `CharSeq` to be canonicalized.
     */
	template<class CharSeq>
	void canonicalize(CharSeq & seq) {
	    chewer<CharSeq> chew(cxx(),seq);
	    canonicalize(chew);
	}
};

/// \cond NO_DOXYGEN

// Copy literal/number spelling after phase-2 line splicing, without folding
// whitespace or interpreting comment delimiters inside quoted tokens.
inline void append_spliced(std::string & out, std::string const & text) {
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && i + 1 < text.size()) {
			if (text[i + 1] == '\n') { ++i; continue; }
			if (text[i + 1] == '\r' && i + 2 < text.size() && text[i + 2] == '\n') {
				i += 2; continue;
			}
		}
		out += text[i];
	}
}

template<>
template<class CharSeq>
void canonical<std::string>::canonicalize(chewer<CharSeq> & chew)
{
	chew(greyspace);
	while (chew) {
		auto mark = size_t(chew);
		chew(raw_string_literal);
		if (size_t(chew) != mark) {
			_canonical += chew.buf().substr(mark,size_t(chew) - mark);
		} else if (size_t(chew(string_literal,number_space,character_literal)) != mark) {
			append_spliced(_canonical,chew.buf().substr(mark,size_t(chew) - mark));
		} else {
			_canonical += *chew;
			++chew;
		}
		chew(continuation);
		mark = size_t(chew);
		chew(greyspace);
		if (chew && size_t(chew) != mark) { _canonical += ' '; }
	}
}

template<>
template<class CharSeq>
void canonical<macro_argument>::canonicalize(chewer<CharSeq> & chew)
{
	unsigned parens = 0;
	chew(greyspace);
	while (chew) {
		auto mark = size_t(chew);
		chew(raw_string_literal);
		if (size_t(chew) != mark) {
			_canonical += chew.buf().substr(mark,size_t(chew) - mark);
		} else if (size_t(chew(string_literal,number_space,character_literal)) != mark) {
			append_spliced(_canonical,chew.buf().substr(mark,size_t(chew) - mark));
		} else {
			if (*chew == ',' && !parens) { break; }
			if (*chew == ')') {
				if (!parens) { break; }
				--parens;
			} else if (*chew == '(') { ++parens; }
			_canonical += *chew;
			++chew;
		}
		chew(continuation);
		mark = size_t(chew);
		chew(greyspace);
		if (chew && size_t(chew) != mark) { _canonical += ' '; }
	}
	if (!_canonical.empty() && _canonical.back() == ' ') { _canonical.pop_back(); }
}

/// \endcond NO_DOXYGEN

#endif /* EOF */
