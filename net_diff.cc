/*
 * Copyright (c) 2011 Bernd Jendrissek (bernd.jendrissek@gmail.com)
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

# include  "config.h"
# include  "netlist.h"
# include  "netmisc.h"
# include  "target.h"
# include  <iostream>

NetExpr* NetExpr::differentiate() throw (const LineInfo*)
{
      throw this;
}

NetExpr* NetEAccess::differentiate() throw (const LineInfo*)
{
      // Can't further differentiate an access function.
      // TODO ddt_nature can help?  Example: ddt(Q(b1,b2)) == I(b1,b2).
      return 0;
}

NetEDerivative::NetEDerivative(NetExpr*arg)
: argument_(arg)
{
}

void NetEDerivative::expr_scan(struct expr_scan_t*tgt) const
{
      tgt->expr_derivative(this);
}

NetExpr* NetEDerivative::differentiate() throw (const LineInfo*)
{
      NetExpr* derivative = argument_->differentiate();
      if (!derivative) return 0;
      // Differentiation can create new constants, so evaluate afterward.
      eval_expr(derivative);

      return derivative;
}

NetExpr* NetEDerivative::eval_tree(int prune_to_width)
{

      try {
	    return differentiate();
      } catch (const LineInfo* li) {
	    cerr << li->get_fileline() << ": error: "
		 << "cannot differentiate: "
		 << *this;
	    const NetExpr* badexpr = dynamic_cast<const NetExpr*> (li);
	    if (badexpr) {
		  cerr << " (specifically, " << *badexpr << ")";
	    }
	    cerr << endl;
	    return 0;
      }
}

NetEDerivative::~NetEDerivative()
{
      delete argument_;
}
