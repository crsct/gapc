/* {{{

    This file is part of gapc (GAPC - Grammars, Algebras, Products - Compiler;
      a system to compile algebraic dynamic programming programs)

    Copyright (C) 2011-2023  Stefan Janssen
         email: stefan.m.janssen@gmail.com or stefan.janssen@computational.bio.uni-giessen.de

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

#ifndef SRC_CYK_HH_
#define SRC_CYK_HH_

#include <list>
#include <vector>
#include <string>

#include "ast.hh"
#include "printer.hh"
#include "cpp.hh"
#include "statement_fwd.hh"
#include "expr.hh"
#include "const.hh"
#include "fn_def.hh"
#include "statement/fn_call.hh"
#include "var_acc.hh"

Fn_Def *print_CYK(const AST &ast);

#endif /* SRC_CYK_HH_ */
