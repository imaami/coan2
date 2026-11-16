/***************************************************************************
 *   Copyright (C) 2004, 2006 Symbian Software Ltd.                        *
 *   All rights reserved.                                                  *
 *   Copyright (C) 2002, 2003 Tony Finch <dot@dotat.at>.                   *
 *   All rights reserved.                                                  *
 *   Copyright (C) 1985, 1993 The Regents of the University of California. *
 *   All rights reserved.                                                  *
 *   Copyright (C) 2007-2014 Mike Kinghan, imk@burroingroingjoing.com      *
 *   All rights reserved.                                                  *
 *                                                                         *
 *   Contributed by Mike Kinghan, imk@burroingroingjoing.com,              *
 *   derived from the code of Tony Finch                                   *
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
 *   Neither the name of Symbian Software Ltd. nor the names of its        *
 *   contributors may be used to endorse or promote products derived from  *
 *   this software without specific prior written permission.              *
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

#include "if_control.h"
#include "diagnostic.h"
#include "line_despatch.h"
#include "chew.h"
#include "io.h"
#include "trivalent_truth_vals.h"
#include "options.h"
#include "symbol.h"

/**	\file if_control.cpp
 *   This file implements `struct if_control`
 */

if_control::scope_info if_control::_scope_info_[MAXDEPTH + 1];
if_control::scope_info * if_control::_pscope_info_ = _scope_info_ + 1;
int if_control::state_truth_values[IF_STATE_COUNT] = {
    truth_value::True, // IF_STATE_OUTSIDE,
    truth_value::False, // IF_STATE_FALSE_PREFIX,
    truth_value::True, // IF_STATE_TRUE_PREFIX,
    truth_value::Indeterminate, // IF_STATE_PASS_MIDDLE,
    truth_value::False, // IF_STATE_FALSE_MIDDLE,
    truth_value::True, // IF_STATE_TRUE_MIDDLE,
    truth_value::Indeterminate, // IF_STATE_PASS_ELSE,
    truth_value::False, // IF_STATE_FALSE_ELSE,
    truth_value::True, // IF_STATE_TRUE_ELSE,
    truth_value::False // IF_STATE_FALSE_TRAILER,
};

void (*if_control::_despatcher_[truth_value::n_values])() = {
    line_despatch::drop,
    line_despatch::print,
    line_despatch::print
};

unsigned if_control::_depth_ = 0;

struct if_control::conditional_state {
	symbol::configuration incoming;
	std::vector<symbol::configuration> alternatives;
	bool fallthrough = true;
};

void if_control::transition(line_type linetype)
{
	std::shared_ptr<conditional_state> entering;
	bool opening = linetype == LT_IF || linetype == LT_TRUE || linetype == LT_FALSE;
	if (options::preprocess_source() && opening && may_reach_line()) {
		entering.reset(new conditional_state);
		entering->incoming = symbol::save_configuration();
		entering->fallthrough = linetype != LT_TRUE;
	}
	if_state previous = state();
	transition_table[previous][linetype]();
	if (opening) {
		_pscope_info_[_depth_]._macros = std::move(entering);
	} else if (_depth_ && (linetype == LT_ELTRUE || linetype == LT_ELSE)) {
		auto const & frame = _pscope_info_[_depth_]._macros;
		if (frame) { frame->fallthrough = false; }
	}
}

void if_control::prepare_branch(directive_type type)
{
	if (!options::preprocess_source() || !_depth_) { return; }
	bool alternate = type == HASH_ELIF || type == HASH_ELIFDEF ||
		type == HASH_ELIFNDEF || type == HASH_ELSE;
	if (!alternate && type != HASH_ENDIF) { return; }
	auto & frame = _pscope_info_[_depth_]._macros;
	if (!frame) { return; }
	if (may_reach_line()) {
		frame->alternatives.push_back(symbol::save_configuration());
	}
	// Header-guard recognition can make an initially unknown condition true.
	if (state() == IF_STATE_TRUE_PREFIX || state() == IF_STATE_TRUE_MIDDLE ||
		state() == IF_STATE_TRUE_ELSE || state() == IF_STATE_PASS_ELSE) {
		frame->fallthrough = false;
	}
	if (type == HASH_ENDIF) {
		if (frame->fallthrough) { frame->alternatives.push_back(frame->incoming); }
		symbol::merge_configurations(frame->alternatives);
		frame.reset();
	} else {
		symbol::restore_configuration(frame->incoming);
	}
}

if_control::input_scope::input_scope() : _saved_depth(_depth_)
{
	std::copy(_scope_info_,_scope_info_ + MAXDEPTH + 1,_saved_scopes.begin());
	std::fill_n(_scope_info_,MAXDEPTH + 1,scope_info());
	_depth_ = 0;
}

if_control::input_scope::~input_scope()
{
	std::copy(_saved_scopes.begin(),_saved_scopes.end(),_scope_info_);
	_depth_ = _saved_depth;
}

void if_control::Strue()
{
	line_despatch::drop();
	set_state(IF_STATE_TRUE_PREFIX);
}

void if_control::Sfalse()
{
	line_despatch::drop();
	set_state(IF_STATE_FALSE_PREFIX);
}

void if_control::Selse()
{
	line_despatch::drop();
	set_state(IF_STATE_TRUE_ELSE);
}

void if_control::Pelif()
{
	line_despatch::print();
	set_state(IF_STATE_PASS_MIDDLE);
}

void if_control::Pelse()
{
	line_despatch::print();
	set_state(IF_STATE_PASS_ELSE);
}

void if_control::Pendif()
{
	line_despatch::print();
	--_depth_;
}

void if_control::Dfalse()
{
	line_despatch::drop();
	set_state(IF_STATE_FALSE_TRAILER);
}

void if_control::Delif()
{
	line_despatch::drop();
	set_state(IF_STATE_FALSE_MIDDLE);
}

void if_control::Delse()
{
	line_despatch::drop();
	set_state(IF_STATE_FALSE_ELSE);
}

void if_control::Dendif()
{
	if (!_pscope_info_[_depth_]._by_idempotence) {
        line_despatch::drop();
	} else {
        _pscope_info_[_depth_]._by_idempotence = false;
        line_despatch::print();
	}
	--_depth_;
}

void if_control::Mpass()
{
	auto & line = line_despatch::cur_line();
	switch (line.get_directive_type()) {
	case HASH_ELIFDEF:
		line.keyword_edit<HASH_IFDEF>();
		break;
	case HASH_ELIFNDEF:
		line.keyword_edit<HASH_IFNDEF>();
		break;
	default:
		line.keyword_edit<HASH_IF>();
	}
	line_despatch::print();
	set_state(IF_STATE_PASS_MIDDLE);
}

void if_control::Mtrue()
{
	line_despatch::cur_line().keyword_edit<HASH_ELSE>();
	line_despatch::print();
	set_state(IF_STATE_TRUE_MIDDLE);
}

void if_control::Melif()
{
	line_despatch::cur_line().keyword_edit<HASH_ENDIF>();
	line_despatch::print();
	set_state(IF_STATE_FALSE_TRAILER);
}

void if_control::Melse()
{
	line_despatch::cur_line().keyword_edit<HASH_ENDIF>();
	line_despatch::print();
	set_state(IF_STATE_FALSE_ELSE);
}

void if_control::orphan_elif()
{
	error_orphan_elif() << "Orphan #elif" << emit();
}

void if_control::orphan_else()
{
	error_orphan_else() << "Orphan #else" << emit();
}

void if_control::orphan_endif()
{
	error_orphan_endif() << "Orphan #endif" << emit();
}

void if_control::early_eof()
{
	error_eof_too_soon() << "Unexpected end of file" << emit();
}


if_control::transition_t * const
if_control::transition_table[IF_STATE_COUNT][LT_SENTINEL] = {
	/* IF_STATE_OUTSIDE*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		orphan_elif,
		orphan_elif,
		orphan_elif,
		orphan_else,
		orphan_endif,
		despatch_plain,
		diagnostic_base::flush_all
	},
	/* IF_STATE_FALSE_PREFIX*/
	{
		Fdrop,
		Fdrop,
		Fdrop,
		Mpass,
		Strue,
		Sfalse,
		Selse,
		Dendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_TRUE_PREFIX*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		Dfalse,
		Dfalse,
		Dfalse,
		Delse,
		Dendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_PASS_MIDDLE*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		Pelif,
		Mtrue,
		Delif,
		Pelse,
		Pendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_FALSE_MIDDLE*/
	{
		Fdrop,
		Fdrop,
		Fdrop,
		Pelif,
		Mtrue,
		Delif,
		Pelse,
		Pendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_TRUE_MIDDLE*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		Melif,
		Melif,
		Melif,
		Melse,
		Pendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_PASS_ELSE*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		orphan_elif,
		orphan_elif,
		orphan_elif,
		orphan_else,
		Pendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_FALSE_ELSE*/
	{
		Fdrop,
		Fdrop,
		Fdrop,
		orphan_elif,
		orphan_elif,
		orphan_elif,
		orphan_else,
		Dendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_TRUE_ELSE*/
	{
		Fpass,
		Ftrue,
		Ffalse,
		orphan_elif,
		orphan_elif,
		orphan_elif,
		orphan_else,
		Dendif,
		despatch_plain,
		early_eof
	},
	/* IF_STATE_FALSE_TRAILER*/
	{
		Fdrop,
		Fdrop,
		Fdrop,
		Dfalse,
		Dfalse,
		Dfalse,
		Delse,
		Dendif,
		despatch_plain,
		early_eof
	}
	/*IF     TRUE   FALSE  ELIF   ELTRUE ELFALSE ELSE  ENDIF  PLAIN  EOF*/
};

void if_control::nest()
{
	if (++_depth_ >= MAXDEPTH) {
		error_too_deep() << "Too many levels of nesting" << emit();
	}
	_pscope_info_[_depth_]._start_line = line_despatch::cur_line().num();
}

void if_control::select_plain_line_despatchers(line_selector opt)
{
    if (opt == line_selector::must) {
        _despatcher_[truth_value::True] = line_despatch::print;
        _despatcher_[truth_value::False] = line_despatch::drop;
        _despatcher_[truth_value::Indeterminate] = line_despatch::drop;
    } else if (opt == line_selector::cant) {
        _despatcher_[truth_value::True] = line_despatch::drop;
        _despatcher_[truth_value::False] = line_despatch::print;
        _despatcher_[truth_value::Indeterminate] = line_despatch::drop;
    } else {
        _despatcher_[truth_value::True] = line_despatch::print;
        _despatcher_[truth_value::False] = line_despatch::drop;
        _despatcher_[truth_value::Indeterminate] = line_despatch::print;
    }
}


/* EOF*/
