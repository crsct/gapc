/* {{{

    This file is part of gapc (GAPC - Grammars, Algebras, Products - Compiler;
      a system to compile algebraic dynamic programming programs)

    Copyright (C) 2008-2011  Georg Sauthoff
         email: gsauthof@techfak.uni-bielefeld.de or gsauthof@sdf.lonestar.org

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

}}} */


#include <cstdio>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>

#include <iostream>

#include "driver.hh"
#include "filter.hh"
#include "expr.hh"

#include "parser.hh"

#include "../src/lexer.h"
#include "../src/lexer_priv.h"


Driver::Driver() : from_stdin(false), trace_lexer(false), trace_parser(false),
  fail_later(false), filename_(0) {
  includes.push_back("");
}


bool Driver::parse() {
  yy::Parser parser(*this, yy::Parser::token::START_PROGRAM);
  parser.set_debug_level(trace_parser);
  // parser.set_debug_level(true);
  if (!lexer_prepare()) {
    return false;
  }
  fail_later = false;

  // FIXME call maximal one time during execution
  Filter::init_table();
  // builts up Fn_Decl for all standard functions
  // populating builtins of Fn_Decls
  Fn_Decl::init_table();
  Mode::init_table();
  // inits objects for all buil in function like MIN and EXP
  Expr::Fn_Call::init_builtins();

  // built up AST
  // driver functions are called from autogenerated parser
  parser.parse();
  fail_later = fail_later || Log::instance()->seen_errors();
  return fail_later;
}


bool Driver::lexer_prepare(void) {
  // file_close();
  yy_flex_debug = trace_lexer;
  scanner::init(this);
  if (from_stdin)
    return true;
  return file_open();
}


void Driver::file_close() {
  if (yyin != stdin && yyin != NULL) {
    std::fclose(yyin);
  }
  for (std::vector<std::FILE*>::iterator i = open_files.begin();
       i != open_files.end(); ++i) {
    std::fclose(*i);
  }
  open_files.clear();
}


bool Driver::file_open() {
  assert(filename_);
  if (!(yyin = std::fopen(filename_->c_str(), "r"))) {
    error(std::string("Can't open ") + *filename_ + std::string(": ") +
          std::string(std::strerror(errno)));
    return false;
  }
  return true;
}


void Driver::error(const std::string &m) {
  Log::instance()->error(m);
  fail_later = true;
}


void Driver::error(const Loc& l, const std::string& m) {
  Log::instance()->error(l, m);
  fail_later = true;
}


#include "instance.hh"


void Driver::parse_product(const std::string &s) {
  if (s.empty() || fail_later)
    return;

  // file_close();
  yyin = 0;

  // don't call yy_delete_buffer(state); on it because yypop_buffer_state()
  // is called from <<EOF>> action
  /* YY_BUFFER_STATE state =*/ yy_scan_string(s.c_str());
  scanner::init(this);
  std::string *temp = filename_;
  setFilename("_PRODUCT_");
  Log::instance()->set_product(s);

  yy::Parser parser(*this, yy::Parser::token::START_PRODUCT);
  parser.set_debug_level(trace_parser);
  parser.parse();
  // yy_delete_buffer(state);
  fail_later = fail_later || Log::instance()->seen_errors();
  if (fail_later)
    return;

  assert(ast.grammar());
  Product::Base *p = ast.product();
  assert(p);
  Instance *i = new Instance(new std::string("_PRODUCT_"), p, ast.grammar());
  ast.first_instance = i;
  ast.instances["_PRODUCT_"] = i;

  filename_ = temp;
  file_open();
}


void Driver::setFilename(const std::string &s) {
  filename_ = new std::string(s);
}


std::string *Driver::filename() {
  assert(filename_);
  return filename_;
}


void Driver::setStdin(bool b) {
  filename_ = new std::string("<stdin>");
  from_stdin = b;
}


void Driver::set_includes(const std::vector<std::string> &v) {
  includes.insert(includes.end(), v.begin(), v.end());
}


void Driver::push_buffer(const std::string &s) {
  std::string f;
  for (std::vector<std::string>::iterator i = includes.begin();
       i != includes.end(); ++i) {
    std::string b(*i);
    if (!b.empty() && b[b.size()-1] != '/')
      b.push_back('/');
    f = b + s;
    yyin = std::fopen(f.c_str(), "r");
    if (yyin)
      break;
  }
  if (!yyin)
    throw LogError(std::string("include: Can't open ") + s +
                   std::string(": ") + std::string(std::strerror(errno)));
  if (open_files.size() > 100)
    throw LogError("Too many open files! (include loop?)");
  yypush_buffer_state(yy_create_buffer(yyin, YY_BUF_SIZE));
  open_files.push_back(yyin);
}
