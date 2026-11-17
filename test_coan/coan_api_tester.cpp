#include "argument_list.h"
#include "diagnostic.h"
#include "line_despatch.h"
#include "options.h"
#include "reference.h"
#include "symbol.h"
#include <iostream>

// Exercise states that whole-directive canonicalization or rescanning can
// otherwise mask before the CLI reaches the affected API.
int main(int, char ** argv)
{
	options::parse_executable(argv);
	line_despatch::top();
	for (auto text : {std::string("(1\\\n'000, 2'000)"),
	                 std::string("(1'\\\n000, 2'000)")}) {
		chewer<std::string> chew(true,text);
		argument_list args(chew);
		if (args.size() != 2 || args[0] != "1'000" || args[1] != "2'000") {
			std::cerr << "Spliced preprocessing number hid an argument separator\n";
			return 1;
		}
	}
	std::string number = "1e\\\n+5";
	chewer<std::string> exponent(true,number);
	exponent(number_space);
	if (exponent) { return 1; }

	error_non_term() << "outer expression" << defer();
	{
		diagnostic_base::discarded_queue diagnostics;
		error_non_term() << "report-only expression" << defer();
		error_unbalanced_paren() << "report-only fragment" << defer();
	}
	if (diagnostic_base::deferred() != 1) { return 1; }
	diagnostic_base::discard_all();

	symbol::locator f("F");
	std::string definition = "(x)=+##x";
	chewer<std::string> def(true,definition);
	f->digest_global_define(def);
	reference preview(f);
	preview.expansion();
	// No intervening definition or cache-generation change: this real call
	// must reject +##x even though the same spelling was a definition preview.
	std::string actual = "(x)";
	chewer<std::string> call(true,actual);
	reference invocation(f,call);
	try { invocation.expansion(); }
	catch (unsigned reason) {
		if (reason != error_bad_token_paste::code()) { return 1; }
		std::cout << "Scanner, diagnostic isolation and preview-cache regressions passed\n";
		return 0;
	}
	std::cerr << "Invalid prototype cache entry concealed an actual paste error\n";
	return 1;
}
