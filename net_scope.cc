/*
 * Copyright (c) 2000-2010 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

# include "config.h"
# include "compiler.h"

# include  "netlist.h"
# include  "netenum.h"
# include  <cstring>
# include  <cstdlib>
# include  <sstream>
# include  "ivl_assert.h"
# include  "PExpr.h"

/*
 * The NetScope class keeps a scope tree organized. Each node of the
 * scope tree points to its parent, its right sibling and its leftmost
 * child. The root node has no parent or siblings. The node stores the
 * name of the scope. The complete hierarchical name of the scope is
 * formed by appending the path of scopes from the root to the scope
 * in question.
 */

NetScope::NetScope(NetScope*up, const hname_t&n, NetScope::TYPE t)
: type_(t), name_(n), up_(up)
{
      events_ = 0;
      lcounter_ = 0;
      is_auto_ = false;
      is_cell_ = false;

      if (up) {
	    time_unit_ = up->time_unit();
	    time_prec_ = up->time_precision();
	    time_from_timescale_ = up->time_from_timescale();
	      // Need to check for duplicate names?
	    up_->children_[name_] = this;
      } else {
	    time_unit_ = 0;
	    time_prec_ = 0;
	    time_from_timescale_ = false;
	    assert(t == MODULE);
      }

      switch (t) {
	  case NetScope::TASK:
	    task_ = 0;
	    break;
	  case NetScope::FUNC:
	    func_ = 0;
	    break;
	  case NetScope::MODULE:
	    module_name_ = perm_string();
	    break;
	  default:  /* BEGIN_END and FORK_JOIN, do nothing */
	    break;
      }
      lineno_ = 0;
      def_lineno_ = 0;
      genvar_tmp_val = 0;
}

NetScope::~NetScope()
{
      lcounter_ = 0;

	/* name_ and module_name_ are perm-allocated. */
}

void NetScope::set_line(const LineInfo*info)
{
      file_ = info->get_file();
      def_file_ = file_;
      lineno_ = info->get_lineno();
      def_lineno_ = lineno_;
}

void NetScope::set_line(perm_string file, unsigned lineno)
{
      file_ = file;
      def_file_ = file;
      lineno_ = lineno;
      def_lineno_ = lineno;
}

void NetScope::set_line(perm_string file, perm_string def_file,
                        unsigned lineno, unsigned def_lineno)
{
      file_ = file;
      def_file_ = def_file;
      lineno_ = lineno;
      def_lineno_ = def_lineno;
}

void NetScope::set_parameter(perm_string key, PExpr*val,
			     ivl_variable_type_t type__,
			     PExpr*msb, PExpr*lsb, bool signed_flag,
			     NetScope::range_t*range_list,
			     const LineInfo&file_line)
{
      param_expr_t&ref = parameters[key];
      ref.msb_expr = msb;
      ref.lsb_expr = lsb;
      ref.val_expr = val;
      ref.val_scope = this;
      ref.type = type__;
      ref.msb = 0;
      ref.lsb = 0;
      ref.signed_flag = signed_flag;
      ivl_assert(file_line, ref.range == 0);
      ref.range = range_list;
      ref.val = 0;
      ref.set_line(file_line);

      ivl_assert(file_line, type__ != IVL_VT_NO_TYPE);
}

bool NetScope::auto_name(const char*prefix, char pad, const char* suffix)
{
	// Find the current reference to myself in the parent scope.
      map<hname_t,NetScope*>::iterator self = up_->children_.find(name_);
      assert(self != up_->children_.end());
      assert(self->second == this);

      char tmp[32];
      int pad_pos = strlen(prefix);
      int max_pos = sizeof(tmp) - strlen(suffix) - 1;
      strncpy(tmp, prefix, sizeof(tmp));
      tmp[31] = 0;

	// Try a variety of potential new names. Make sure the new
	// name is not in the parent scope. Keep looking until we are
	// sure we have a unique name, or we run out of names to try.
      while (pad_pos <= max_pos) {
	      // Try this name...
	    strcat(tmp + pad_pos, suffix);
	    hname_t new_name(lex_strings.make(tmp));
	    if (!up_->child(new_name)) {
		    // Ah, this name is unique. Rename myself, and
		    // change my name in the parent scope.
		  name_ = new_name;
		  up_->children_.erase(self);
		  up_->children_[name_] = this;
		  return true;
	    }
	    tmp[pad_pos++] = pad;
      }
      return false;
}

/*
 * Return false if the parameter does not already exist.
 * A parameter is not automatically created.
 */
bool NetScope::replace_parameter(perm_string key, PExpr*val, NetScope*scope)
{
      bool flag = false;

      if (parameters.find(key) != parameters.end()) {
	    param_expr_t&ref = parameters[key];
	    ref.val_expr = val;
            ref.val_scope = scope;
	    flag = true;
      }

      return flag;
}

/*
 * This is not really complete (msb, lsb, sign). It is currently only
 * used to add a genvar to the local parameter list.
 */
NetExpr* NetScope::set_localparam(perm_string key, NetExpr*val,
				  const LineInfo&file_line)
{
      param_expr_t&ref = localparams[key];
      NetExpr* res = ref.val;
      ref.msb_expr = 0;
      ref.lsb_expr = 0;
      ref.val_expr = 0;
      ref.val_scope = this;
      ref.type = IVL_VT_BOOL;
      ref.msb = 0;
      ref.lsb = 0;
      ref.signed_flag = false;
      ref.val = val;
      ref.set_line(file_line);
      return res;
}

/*
 * NOTE: This method takes a const char* as a key to lookup a
 * parameter, because we don't save that pointer. However, due to the
 * way the map<> template works, we need to *cheat* and use the
 * perm_string::literal method to fake the compiler into doing the
 * compare without actually creating a perm_string.
 */
const NetExpr* NetScope::get_parameter(Design*des,
				       const char* key,
				       const NetExpr*&msb,
				       const NetExpr*&lsb)
{
      return get_parameter(des, perm_string::literal(key), msb, lsb);
}

const NetExpr* NetScope::get_parameter(Design*des,
				       perm_string key,
				       const NetExpr*&msb,
				       const NetExpr*&lsb)
{
      map<perm_string,param_expr_t>::iterator idx;

      idx = parameters.find(key);
      if (idx != parameters.end()) {
            if (idx->second.val_expr)
                  evaluate_parameter_(des, idx);

	    msb = idx->second.msb;
	    lsb = idx->second.lsb;
	    return idx->second.val;
      }

      idx = localparams.find(key);
      if (idx != localparams.end()) {
            if (idx->second.val_expr)
                  evaluate_parameter_(des, idx);

	    msb = idx->second.msb;
	    lsb = idx->second.lsb;
	    return idx->second.val;
      }

      map<perm_string,NetEConstEnum*>::const_iterator eidx;

      eidx = enum_names_.find(key);
      if (eidx != enum_names_.end()) {
	    msb = 0;
	    lsb = 0;
	    return eidx->second;
      }

      return 0;
}

NetScope::param_ref_t NetScope::find_parameter(perm_string key)
{
      map<perm_string,param_expr_t>::iterator idx;

      idx = parameters.find(key);
      if (idx != parameters.end()) return idx;

      idx = localparams.find(perm_string::literal(key));
      if (idx != localparams.end()) return idx;

	// To get here the parameter must already exist, so we should
	// never get here.
      assert(0);
}

NetScope::TYPE NetScope::type() const
{
      return type_;
}

void NetScope::print_type(ostream&stream) const
{
      switch (type_) {
	case BEGIN_END:
	    stream << "sequential block";
	    break;
	case FORK_JOIN:
	    stream << "parallel block";
	    break;
	case FUNC:
	    stream << "function";
	    break;
	case MODULE:
	    stream << "module <" << (module_name_ ? module_name_.str() : "")
	           << "> instance";
	    break;
	case TASK:
	    stream << "task";
	    break;
	case GENBLOCK:
	    stream << "generate block";
	    break;
      }
}

void NetScope::set_task_def(NetTaskDef*def)
{
      assert( type_ == TASK );
      assert( task_ == 0 );
      task_ = def;
}

NetTaskDef* NetScope::task_def()
{
      assert( type_ == TASK );
      return task_;
}

const NetTaskDef* NetScope::task_def() const
{
      assert( type_ == TASK );
      return task_;
}

void NetScope::set_func_def(NetFuncDef*def)
{
      assert( type_ == FUNC );
      assert( func_ == 0 );
      func_ = def;
}

NetFuncDef* NetScope::func_def()
{
      assert( type_ == FUNC );
      return func_;
}

bool NetScope::in_func() const
{
      return (type_ == FUNC) ? true : false;
}

const NetFuncDef* NetScope::func_def() const
{
      assert( type_ == FUNC );
      return func_;
}

void NetScope::set_module_name(perm_string n)
{
      assert(type_ == MODULE);
      module_name_ = n; /* NOTE: n must have been permallocated. */
}

perm_string NetScope::module_name() const
{
      assert(type_ == MODULE);
      return module_name_;
}

void NetScope::time_unit(int val)
{
      time_unit_ = val;
}

void NetScope::time_precision(int val)
{
      time_prec_ = val;
}

void NetScope::time_from_timescale(bool val)
{
      time_from_timescale_ = val;
}

int NetScope::time_unit() const
{
      return time_unit_;
}

int NetScope::time_precision() const
{
      return time_prec_;
}

bool NetScope::time_from_timescale() const
{
      return time_from_timescale_;
}

perm_string NetScope::basename() const
{
      return name_.peek_name();
}

void NetScope::add_event(NetEvent*ev)
{
      assert(ev->scope_ == 0);
      ev->scope_ = this;
      ev->snext_ = events_;
      events_ = ev;
}

void NetScope::rem_event(NetEvent*ev)
{
      assert(ev->scope_ == this);
      ev->scope_ = 0;
      if (events_ == ev) {
	    events_ = ev->snext_;

      } else {
	    NetEvent*cur = events_;
	    while (cur->snext_ != ev) {
		  assert(cur->snext_);
		  cur = cur->snext_;
	    }
	    cur->snext_ = ev->snext_;
      }

      ev->snext_ = 0;
}


NetEvent* NetScope::find_event(perm_string name)
{
      for (NetEvent*cur = events_;  cur ;  cur = cur->snext_)
	    if (cur->name() == name)
		  return cur;

      return 0;
}

void NetScope::add_genvar(perm_string name, LineInfo *li)
{
      assert((type_ == MODULE) || (type_ == GENBLOCK));
      genvars_[name] = li;
}

LineInfo* NetScope::find_genvar(perm_string name)
{
      if (genvars_.find(name) != genvars_.end())
	    return genvars_[name];
      else
            return 0;
}

void NetScope::add_signal(NetNet*net)
{
      signals_map_[net->name()]=net;
}

void NetScope::rem_signal(NetNet*net)
{
      assert(net->scope() == this);
      signals_map_.erase(net->name());
}

/*
 * This method looks for a signal within the current scope. The name
 * is assumed to be the base name of the signal, so no sub-scopes are
 * searched.
 */
NetNet* NetScope::find_signal(perm_string key)
{
      if (signals_map_.find(key)!=signals_map_.end())
	    return signals_map_[key];
      else
	    return 0;
}

NetNet* NetScope::find_signal(PEIdent*arg)
{
      const pform_name_t&path = arg->path();
      ivl_assert(*arg, path.size()==1);
      perm_string name = peek_tail_name(path);

      NetNet*sig = find_signal(name);
      return sig;
}

void NetScope::add_enumeration_set(netenum_t*enum_set)
{
      enum_sets_.push_back(enum_set);
}

bool NetScope::add_enumeration_name(netenum_t*enum_set, perm_string name)
{
      netenum_t::iterator enum_val = enum_set->find_name(name);
      assert(enum_val != enum_set->end_name());

      NetEConstEnum*val = new NetEConstEnum(this, name, enum_set, enum_val->second);

      pair<map<perm_string,NetEConstEnum*>::iterator, bool> cur;
      cur = enum_names_.insert(make_pair(name,val));

	// Return TRUE if the name is added (i.e. is NOT a duplicate.)
      return cur.second;
}

netenum_t*NetScope::enumeration_for_name(perm_string name)
{
      NetEConstEnum*tmp = enum_names_[name];
      assert(tmp != 0);

      return tmp->enumeration();
}

/*
 * This method locates a child scope by name. The name is the simple
 * name of the child, no hierarchy is searched.
 */
NetScope* NetScope::child(const hname_t&name)
{
      map<hname_t,NetScope*>::iterator cur = children_.find(name);
      if (cur == children_.end())
	    return 0;
      else
	    return cur->second;
}

const NetScope* NetScope::child(const hname_t&name) const
{
      map<hname_t,NetScope*>::const_iterator cur = children_.find(name);
      if (cur == children_.end())
	    return 0;
      else
	    return cur->second;
}

perm_string NetScope::local_symbol()
{
      ostringstream res;
      res << "_s" << (lcounter_++);
      return lex_strings.make(res.str());
}
