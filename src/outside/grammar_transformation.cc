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

#include "grammar_transformation.hh"

bool Grammar::check_outside_parse_empty_word() {
  if (this->ast.outside_generation()) {
    for (std::vector<Yield::Size>::const_iterator i =
         this->axiom->multi_ys().begin();
         i != this->axiom->multi_ys().end(); ++i) {
      if ((*i).low() > 0) {
        std::ostringstream msg;
        msg << "The minimal yield size of your grammar '" << *this->name
            << "' is ";
        (*i).low().put(msg);
        msg << ", i.e. it cannot parse the empty input string ''."
            << " For outside grammar generation, this means you are lacking a"
            << " recursion basis which will result in empty results for "
            << "ALL outside candidates! Try adding an alternative like "
            << "nil(EMPTY) to your axiom.";
        Log::instance()->warning(this->location, msg.str());
        return false;
      }
    }
  }
  return true;
}

void Grammar::check_outside_requested_nonexisting_nts() {
  /* double check that all NTs do exist that the user requested to
   * be reported in the outside grammar.
   */

  if (!this->ast.get_outside_nt_list()) {
    // the user did not request any outside NT to be reported
    return;
  }
  assert(this->ast.get_outside_nt_list());

  // a list that collects the names of non existent NTs
  std::list<std::string> *warn_missing_nts = new std::list<std::string>();

  // iterate through NTs which the user requested to be reported
  for (std::vector<std::string>::const_iterator i =
       this->ast.get_outside_nt_list()->begin();
       i != this->ast.get_outside_nt_list()->end(); ++i) {
    // ignore the special user input "ALL", since this is per definition not
    // a non-terminal in the grammar
    if ((*i).compare(OUTSIDE_ALL) == 0) {
      continue;
    }

    // next, check if NT with user given name is present in grammar
    if (this->NTs.find(*i) == this->NTs.end()) {
      // if not, add this NT name to the list
      warn_missing_nts->push_back(*i);
    }
  }

  if (warn_missing_nts->size() > 0) {
    std::string msg = std::string(
      "You requested outside grammar generation and\nreporting results for "
      "the following non-terminals, which do NOT exist in your grammar '" +
      *this->name + "':\n");
    for (std::list<std::string>::iterator i = warn_missing_nts->begin();
         i != warn_missing_nts->end(); ++i) {
      msg += "  '" + *i + "'";
      if (next(i) != warn_missing_nts->end()) {
        msg += "\n";
      }
    }
    throw LogError(this->location, msg);
  }
}

// traverses the grammar and collect all algebra function names used
// such that after traversal, we can ask if an algebra function,
// given its name, is actually part of the grammar
struct AlgfctUsedInGrammar : public Visitor {
 private:
  std::set<std::string> used_algfct;
  bool is_traversed = false;

 public:
  void visit(Alt::Base &a) {
    Alt::Simple *as = dynamic_cast<Alt::Simple*>(&a);
    if (as) {
      used_algfct.insert(*as->name);
    }
    is_traversed = true;
  }

  bool is_used(std::string *algfct_name) {
    assert(is_traversed);
    return used_algfct.find(*algfct_name) != used_algfct.end();
  }
};


bool is_terminal_type(Type::Base *t) {
  if ((t->is(Type::ALPHABET)) || (t->is(Type::VOID)) ||
      (t->is(Type::REALVOID)) || (t->is(Type::CHAR)) ||
      (t->is(Type::STRING)) || (t->is(Type::BOOL)) || (t->is(Type::SEQ)) ||
      (t->is(Type::SUBSEQ)) || (t->is(Type::INT)) || (t->is(Type::FLOAT)) ||
      (t->is(Type::RATIONAL))) {
    return true;
  }
  if ((t->is(Type::BIGINT)) || (t->is(Type::SHAPE)) || (t->is(Type::INTEGER))) {
    return false;
  }
  if ((t->is(Type::EXTERNAL)) || (t->is(Type::TUPLEDEF))) {
    return false;
  }
  if (t->is(Type::USAGE)) {
    return (t->is_terminal()) && (is_terminal_type(t->simple()));
  }
  if (t->is(Type::MULTI)) {
    Type::Multi *tm = dynamic_cast<Type::Multi*>(t);
    for (std::list<Type::Base*>::const_iterator i = tm->types().begin();
         i != tm->types().end(); ++i) {
      if (!is_terminal_type(*i)) {
        return false;
      }
    }
    return true;
  }
  if (t->is(Type::SINGLE)) {
    return t->is_terminal();
  }

  /* TODO(sjanssen): I need to find out if following types are either terminals
   * or not: Signature, Table, Base, List, Name, Tuple, Def, Choice, Size,
   *         Range, Table, Generic, Referencable
   */
  assert(false && "I have been lazy and did not yet define if above mentioned"
                  " types are terminals or not");
  return false;
}


bool Instance::check_multiple_answer_types(bool for_outside_generation) {
  if (!for_outside_generation) {
    // no need to check, if no outside transformation was requested
    return true;
  }

  AlgfctUsedInGrammar v = AlgfctUsedInGrammar();
  this->grammar_->traverse(v);

  // identify individual algebras used in the algebra product of the instance
  unsigned int num_errors = 0;
  for (Product::iterator i = Product::begin(this->product); i != Product::end();
       ++i) {
    if ((*i)->is(Product::SINGLE)) {
      Algebra *alg = dynamic_cast<Product::Single*>(*i)->algebra();
      for (hashtable<std::string, Fn_Def*>::const_iterator i = alg->fns.begin();
           i != alg->fns.end(); ++i) {
        Fn_Decl *algfct = (*i).second;

        // do not check choice functions
        if (algfct->is_Choice_Fn()) {
          continue;
        }

        // ignore algebra function if not used in instance' grammar, i.e.
        // it might be declared in signature and algebra(s) but not used
        // in grammar definition
        if (!v.is_used(algfct->name)) {
          continue;
        }

        // only check algebra functions whose return type is NOT a terminal
        // (type)
        if (!is_terminal_type(algfct->return_type)) {
          for (std::list<Type::Base*>::const_iterator t = algfct->types.begin();
               t != algfct->types.end(); ++t) {
            // only check rhs components that are not terminal (types)
            if (!is_terminal_type(*t)) {
              // check if return type is NOT equal to non-terminal types on the
              // rhs
              if (!algfct->return_type->simple()->is_eq(*(*t)->simple())) {
                std::ostringstream msg;
                msg << "return type '"
                    << *algfct->return_type
                    << "' is different to the type '"
                    << *(*t)
                    << "',\nwhich you are using on the r.h.s. of the function "
                    << "definition '"
                    << *(algfct->name)
                    << "' in algebra '"
                    << *(alg->name)
                    << "'.\nThis will lead to a compile error, since"
                    << " you requested outside grammar generation.\nThe outside"
                    << " grammar parts will contain production rules where "
                    << "l.h.s. and r.h.s. non-termials of '" + (*(algfct->name))
                    << + "' are swapped,\nbut we lack definitions for these "
                    << "swapped versions in your algebras!";
                Log::instance()->error(alg->location, "type mismatch");
                Log::instance()->error((*t)->location, msg.str());
                num_errors++;

                // one warning per algebra function should be enough
                break;
              }
            }
          }
        }
      }
    }
  }

  return num_errors == 0;
}

/* iterates through one lhs NT and reports the first occurrence of an
 * Alt::Block, i.e.
 * - hold a pointer to the Alt::Block,
 * - hold a pointer to the top level Alt::Base on the rhs of the NT that holds
 *   the Alt::Block
 * - and either
 *   + a pointer to the Symbol::NT, if the Block is on the top level rhs
 *   + or a pointer to the Alt::Base which is the parent of the Alt::Block
 *     together with the a pointer to the "Handle" (= Fn_Arg::Alt) enclosing
 *     the Alt::Block */
struct FindFirstBlock : public Visitor {
  // pointer to the first found block
  Alt::Block *block = nullptr;

  // pointer to the Fn_Arg::Alt that encloses the first found block - iff it's
  // parent is an Alt::Base
  Fn_Arg::Alt *block_fnarg = nullptr;

  // the top level alternative that contains (somewhere) the first found block
  Alt::Base *topalt = nullptr;

  // the direct Alt::Base parent of the first found block - iff it is not a
  // Symbol::NT
  Alt::Base *parent_alt = nullptr;

  // the direct Symbol::NT parent of the first found block - iff it is not an
  // Alt::Block
  Symbol::NT *parent_nt = nullptr;

  FindFirstBlock() : block(nullptr), block_fnarg(nullptr), parent_alt(nullptr),
                     parent_nt(nullptr) {
  }

  void visit(Symbol::NT &nt) {
    if (!block) {
      parent_alt = nullptr;
      block_fnarg = nullptr;
      parent_nt = &nt;
    }
  }
  void visit_itr(Symbol::NT &nt) {
    if (!block) {
      parent_alt = nullptr;
      block_fnarg = nullptr;
      parent_nt = &nt;
    }
  }

  void visit_begin(Alt::Simple &alt) {
    if (!block) {
      parent_alt = &alt;
      parent_nt = nullptr;
      if (alt.top_level) {
        topalt = &alt;
      }
    }
  }
  void visit(Alt::Link &alt) {
    // can only point to a rhs non-terminal
  }
  void visit_begin(Alt::Block &alt) {
    if ((!block) && (alt.alts.size() > 0)) {
      block = &alt;
      if (alt.top_level) {
        topalt = &alt;
      }
    }
  }
  void visit(Alt::Multi &alt) {
    if (!block) {
      parent_alt = &alt;
      parent_nt = nullptr;
      if (alt.top_level) {
        topalt = &alt;
      }
    }
  }

  void visit(Fn_Arg::Alt &arg) {
    if (!block) {
      block_fnarg = &arg;
    }
  }

  void visit(Grammar &g) {
    throw LogError(
      "Please only apply at individual NTs, not the full grammar!");
  }
};

void resolve_blocks(Symbol::NT *nt) {
  if (nt) {
    // check if there is any Alt::Block at the rhs of the NT
    FindFirstBlock v_block = FindFirstBlock();
    nt->traverse(v_block);

    // iterate through all alternatives until no more Alt::Block can be found
    while (v_block.block) {
      // determine the top level alternative in rhs of NT that holds the
      // Alt::Block
      std::list<Alt::Base*>::iterator topalt = nt->alts.begin();
      for (; topalt != nt->alts.end(); ++topalt) {
        if ((*topalt) == v_block.topalt) {
          break;
        }
      }

      // Alt::Block can either occur within an algebra function like
      // struct = cadd(foo, {joe, user})
      if (v_block.parent_alt && !v_block.parent_nt) {
        if (v_block.parent_alt->is(Alt::SIMPLE)) {
          // parent of the block is an Alt::Simple, i.e. has a list of children
          for (std::list<Alt::Base*>::iterator child =
               v_block.block->alts.begin();
               child != v_block.block->alts.end(); ++child) {
            // create a clone of the full alternative (up to the top level) that
            // contains this block. This will invalidate all pointer information
            // we have for the block ...
            Alt::Base *clone = (*v_block.topalt).clone();

            // ... thus acquire these info again, but for the clone, which is
            // not yet part of any non-terminal
            FindFirstBlock v_clone = FindFirstBlock();
            clone->traverse(v_clone);

            // now replace the block in the clone with the child of the original
            // block
            v_clone.block_fnarg->alt = *child;

            // carry over filters that are attached to the block, from the block
            // to the child in the clone
            v_clone.block_fnarg->alt->filters.insert(
              v_clone.block_fnarg->alt->filters.end(),
              v_block.block->filters.begin(),
              v_block.block->filters.end());
            v_clone.block_fnarg->alt->multi_filter.insert(
              v_clone.block_fnarg->alt->multi_filter.end(),
              v_block.block->multi_filter.begin(),
              v_block.block->multi_filter.end());

            // insert new (partially, since it can still hold further Blocks)
            // alternative into rhs of the NT
            nt->alts.insert(topalt, clone);
          }
          // remove original top-alternative, which holds the found Alt::Block
          nt->alts.remove(v_block.topalt);
        } else if (v_block.parent_alt->is(Alt::LINK)) {
          throw LogError("a Link is a leaf and thus cannot contain a block!");
        } else if (v_block.parent_alt->is(Alt::BLOCK)) {
          throw LogError("parent block should have been removed already!");
        } else if (v_block.parent_alt->is(Alt::MULTI)) {
          throw LogError("Alternative is not allowed in Multi-Track link.");
        } else {
          throw LogError("this is an unknown Alt subclass");
        }

      // or directly as a top level alternative of the non-termial,
      // like struct = {joe, user}
      } else if (!v_block.parent_alt && v_block.parent_nt) {
        for (std::list<Alt::Base*>::iterator child =
             v_block.block->alts.begin();
             child != v_block.block->alts.end(); ++child) {
          Alt::Base *clone = (*child)->clone();

          // since parent is lhs non-terminal and block itself will be removed,
          // children will become top level alternatives
          clone->top_level = Bool(true);

           // don't forget to carry over filters ...
          clone->filters.insert(clone->filters.end(),
              v_block.block->filters.begin(),
              v_block.block->filters.end());

          // ... and filters for multitrack
          clone->multi_filter.insert(clone->multi_filter.end(),
              v_block.block->multi_filter.begin(),
              v_block.block->multi_filter.end());

          // insert new (partially, since it can still hold further Blocks)
          // alternative into rhs of the NT
          nt->alts.insert(topalt, clone);
        }

        nt->alts.remove(*topalt);
      } else {
        throw LogError("each Alt::Block should have a parent!");
      }

      // check if there exist further Alt::Blocks, if not, we exit the while
      // loop
      v_block = FindFirstBlock();
      nt->traverse(v_block);
    }
  }
}

/* iterates through the rhs alternatives of an NT and creates a clone of an
 * alternative where ONE (but not all) rhs NT is swapped with the lhs NT, e.g.
 *   struct = cadd(dangle, weak) | sadd(BASE, struct) will result in
 * a) outside_dangle = cadd(outside_struct, weak)
 * b) outside_weak = cadd(dangle, outside_struct)
 * c) outside_struct = sadd(BASE, outside_struct) */
struct Flip_lhs_rhs_nonterminals : public Visitor {
  /* a list to store all newly generated clone alternatives. Each entry is a
   * pair to save the new lhs non-terminal together with the modified rhs
   * alternative */
  std::list<std::pair<Symbol::NT*, Alt::Base*> > *alt_clones;

  // a clone of the original inside lhs NT
  Symbol::NT *lhs_nt;

  // the rhs top level alternative
  Alt::Base *topalt = nullptr;

  Flip_lhs_rhs_nonterminals(Symbol::NT *nt) {
    // initialize the for now empty list of flipped alternative production rules
    alt_clones = new std::list<std::pair<Symbol::NT*, Alt::Base*> >();

    // clone the given inside lhs NT
    lhs_nt = nt->clone(nt->track_pos(), true);
    // and prefix it's name with "outside_"
    lhs_nt->name = new std::string(OUTSIDE_NT_PREFIX + *(nt->name));
    lhs_nt->orig_name = lhs_nt->name;
    // remove all alternatives
    lhs_nt->alts.clear();
  }
  void visit(Alt::Base &alt) {
    if (alt.top_level) {
      // record the current top level alternative. Starting point for cloning
      topalt = &alt;
    }
  }
  void visit(Alt::Link &alt) {
    // skip links to terminal parser
    if (alt.nt->is(Symbol::NONTERMINAL)) {
      /* a bit hacky: we need to create exact copies of the inside alternative
       * production rule, but if we clone all components will have different
       * pointers as they are different objects. Thus, we
       *   a) safely store the original rhs NT (orig_rhs_nt) away
       *   b) create a second clone of the rhs NT, but prefix its name with
       *      "outside_" and remove all alternatives
       *   c) next, we overwrite the current rhs NT of the Alt::Link with the
       *      lhs NT (which was already prefixed with "outside_")
       *   d) NOW clone the modified production rule
       *   e) restore the state before cloning of the inside production rule
       */

      // a)
      Symbol::NT *orig_rhs_nt = dynamic_cast<Symbol::NT*>(alt.nt)->clone(dynamic_cast<Symbol::NT*>(alt.nt)->track_pos(), true);

      // b)
      Symbol::NT *outside_rhs_nt = dynamic_cast<Symbol::NT*>(alt.nt)->clone(dynamic_cast<Symbol::NT*>(alt.nt)->track_pos(), true);
      outside_rhs_nt->name = new std::string(OUTSIDE_NT_PREFIX + *(outside_rhs_nt->name));
      outside_rhs_nt->orig_name = outside_rhs_nt->name;
      outside_rhs_nt->alts.clear();

      // c)
      alt.nt = lhs_nt;
      alt.m_ys = lhs_nt->multi_ys();
      alt.name = lhs_nt->name;

      // d)
      alt_clones->push_back(std::make_pair(outside_rhs_nt, topalt->clone()));

      // e)
      alt.nt = orig_rhs_nt;
      alt.m_ys = orig_rhs_nt->multi_ys();
      alt.name = orig_rhs_nt->name;
    }
  }

  void visit(Grammar &g) {
    throw LogError(
      "Please only apply at individual NTs, not the full grammar!");
  }
};

struct Count_rhsNTs : public Visitor {
  unsigned int rhs_nts = 0;

  std::set<Symbol::NT*> *axiom_candidates;
  hashtable<std::string, Symbol::Base*> outside_nts;

  Count_rhsNTs(hashtable<std::string, Symbol::Base*> outside_nts) : outside_nts(outside_nts) {
    axiom_candidates = new std::set<Symbol::NT*>();
  }

  void visit(Alt::Link &alt) {
    if (alt.nt->is(Symbol::NONTERMINAL)) {
      rhs_nts++;
    }
  }

  void visit_itr(Symbol::NT &nt) {
    if (rhs_nts == 0) {
      hashtable<std::string, Symbol::Base*>::iterator it_outside_nt = outside_nts.find(std::string(OUTSIDE_NT_PREFIX + *nt.name));
      if (it_outside_nt != outside_nts.end()) {
        axiom_candidates->insert(dynamic_cast<Symbol::NT*>((*it_outside_nt).second));
      }
    }
    rhs_nts = 0;  // for next iteration
  }

  void visit_end(Grammar &g) {
    if (axiom_candidates->size() == 1) {
      // if there is only one candidate NT, we simple make this NT the new axiom
      g.axiom_name = (*axiom_candidates->begin())->name;
    } else if (axiom_candidates->size() > 1) {
      // it is more complicated if there are several NTs
      // we then need to create a novel lhs NT ...
      //std::string *axiom_name = new std::string(*OUTSIDE_NT_PREFIX + "_axioms");
      std::string *axiom_name = new std::string(OUTSIDE_NT_PREFIX + std::string("axioms"));
      hashtable<std::string, Symbol::Base*>::iterator it_ntclash = g.NTs.find(*axiom_name);
      if (it_ntclash != g.NTs.end()) {
        throw LogError((*it_ntclash).second->location, "Please avoid using '" + *axiom_name + "' as l.h.s. non-terminal name, when requesting outside grammar generation!");
      }
      Symbol::NT *nt_axiom = new Symbol::NT(axiom_name, Loc());
      nt_axiom->name = axiom_name;
      nt_axiom->orig_name = axiom_name;

      // TODO can't I clone inside NT?
      // carry over tracks from original inside axiom
      nt_axiom->set_tracks(g.axiom->tracks(), g.axiom->track_pos());
      nt_axiom->setup_multi_ys();

      for (std::set<Symbol::NT*>::iterator i = axiom_candidates->begin();
           i != axiom_candidates->end(); ++i) {
        Alt::Link *link = new Alt::Link((*i)->name, Loc());
        link->nt = *i;
        link->set_tracks((*i)->tracks(), (*i)->track_pos());
        link->init_multi_ys();
        nt_axiom->alts.push_back(link);
      }
      // add new lhs non-terminal to grammar
      g.add_nt(nt_axiom);

      g.axiom_name = axiom_name;

    }
    g.init_axiom();
  }
};

void inject_outside_axiom(Grammar *grammar, hashtable<std::string, Symbol::Base*> outside_nts) {
  /* inside production rules that have NO non-terminals on their right hand
   * sides must be those that parse the final sub-words of the input.
   * Therefore, they must be the smallest start-points for outside
   * construction, i.e. the axiom should point to them. */
  std::set<Symbol::NT*> *axiom_candidates = new std::set<Symbol::NT*>();
  // check all alternatives, if they do NOT use any non-terminals on their rhs
  // if so, the NT must become one of the outside axioms
  for (hashtable<std::string, Symbol::Base*>::iterator i = grammar->NTs.begin();
       i != grammar->NTs.end(); ++i) {
    if ((*i).second->is(Symbol::NONTERMINAL)) {
      Symbol::NT *nt_inside = dynamic_cast<Symbol::NT*>((*i).second);
      for (std::list<Alt::Base*>::const_iterator a = nt_inside->alts.begin(); a != nt_inside->alts.end(); ++a) {
        Count_rhsNTs v = Count_rhsNTs(outside_nts);
        (*a)->traverse(v);
        if (v.rhs_nts == 0) {
          std::string he = std::string(OUTSIDE_NT_PREFIX + *nt_inside->name);
          hashtable<std::string, Symbol::Base*>::iterator it_outside_nt = outside_nts.find(he);
          if (it_outside_nt != outside_nts.end()) {
            axiom_candidates->insert(dynamic_cast<Symbol::NT*>((*it_outside_nt).second));
          }
        }
      }
    }
  }

  if (axiom_candidates->size() == 1) {
    // if there is only one candidate NT, we simple make this NT the new axiom
    grammar->axiom_name = (*axiom_candidates->begin())->name;
  } else if (axiom_candidates->size() > 1) {
    // it is more complicated if there are several NTs
    // we then need to create a novel lhs NT ...
    //std::string *axiom_name = new std::string(*OUTSIDE_NT_PREFIX + "_axioms");
    std::string *axiom_name = new std::string(OUTSIDE_NT_PREFIX + std::string("axioms"));
    hashtable<std::string, Symbol::Base*>::iterator it_ntclash = grammar->NTs.find(*axiom_name);
    if (it_ntclash != grammar->NTs.end()) {
      throw LogError((*it_ntclash).second->location, "Please avoid using '" + *axiom_name + "' as l.h.s. non-terminal name, when requesting outside grammar generation!");
    }
    Symbol::NT *nt_axiom = new Symbol::NT(axiom_name, Loc());
    nt_axiom->name = axiom_name;
    nt_axiom->orig_name = axiom_name;

    // TODO can't I clone inside NT?
    // carry over tracks from original inside axiom
    nt_axiom->set_tracks(grammar->axiom->tracks(), grammar->axiom->track_pos());
    nt_axiom->setup_multi_ys();

    for (std::set<Symbol::NT*>::iterator i = axiom_candidates->begin();
         i != axiom_candidates->end(); ++i) {
      Alt::Link *link = new Alt::Link((*i)->name, Loc());
      link->nt = *i;
      link->set_tracks((*i)->tracks(), (*i)->track_pos());
      link->init_multi_ys();
      nt_axiom->alts.push_back(link);
    }
    // add new lhs non-terminal to grammar
    grammar->add_nt(nt_axiom);

    grammar->axiom_name = axiom_name;

  }
  grammar->init_axiom();
}

void inject_outside_inside_transition(Grammar *grammar, Symbol::NT* target) {
  Alt::Link *link = new Alt::Link(grammar->axiom_name, grammar->axiom_loc);
  link->nt = dynamic_cast<Symbol::Base*>(grammar->axiom);
  Filter *f = new Filter(new std::string("complete_track"), Loc());
  f->type = Filter::WITH;
  std::list<Filter*> *comptracks = new std::list<Filter*>();
  for (unsigned int track = 0; track < grammar->axiom->tracks(); ++track) {
    comptracks->push_back(f);
  }
  link->set_tracks(grammar->axiom->tracks(), grammar->axiom->track_pos());
  link->init_multi_ys();
//  link->is_outside_inside_transition = true;
  if (link->nt->tracks() == 1) {
    link->filters.push_back(f);
  } else {
    link->add_multitrack_filter(*comptracks, f->type, Loc());
  }
  link->top_level = Bool(true);
  target->alts.push_back(link);
}

void Grammar::convert_to_outside() {
  hashtable<std::string, Symbol::Base*> outside_nts;

  for (hashtable<std::string, Symbol::Base*>::iterator i = NTs.begin();
       i != NTs.end(); ++i) {
    if ((*i).second->is(Symbol::NONTERMINAL)) {
      Symbol::NT *nt_inside = dynamic_cast<Symbol::NT*>((*i).second);

      // don't operate on the original inside non-terminal, but on a clone in which Alt::Block applications have been resolved
      Symbol::NT *nt_inside_resolved = nt_inside->clone(nt_inside->track_pos(), true);
      resolve_blocks(nt_inside_resolved);

      Flip_lhs_rhs_nonterminals v = Flip_lhs_rhs_nonterminals(nt_inside_resolved);
      nt_inside_resolved->traverse(v);

      // add new alternatives and new non-terminals to existing grammar
      for (std::list<std::pair<Symbol::NT*, Alt::Base*> >::iterator i = v.alt_clones->begin(); i != v.alt_clones->end(); ++i) {
        std::string *nt_name = (*i).first->name;
        hashtable<std::string, Symbol::Base*>::iterator it_nt = outside_nts.find(*nt_name);
        if (it_nt == outside_nts.end()) {
          outside_nts[*nt_name] = (*i).first;
        }
        it_nt = outside_nts.find(*nt_name);

        dynamic_cast<Symbol::NT*>((*it_nt).second)->alts.push_back((*i).second);
      }
    }
  }

  // now add the new outside NTs to the grammar
  for (hashtable<std::string, Symbol::Base*>::iterator i = outside_nts.begin();
       i != outside_nts.end(); ++i) {
    this->add_nt(dynamic_cast<Symbol::NT*>((*i).second));
  }

  // inject one alternative to the inside axiom which enables the transition
  // from outside parts into the original inside part of the grammar
  inject_outside_inside_transition(this, dynamic_cast<Symbol::NT*>((*outside_nts.find(std::string(OUTSIDE_NT_PREFIX + *this->axiom_name))).second));

  Count_rhsNTs v = Count_rhsNTs(outside_nts);
  this->traverse(v);
//  // inject new outside axiom
//  inject_outside_axiom(this, outside_nts);

  /* NT-table dimension optimization (all start quadratic, but depending on
   * yield size, some NT-tables can be reduced to linear or even constant
   * "tables") must be re-considered, since original inside NTs will now
   * be called from outside NTs and former constant tables (e.g. consuming
   * the full input) might now be within a variable infix size and thus
   * must become quadratic again.
   * We therefore here reset the flag of all NTs to enable re-computation
   * of optimal table dimensions ... within the outside context.
   */
//  for (hashtable<std::string, Symbol::Base*>::iterator i = NTs.begin();
//       i != NTs.end(); ++i) {
//    if ((*i).second->is(Symbol::NONTERMINAL)) {
//      dynamic_cast<Symbol::NT*>((*i).second)->reset_table_dim();
//    }
//  }
//
//  /* re-run "check_semantics" to properly initialize novel non-
//   * terminals, links to non-terminals, update yield size analysis and
//   * update table dimensions for NTs
//   */
//  this->check_semantic();

  Log::instance()->verboseMessage(
    "Grammar has been modified into an outside version.");

//  unsigned int nodeID = 1;
//  this->to_dot(&nodeID, std::cerr, 1);
}
