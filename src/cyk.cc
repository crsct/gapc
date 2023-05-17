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

#include "cyk.hh"

static const char *MUTEX = "mutex";
static const char *VARNAME_OuterLoop1 = "outer_loop_1_idx";
static const char *VARNAME_OuterLoop2 = "outer_loop_2_idx";
static const char *VARNAME_InnerLoop2 = "inner_loop_2_idx";

Statement::Fn_Call *mutex_lock() {
  Statement::Fn_Call *fn = new Statement::Fn_Call("lock_shared");
  fn->add_arg(new std::string(MUTEX));
  fn->is_obj = Bool(true);
  return fn;
}
Statement::Fn_Call *mutex_unlock() {
  Statement::Fn_Call *fn = new Statement::Fn_Call("unlock_shared");
  fn->add_arg(new std::string(MUTEX));
  fn->is_obj = Bool(true);
  return fn;
}

std::tuple<std::list<Statement::Base*>*, Statement::Var_Decl*>
get_tile_computation(const AST &ast, std::string name_maxtilen,
    Statement::Var_Decl *input_seq, bool just_tilesize) {
  Statement::Var_Decl *tile_size = new Statement::Var_Decl(
        new Type::Size(),
        "tile_size",
        new Expr::Const(32));

  std::list<Statement::Base*> *res = new std::list<Statement::Base*>();

  if (!(ast.checkpoint && ast.checkpoint->cyk) || just_tilesize) {
    res->push_back(tile_size);
    res->push_back(new Statement::CustomCode("#ifdef TILE_SIZE"));
    res->push_back(new Statement::Var_Assign(*tile_size, new Expr::Vacc(
        new std::string("TILE_SIZE"))));
    res->push_back(new Statement::CustomCode("#endif"));
    if (just_tilesize) {
      return std::make_tuple(res, tile_size);;
    }
  }
  res->push_back(new Statement::Fn_Call(
      Statement::Fn_Call::ASSERT, *tile_size));
  Expr::Fn_Call *end = new Expr::Fn_Call(new std::string("size"));
    end->add_arg(input_seq->name);
    end->is_obj = Bool(true);
  Statement::Var_Decl *max_tiles = new Statement::Var_Decl(
      new Type::Size(),
      "max_tiles",
      new Expr::Div(end, new Expr::Vacc(*tile_size)));
  res->push_back(max_tiles);
  Statement::Var_Decl *max_tiles_n = new Statement::Var_Decl(
      new Type::Int(),
      name_maxtilen,
      new Expr::Times(new Expr::Vacc(*max_tiles), new Expr::Vacc(*tile_size)));
  res->push_back(max_tiles_n);

  return std::make_tuple(res, tile_size);
}

/* deep copy of a list of statements */
std::list<Statement::Base*> *copy_statements(
    std::list<Statement::Base*> *other) {
  std::list<Statement::Base*> *co = new std::list<Statement::Base*>();
  for (std::list<Statement::Base*>::iterator i = other->begin();
       i != other->end(); ++i) {
    co->push_back((*i)->copy());
  }
  return co;
}

/* data structure to bundle a Statement::For and a Statement::Var_Decl which
 * constitute a for loop to iterate over NT indices and the last index the loop
 * not yet iterated over.
 */
class CYKloop {
 public:
  Statement::For *loop;  // the constructed for loop statement
  Statement::Var_Decl *end_state;  // the variable declaration of index the
                                   // loop did not reach
  CYKloop(Statement::For *loop, Statement::Var_Decl *end_state) :
    loop(loop), end_state(end_state) {
    assert(loop->var_decl->name == end_state->name);
  }
};

enum CYKmode {SINGLETHREAD, OPENMP_PARALLEL, OPENMP_SERIAL, SINGLETHREAD_OUTSIDE};

CYKloop get_for_column(Expr::Vacc *running_boundary,
    Expr::Base *start, Expr::Base *end,
    bool with_checkpoint, CYKmode mode) {
  // create loop variable addressing the DP column (=2nd index)
  // e.g.: for (unsigned int t_0_j = 0; t_0_j < t_0_seq.size(); ++t_0_j) {
  Type::Base *t = new Type::Size();
  if (with_checkpoint && (mode != CYKmode::OPENMP_PARALLEL)) {
    t = new Type::External("");  // ugly hack to avoid redeclaration of variable
    start = new Expr::Cond(
        new Expr::Vacc(new std::string(
            *running_boundary->name() + "_loaded++")),
        start,
        running_boundary);
  }

  Statement::Var_Decl *var_col = new Statement::Var_Decl(
      t,
      running_boundary,
      start);

  // create condition of For loop
  Expr::Less *cond_col = new Expr::Less(
      new Expr::Vacc(*var_col), end);

  Statement::For *loop = new Statement::For(var_col, cond_col);

  Statement::Var_Decl *var_nonloop = var_col->clone();
  var_nonloop->rhs = end;

  return CYKloop(loop, var_nonloop);
}

CYKloop get_for_row(Expr::Vacc *running_boundary, Expr::Base *start,
    Expr::Base *end, bool with_checkpoint, CYKmode mode) {
  // create loop variable addressing the DP row (=1st index)
  // e.g.: for (unsigned int t_0_i = t_0_j + 1; t_0_i > 1; t_0_i--) {
  Type::Base *t = new Type::Size();
  if (mode == CYKmode::OPENMP_PARALLEL) {
    t = new Type::Int();
  }
  if (with_checkpoint && (mode != CYKmode::OPENMP_PARALLEL)) {
    t = new Type::External("");  // ugly hack to avoid redeclaration of variable
    start = new Expr::Cond(
        new Expr::Vacc(new std::string(
            *running_boundary->name() + "_loaded++")),
        start,
        running_boundary);
  }
  Statement::Var_Decl *var_row = new Statement::Var_Decl(
      t,
      running_boundary,
      start);

  // create condition of For loop
  Expr::Two *cond_row = new Expr::Greater(new Expr::Vacc(*var_row), end);
  if (mode == CYKmode::SINGLETHREAD_OUTSIDE) {
    cond_row = new Expr::Less(new Expr::Vacc(*var_row), end);
  }

  Statement::For *loop = new Statement::For(var_row, cond_row);
  // except for outside, we need to decrement the loop variable, i.e. t_x_i--
  // In outside, it must be ++t_x_i
  if (mode != CYKmode::SINGLETHREAD_OUTSIDE) {
    Statement::Var_Assign *x = new Statement::Var_Assign(
        *var_row, new Expr::Const(new Const::Int(-1)));
    x->set_op(::Expr::Type::PLUS);
    loop->inc = x;
  }

  Statement::Var_Decl *var_nonloop = var_row->clone();
  var_nonloop->rhs = new Expr::Const(1);

  return CYKloop(loop, var_nonloop);
}

Statement::For *get_for_openMP(Expr::Vacc *loopvar, Expr::Base *start,
    Expr::Base *end, Statement::Var_Decl *inc) {
  Statement::Var_Decl *var = new Statement::Var_Decl(
      new Type::Int(),
      loopvar,
      start);

  // create condition of For loop
  Expr::Less *cond_row = new Expr::Less(new Expr::Vacc(*var), end);

  Statement::For *loop = new Statement::For(var, cond_row);
  Statement::Var_Assign *x = new Statement::Var_Assign(*var, *inc);
  x->set_op(::Expr::Type::PLUS);
  loop->inc = x;

  return loop;
}

/*
 * Construct the loop traversal structure for CYK parsing of one track as below.
 * Note that this general structure gets recursively nested for multiple tracks!
 * The result will "only" contain loops, but they are empty for now.
 * Call function add_nt_call() to populate loops with concrete NT calls,
 * which depends on the NT actual table dimensions.
 * for (t_x_j ... {
 *   for (t_x_i ... {
 *     calls to triangular cells = A
 *     nt_tabulated_foo(t_x_i+1, t_x_j, ...)
 *   }
 *   calls to top row = B
 *   nt_tabulated_foo(0, t_x_j, ...)
 * }
 * for (t_x_i ... {
 *   calls to last column = C
 *   nt_tabulated_foo(t_x_i, x_n, ...)
 * }
 * calls to top right cell = D
 * nt_tabulated_foo(0, x_n, ...)
 *
 *   |  0  1  2  3   4  5          |  0  1  2  3  4  5
 * --|-------------------        --|------------------
 * 0 |  0  2  5  9  14 20        0 |  B  B  B  B  B  D
 * 1 |     1  4  8  13 19        1 |     A  A  A  A  C
 * 2 |        3  7  12 18        2 |        A  A  A  C
 * 3 |           6  11 17        3 |           A  A  C
 * 4 |              10 16        4 |              A  C
 * 5 |                 15        5 |                 C
 */
std::list<Statement::Base*> *cyk_traversal_singlethread_singletrack(
    size_t track, const AST &ast, Statement::Var_Decl *seq,
    std::list<Statement::Base*> *nested_stmts, bool with_checkpoint,
    CYKmode mode) {
  std::list<Statement::Base*> *stmts = new std::list<Statement::Base*>();

  Expr::Base *row_start = ast.grammar()->right_running_indices.at(
      track)->plus(new Expr::Const(1));
  // create t_X_seq.size() call
  Expr::Fn_Call *seqend = new Expr::Fn_Call(new std::string("size"));
  dynamic_cast<Expr::Fn_Call*>(seqend)->add_arg(seq->name);
  dynamic_cast<Expr::Fn_Call*>(seqend)->is_obj = Bool(true);

  // A: major cells in triangle below first row, left of last columns
  // A: t_x_i = row index
  std::list<Statement::Base*> *co = copy_statements(nested_stmts);
  CYKloop row = get_for_row(ast.grammar()->left_running_indices[track],
      row_start, new Expr::Const(1), with_checkpoint, mode);
  row.loop->statements.insert(
      row.loop->statements.end(), co->begin(), co->end());

  // A: t_x_j = column index
  Expr::Base *alt_start = new Expr::Const(0);
  if (mode == CYKmode::OPENMP_SERIAL) {
    alt_start = new Expr::Vacc(new std::string("max_tiles_n"));
  }
  CYKloop col = get_for_column(ast.grammar()->right_running_indices[track],
      alt_start, seqend, with_checkpoint, mode);
  col.loop->statements.push_back(row.loop);
  col.loop->statements.push_back(row.end_state);

  // B: first row
  co = copy_statements(nested_stmts);
  col.loop->statements.insert(
      col.loop->statements.end(), co->begin(), co->end());
  stmts->push_back(col.loop);
  stmts->push_back(col.end_state);

  // C: last column
  CYKloop rowC = get_for_row(ast.grammar()->left_running_indices[track],
      row_start, new Expr::Const(1), with_checkpoint, mode);
  co = copy_statements(nested_stmts);
  rowC.loop->statements.insert(
      rowC.loop->statements.end(), co->begin(), co->end());
  stmts->push_back(rowC.loop);
  stmts->push_back(rowC.end_state);

  // D: top right cell
  co = copy_statements(nested_stmts);
  stmts->insert(stmts->end(), co->begin(), co->end());

  return stmts;
}

/*
 *   for (unsigned int t_0_i = 0; t_0_i <= t_0_seq.size(); ++t_0_i) {
    for (unsigned int t_0_j = t_0_seq.size() - t_0_i; t_0_j <= t_0_seq.size(); ++t_0_j) {
 *
 */
std::list<Statement::Base*> *cyk_traversal_singlethread_singletrack_outside(
    size_t track, const AST &ast, Statement::Var_Decl *seq,
    std::list<Statement::Base*> *nested_stmts, bool with_checkpoint,
    CYKmode mode) {
  std::list<Statement::Base*> *stmts = new std::list<Statement::Base*>();

  // create t_X_seq.size() call
  Expr::Fn_Call *seqend = new Expr::Fn_Call(new std::string("size"));
  dynamic_cast<Expr::Fn_Call*>(seqend)->add_arg(seq->name);
  dynamic_cast<Expr::Fn_Call*>(seqend)->is_obj = Bool(true);

  CYKloop col = get_for_column(
      ast.grammar()->right_running_indices[track],
      seqend->minus(ast.grammar()->left_running_indices[track]), seqend->plus(new Expr::Const(1)),
      with_checkpoint, mode);
  std::list<Statement::Base*> *co = copy_statements(nested_stmts);
  col.loop->statements.insert(
      col.loop->statements.end(), co->begin(), co->end());

  CYKloop row = get_for_row(ast.grammar()->left_running_indices[track],
      new Expr::Const(0), seqend->plus(new Expr::Const(1)),
      with_checkpoint, mode);
  row.loop->statements.push_back(col.loop);

  stmts->push_back(row.loop);

  return stmts;
}

// recursively reverse iterate through tracks and create nested for loop
// structures
std::list<Statement::Base*> *cyk_traversal_singlethread(const AST &ast,
    CYKmode mode) {
  std::list<Statement::Base*> *stmts = new std::list<Statement::Base*>();

  assert(ast.seq_decls.size() == ast.grammar()->axiom->tracks());
  std::vector<Statement::Var_Decl*>::const_reverse_iterator it_stmt_seq =
      ast.seq_decls.rbegin();
  for (int track = ast.grammar()->axiom->tracks() - 1; track >= 0;
       track--, ++it_stmt_seq) {
    if (mode == CYKmode::SINGLETHREAD_OUTSIDE) {
      stmts = cyk_traversal_singlethread_singletrack_outside(
          track, ast, *it_stmt_seq, stmts, ast.checkpoint && ast.checkpoint->cyk,
          mode);
    } else {
      stmts = cyk_traversal_singlethread_singletrack(
          track, ast, *it_stmt_seq, stmts, ast.checkpoint && ast.checkpoint->cyk,
          mode);
    }
  }

  return stmts;
}




/* Construct the loop traversal structure for CYK parsing of one track in
 * multi-threaded mode. Before we can start operating in parallel, we need to
 * compute all predecessor cells (part A). Thus, tiles of the DP matrix on the
 * diagonal can then be processed in parallel (part B)
 * Note: currently only works for single track!
 *  A: tile_size = 4, input = aaaaccccgggg
 *    |  0   1   2   3   4   5   6   7   8   9  10  11  12
 * ---|----------------------------------------------------
 *  0 |  0   2   5   9
 *  1 |      1   4   8
 *  2 |          3   7
 *  3 |              6
 *  4 |                 10  12  15  19
 *  5 |                     11  14  18
 *  6 |                         13  17
 *  7 |                             16
 *  8 |                                 20  22  25  29
 *  9 |                                     21  24  28
 * 10 |                                         23  27
 * 11 |                                             26
 * 12 |
 *
 *  B: tile_size = 4, input = aaaaccccgggg
 *    |  0   1   2   3   4   5   6   7   8   9  10  11  12
 * ---|----------------------------------------------------
 *  0 |                 33  37  41  45  65  69  73  77
 *  1 |                 32  36  40  44  64  68  72  76
 *  2 |                 31  35  39  43  63  67  71  75
 *  3 |                 30  34  38  42  62  66  70  74
 *  4 |                                 49  53  57  61
 *  5 |                                 48  52  56  60
 *  6 |                                 47  51  55  59
 *  7 |                                 46  50  54  58
 *  8 |
 *  9 |
 * 10 |
 * 11 |
 * 12 |
 *
 * Note: the below can be constructured by the cyk_traversal_singlethread
 * Construct the loop traversal structure for the non-parallel part in multi-
 * threaded mode, i.e. iterate over all DP cells that fall out of the tiling
 * pattern.
 *  C: tile_size = 4, input = aaaaccccgggg
 *    |  0  1  2  3  4  5  6  7  8  9 10 11 12
 * ---|----------------------------------------
 *  0 |                                     90
 *  1 |                                     89
 *  2 |                                     88
 *  3 |                                     87
 *  4 |                                     86
 *  5 |                                     85
 *  6 |                                     84
 *  7 |                                     83
 *  8 |                                     82
 *  9 |                                     81
 * 10 |                                     80
 * 11 |                                     79
 * 12 |                                     78
 *
 */
std::list<Statement::Base*> *cyk_traversal_multithread_parallel(const AST &ast,
    Statement::Var_Decl *seq, Statement::Var_Decl *tile_size,
    std::string *name_maxtilen, bool with_checkpoint) {
  size_t track = 0;  // as openMP currently only works for single track grammars
  std::list<Statement::Base*> *stmts = new std::list<Statement::Base*>();

  Expr::Base *row_start = ast.grammar()->right_running_indices.at(
      track)->plus(new Expr::Const(1));

  Expr::Vacc *z = new Expr::Vacc(new std::string("z"));
  Expr::Vacc *y = new Expr::Vacc(new std::string("y"));
  Statement::Var_Decl *x = new Statement::Var_Decl(
      new Type::Size(), "x", (y->minus(z))->plus(new Expr::Vacc(*tile_size)));

  // part A: prepare for parallel tile phase, prepare predecessor DP cells for
  // later parallel computation
  CYKloop row = get_for_row(ast.grammar()->left_running_indices[track],
      row_start, z, with_checkpoint, CYKmode::OPENMP_PARALLEL);
  CYKloop col = get_for_column(ast.grammar()->right_running_indices[track],
      z, z->plus(new Expr::Vacc(*tile_size)), with_checkpoint,
      CYKmode::OPENMP_PARALLEL);
  col.loop->statements.push_back(row.loop);
  Expr::Base *start_z = new Expr::Const(0);
  if (with_checkpoint) {
    start_z = new Expr::Vacc(new std::string(std::string(
        VARNAME_OuterLoop1) + "_start"));
  }
  Statement::For *loop_z = get_for_openMP(z, start_z,
      new Expr::Vacc(name_maxtilen), tile_size);
  if (with_checkpoint) {
    loop_z->statements.push_back(mutex_lock());
  }
  loop_z->statements.push_back(col.loop);
  // code to wait for threads to finish
  if (with_checkpoint) {
    loop_z->statements.push_back(new Statement::CustomCode(
        "#pragma omp ordered"));
    Statement::Block *blk_omp = new Statement::Block();
    blk_omp->statements.push_back(new Statement::CustomCode(
        "// force omp to wait for all threads to finish their current batch "
        "(of size tile_size)"));
    blk_omp->statements.push_back(new Statement::Var_Assign(
        new Var_Acc::Plain(new std::string(VARNAME_OuterLoop1)),
        start_z->plus(new Expr::Vacc(tile_size->var_decl()->name))));
    blk_omp->statements.push_back(mutex_unlock());
    loop_z->statements.push_back(blk_omp);
  }
  stmts->push_back(loop_z);

  // part B: code for the actual parallel tile computation
  CYKloop rowB = get_for_row(ast.grammar()->left_running_indices[track],
      new Expr::Vacc(*x),
      (new Expr::Vacc(*x))->minus(new Expr::Vacc(*tile_size)),
      with_checkpoint, CYKmode::OPENMP_PARALLEL);
  CYKloop colB = get_for_column(ast.grammar()->right_running_indices[track],
      y, y->plus(new Expr::Vacc(*tile_size)), with_checkpoint,
      CYKmode::OPENMP_PARALLEL);
  colB.loop->statements.push_back(rowB.loop);

  Expr::Base *start_y = z;
  if (with_checkpoint) {
    start_y = new Expr::Cond(
        new Expr::Vacc(new std::string(std::string(
            VARNAME_InnerLoop2) + "_loaded")),
        z,
        new Expr::Vacc(new std::string(std::string(
            VARNAME_InnerLoop2) + "_start")));
  }
  Statement::For *loop_y = get_for_openMP(y, start_y,
      new Expr::Vacc(name_maxtilen), tile_size);
  // produce: unsigned int x = y - z + tile_size;
  if (with_checkpoint) {
    loop_y->statements.push_back(new Statement::CustomCode(
        "++inner_loop_2_idx_loaded;"));
    loop_y->statements.push_back(mutex_lock());
  }
  loop_y->statements.push_back(x);
  loop_y->statements.push_back(colB.loop);
  if (with_checkpoint) {
    loop_y->statements.push_back(new Statement::CustomCode(
        "#pragma omp ordered"));
    Statement::Block *blk_omp2 = new Statement::Block();
    blk_omp2->statements.push_back(new Statement::Var_Assign(
        new Var_Acc::Plain(new std::string(VARNAME_InnerLoop2)),
        (new Expr::Vacc(new std::string(VARNAME_InnerLoop2)))->plus(
            new Expr::Vacc(tile_size->var_decl()->name))));
    blk_omp2->statements.push_back(new Statement::Var_Assign(
        new Var_Acc::Plain(new std::string(VARNAME_OuterLoop2)), z));
    blk_omp2->statements.push_back(mutex_unlock());
    loop_y->statements.push_back(blk_omp2);
  }


  Expr::Vacc *start_z2 = new Expr::Vacc(*tile_size);
  if (with_checkpoint) {
    start_z2 = new Expr::Vacc(new std::string(std::string(
        VARNAME_OuterLoop2) + "_start"));
  }
  loop_z = get_for_openMP(z, start_z2, new Expr::Vacc(name_maxtilen),
      tile_size);
  if (with_checkpoint) {
    loop_z->statements.push_back(new Statement::CustomCode(
        "#pragma omp for ordered schedule(dynamic)"));
  } else {
    loop_z->statements.push_back(new Statement::CustomCode("#pragma omp for"));
  }
  loop_z->statements.push_back(loop_y);
  if (with_checkpoint) {
    loop_z->statements.push_back(new Statement::Var_Assign(new Var_Acc::Plain(
        new std::string(VARNAME_InnerLoop2)),
        z->plus(new Expr::Vacc(tile_size->var_decl()->name))));
  }

  stmts->push_back(loop_z);

  return stmts;
}


size_t count_nt_calls_and_loops(Statement::For *loop) {
  size_t res = 0;
  for (std::list<Statement::Base*>::const_iterator i = loop->statements.begin();
       i != loop->statements.end(); ++i) {
    if ((*i)->is(Statement::FN_CALL) &&
        (dynamic_cast<Statement::Fn_Call*>(*i)->name().find(
            "nt_tabulate_", 0) == 0)) {
      res++;
    }
    if ((*i)->is(Statement::FOR)) {
      res++;
    }
  }
  return res;
}

/* This function will add NT calls (and mutex operations) into a given CYK
 * traversal structure in a recursive fashion. The challenge is to add an NT
 * call into the correct level of nested for loops, i.e. only as deep as the NT
 * table has indices. However, we can have left or right linear optimized tables
 * and we need to ensure to find the correct loop (row or column) at the same
 * level. Furthermore, last row/column in single thread CYK mode are called
 * AFTER the triangle (with cells A) has been computed, which means NTs also
 * have to be called outside the correct nesting level!
 *
 * The strategy here is to use a "stack" of loop variable names for the nesting
 * level and count how many indices actually are used by an NT. Depending on
 * single (see above problem with last row/col) or multi thread mode, NT calls
 * are only added IF the number of *used* indices (through a loop =
 * used_indices) coincide with nesting level or additionally if the NT has the
 * correct number of indices (nt_has_index), respectively.
 */
std::list<Statement::Base*> *add_nt_calls(std::list<Statement::Base*> &stmts,
    std::list<std::string*> *loop_vars, std::list<Symbol::NT*> orderedNTs,
    bool with_checkpoint, CYKmode mode) {
  bool contains_nested_for = false;
  for (std::list<Statement::Base*>::iterator s = stmts.begin();
       s != stmts.end(); ++s) {
    // recurse into next for loop
    if ((*s)->is(Statement::FOR)) {
      contains_nested_for = true;
      Statement::For *fl = dynamic_cast<Statement::For*>(*s);
      std::list<std::string*> *next_loop_vars = new std::list<std::string*>();
      next_loop_vars->insert(
          next_loop_vars->end(), loop_vars->begin(), loop_vars->end());
      if ((mode != CYKmode::OPENMP_PARALLEL) ||
          (fl->var_decl->name->find("t_", 0) == 0)) {
        // openMP code adds in loops that do not traverse NT indices. Only add
        // loop variable, if it regard to NT indices, which all start with t_
        // e.g. t_0_i or t_1_j
        next_loop_vars->push_back(fl->var_decl->name);
      }
      std::list<Statement::Base*> *new_stmts = add_nt_calls(
          fl->statements, next_loop_vars, orderedNTs, with_checkpoint,
          mode);
      fl->statements.insert(
          fl->statements.end(), new_stmts->begin(), new_stmts->end());
    }
  }

  // remove loops that neither contain NT calls nor nested loops
  for (std::list<Statement::Base*>::iterator s = stmts.begin();
       s != stmts.end(); ++s) {
    if ((*s)->is(Statement::FOR)) {
      if (count_nt_calls_and_loops(dynamic_cast<Statement::For*>(*s)) == 0) {
        s = stmts.erase(s);
      }
    }
  }

  if ((mode == CYKmode::OPENMP_PARALLEL) && contains_nested_for) {
    // don't add NT calls in for loops that is not the innermost loop, if in
    // multi threaded mode.
    return new std::list<Statement::Base*>();
  }

  // add NTs
  std::list<Statement::Base*> *nt_stmts = new std::list<Statement::Base*>();
  if (with_checkpoint) {
    if (mode == CYKmode::SINGLETHREAD) {
      nt_stmts->push_back(new Statement::CustomCode(
          "std::lock_guard<fair_mutex> lock(mutex);"));
    } else {
      if (mode == CYKmode::OPENMP_SERIAL) {
        nt_stmts->push_back(mutex_lock());
      }
    }
  }
  for (std::list<Symbol::NT*>::const_iterator i = orderedNTs.begin();
       i != orderedNTs.end(); ++i) {
    if (!(*i)->is_tabulated()) {
      continue;
    }
    std::list<Expr::Base*> *args = new std::list<Expr::Base*>();
    size_t used_indices = 0;
    size_t nt_has_indices = 0;
    for (size_t t = 0; t < (*i)->tracks(); ++t) {
      if (!(*i)->tables()[t].delete_left_index()) {
        Expr::Vacc *idx = (*i)->left_indices.at(t)->vacc();
        if (std::find(loop_vars->begin(), loop_vars->end(),
            idx->name()) != loop_vars->end()) {
          used_indices++;
        }
        nt_has_indices++;
        args->push_back(idx->minus(new Expr::Const(1)));
      }
      if (!(*i)->tables()[t].delete_right_index()) {
        Expr::Vacc *idx = (*i)->right_indices.at(t)->vacc();
        if (std::find(loop_vars->begin(), loop_vars->end(),
            idx->name()) != loop_vars->end()) {
          used_indices++;
        }
        nt_has_indices++;
        args->push_back(idx);
      }
    }
    if (used_indices == loop_vars->size()) {
        assert((*i)->code_list().size() > 0);
        Statement::Fn_Call *nt_call = new Statement::Fn_Call(
            (*(*i)->code_list().rbegin())->name, args, Loc());
        nt_stmts->push_back(nt_call);
    }
  }
  if (with_checkpoint) {
    if (mode == CYKmode::OPENMP_SERIAL) {
      nt_stmts->push_back(mutex_unlock());
    }
  }

  return nt_stmts;
}

Fn_Def *print_CYK(const AST &ast) {
  Fn_Def *fn_cyk = new Fn_Def(new Type::RealVoid(), new std::string("cyk"));
  if (!ast.cyk()) {
    /* return empty function, if CYK was not requested. It is called in the
     * generic out_main.cc source, thus it has to be defined but can remain
     * empty.
     */
    return fn_cyk;
  }

  if (ast.checkpoint && ast.checkpoint->cyk) {
  /*
    define a boolean marker (as an int) for every loop idx
    to allow for the loading of the checkpointed loop indices;
    if the user wants to load a checkpoint (load_checkpoint == true)
    and the loaded idx value doesn't equal the default value 0
    (meaning that the checkpointed program made enough progress
     to get to the loop where the current idx lives),
    the markers will be set to "false" (== 0), which indicates
    that the respective loop idx hasn't been loaded yet and
    should be loaded when it is first requested;
    if the user does not want to load a checkpoint
    (load_checkpoint == false) or the load idx values are still 0,
    the respective markers will be set to "true" (== 1);
    this means that all idx values are already assumed to be
    loaded and won't be loaded when they are first requested;
    this ensures that the idx values start at whatever value
    they would normally start with
   */
    for (size_t track = 0; track < ast.grammar()->axiom->tracks(); ++track) {
      fn_cyk->stmts.push_back(new Statement::Var_Decl(
          new Type::Int(),
          *(ast.grammar()->left_running_indices.at(track)->name()) +
            std::string("_loaded"), new Expr::Or(
              new Expr::Not(new Expr::Vacc(new std::string("load_checkpoint"))),
              new Expr::Not(ast.grammar()->left_running_indices.at(track)))));

      fn_cyk->stmts.push_back(new Statement::Var_Decl(
          new Type::Int(),
          *(ast.grammar()->right_running_indices.at(track)->name()) +
          std::string("_loaded"), new Expr::Or(
              new Expr::Not(new Expr::Vacc(new std::string("load_checkpoint"))),
              new Expr::Not(ast.grammar()->right_running_indices.at(track)))));
    }
  }

  // ==== single thread version
  fn_cyk->stmts.push_back(new Statement::CustomCode("#ifndef _OPENMP"));
  // recursively reverse iterate through tracks and create nested for loop
  // structures
  std::list<Statement::Base*> *stmts = cyk_traversal_singlethread(
      ast, ast.outside_generation() ? CYKmode::SINGLETHREAD_OUTSIDE : CYKmode::SINGLETHREAD);
//  // add NT calls to traversal structure
//  std::list<Statement::Base*> *new_stmts = add_nt_calls(*stmts,
//      new std::list<std::string*>(), ast.grammar()->topological_ord(),
//      ast.checkpoint && ast.checkpoint->cyk, CYKmode::SINGLETHREAD);
//  stmts->insert(stmts->end(), new_stmts->begin(), new_stmts->end());
//  // finally add traversal structure with NT calls to function body
  fn_cyk->stmts.insert(fn_cyk->stmts.end(), stmts->begin(), stmts->end());

  // ==== multi thread version (only single-track possible for now)
  fn_cyk->stmts.push_back(new Statement::CustomCode("#else"));
  // FIXME generalize for multi-track ...
  if (ast.grammar()->axiom->tracks() == 1) {
    std::string *name_maxtilen = new std::string("max_tiles_n");
    std::vector<Statement::Var_Decl*>::const_reverse_iterator it_stmt_seq =
        ast.seq_decls.rbegin();

    // FIXME abstract from unsigned int, int -> perhaps wait for OpenMP 3
    // since OpenMP < 3 doesn't allow unsigned int in workshared fors

    // header
    if (ast.checkpoint && ast.checkpoint->cyk) {
      std::tuple<std::list<Statement::Base*>*, Statement::Var_Decl*>
      stmts_tilesize = get_tile_computation(
          ast, *name_maxtilen, *it_stmt_seq, true);
      fn_cyk->stmts.insert(
          fn_cyk->stmts.end(), std::get<0>(stmts_tilesize)->begin(),
          std::get<0>(stmts_tilesize)->end());

      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_OuterLoop1) +
          "_loaded = !load_checkpoint || !" + VARNAME_OuterLoop1 + ";"));
      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_OuterLoop2) +
          "_loaded = !load_checkpoint || !" + VARNAME_OuterLoop2 + ";"));
      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_InnerLoop2) +
          "_loaded = !load_checkpoint || !" + VARNAME_InnerLoop2 + ";"));
      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_OuterLoop1) +
          "_start = (" + VARNAME_OuterLoop1 + "_loaded++) ? 0 : " +
          VARNAME_OuterLoop1 + ";"));
      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_OuterLoop2) +
          "_start = (" + VARNAME_OuterLoop2 + "_loaded++) ? tile_size : " +
          VARNAME_OuterLoop2 + ";"));
      fn_cyk->stmts.push_back(new Statement::CustomCode(
          "int " + std::string(VARNAME_InnerLoop2) +
          "_start = " + VARNAME_InnerLoop2 + ";"));
    }
    fn_cyk->stmts.push_back(new Statement::CustomCode("#pragma omp parallel"));
    Statement::Block *blk_parallel = new Statement::Block();
    std::tuple<std::list<Statement::Base*>*, Statement::Var_Decl*>
    stmts_tilesize = get_tile_computation(
        ast, *name_maxtilen, *it_stmt_seq, false);
    blk_parallel->statements.insert(blk_parallel->statements.end(),
        std::get<0>(stmts_tilesize)->begin(),
        std::get<0>(stmts_tilesize)->end());
    if (ast.checkpoint && ast.checkpoint->cyk) {
      blk_parallel->statements.push_back(new Statement::CustomCode(
          "#pragma omp for ordered schedule(dynamic)"));
    } else {
      blk_parallel->statements.push_back(new Statement::CustomCode(
          "#pragma omp for"));
    }
    blk_parallel->statements.push_back(new Statement::CustomCode(
        "// OPENMP < 3 requires signed int here ..."));

    // parallel part
    std::list<Statement::Base*> *stmts = cyk_traversal_multithread_parallel(
        ast, *it_stmt_seq, std::get<1>(stmts_tilesize), name_maxtilen,
        ast.checkpoint && ast.checkpoint->cyk);
    // inject NT calls
    std::list<Statement::Base*> *new_stmts = add_nt_calls(*stmts,
        new std::list<std::string*>(), ast.grammar()->topological_ord(),
        ast.checkpoint && ast.checkpoint->cyk, CYKmode::OPENMP_PARALLEL);
    stmts->insert(stmts->end(), new_stmts->begin(), new_stmts->end());

    blk_parallel->statements.insert(blk_parallel->statements.end(),
        stmts->begin(), stmts->end());
    blk_parallel->statements.push_back(new Statement::CustomCode(
        "// end parallel"));
    fn_cyk->stmts.push_back(blk_parallel);

    // serial part
    fn_cyk->stmts.insert(fn_cyk->stmts.end(),
        std::get<0>(stmts_tilesize)->begin(),
        std::get<0>(stmts_tilesize)->end());
    stmts = cyk_traversal_singlethread(ast, CYKmode::OPENMP_SERIAL);
    // inject NT calls
    std::list<Statement::Base*> *new_serial_stmts = add_nt_calls(
        *stmts, new std::list<std::string*>(), ast.grammar()->topological_ord(),
        ast.checkpoint && ast.checkpoint->cyk, CYKmode::OPENMP_SERIAL);
    stmts->insert(stmts->end(), new_serial_stmts->begin(),
        new_serial_stmts->end());
    fn_cyk->stmts.insert(fn_cyk->stmts.end(), stmts->begin(), stmts->end());
  }

  fn_cyk->stmts.push_back(new Statement::CustomCode("#endif"));

  return fn_cyk;
}




