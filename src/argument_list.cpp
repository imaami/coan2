/***************************************************************************
 *   Copyright (C) 2007-2014 Mike Kinghan, imk@burroingroingjoing.com      *
 *   All rights reserved.                                                  *
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
#include "argument_list.h"
#include "canonical.h"
#include "parse_buffer.h"
#include "options.h"

/** \file argument_list.cpp
 *   This file implements `struct argument_list`.
 */

using namespace std;

template<class CharSeq>
void argument_list::read(chewer<CharSeq> & chew, bool header)
{
	_defect = none;
	_params.reset();
	_spelling.clear();
	chew(greyspace);
	if (*chew != '(') { return; }
	auto begin = size_t(chew);
	_params.reset(new std::vector<std::string>);
	chew(+1,greyspace);
	while (chew && *chew != ')') {
		auto start = size_t(chew);
		if (header && _params->empty() && *chew == '<') { chew(header_name); }
		std::string arg = chew.buf().substr(start,size_t(chew) - start);
		std::string rest = canonical<macro_argument>(chew);
		if (!arg.empty() && !rest.empty()) { arg += ' '; }
		_params->push_back(arg + rest);
		if (*chew == ')') { break; }
		if (*chew != ',') { break; }
		chew(+1,greyspace);
		if (*chew == ')') { _params->push_back(""); break; }
	}
	if (*chew == ')') { ++chew; }
	else { _defect = unclosed; }
	auto end = size_t(chew);
	std::string text = chew.buf().substr(begin,end - begin);
	chewer<std::string> spelling(!options::plaintext(),text);
	_spelling = canonical<std::string>(spelling);
}

template
void argument_list::read(chewer<string> & chew, bool header);
template
void argument_list::read(chewer<parse_buffer> & chew, bool header);

// EOF
