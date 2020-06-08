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

#ifndef SRC_LEXER_H_
#define SRC_LEXER_H_

#include <cstdio>

/* YY_DECL is used by flex */
#define YY_DECL yy::Parser::token_type yylex(
  yy::Parser::semantic_type *yylval, \
  yy::Parser::location_type *yylloc, \
  yy::Parser::token_type start_symbol)

extern int yy_flex_debug;
extern std::FILE *yyin;


#endif  // SRC_LEXER_H_
