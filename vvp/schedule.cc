/*
 * Copyright (c) 2001 Stephen Williams (steve@icarus.com)
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
#if !defined(WINNT)
#ident "$Id: schedule.cc,v 1.1 2001/03/11 00:29:39 steve Exp $"
#endif

# include  "schedule.h"
# include  "vthread.h"
# include  <malloc.h>
# include  <assert.h>

struct event_s {
      unsigned delay;

      vthread_t thr;
      vvp_ipoint_t fun;

      struct event_s*next;
      struct event_s*last;
};

static struct event_s* list = 0;

static void schedule_event_(struct event_s*cur)
{
      cur->last = cur;

      if (list == 0) {
	    list = cur;
	    cur->next = 0;
	    return;
      }

      struct event_s*idx = list;
      if (cur->delay < idx->delay) {
	    idx->delay -= cur->delay;
	    cur->next = idx;
	    list = cur;

      } else {
	    struct event_s*prev = idx;

	    while (cur->delay > idx->delay) {
		  cur->delay -= idx->delay;
		  prev = idx->last;
		  if (prev->next == 0) {
			cur->next = 0;
			prev->next = cur;
			return;
		  }
		  idx = prev->next;
	    }

	    if (cur->delay < idx->delay) {
		  idx->delay -= cur->delay;
		  cur->last = cur;
		  cur->next = idx;
		  prev->next = cur;

	    } else {
		  assert(cur->delay == idx->delay);
		  cur->delay = 0;
		  cur->last = cur;
		  cur->next = idx->last->next;
		  idx->last->next = cur;
		  idx->last = cur;
	    }
      }
}

void schedule_vthread(vthread_t thr, unsigned delay)
{
      struct event_s*cur = (struct event_s*)
	    calloc(1, sizeof(struct event_s));

      cur->delay = delay;
      cur->thr = thr;

      schedule_event_(cur);
}

void schedule_functor(vvp_ipoint_t fun, unsigned delay)
{
      struct event_s*cur = (struct event_s*)
	    calloc(1, sizeof(struct event_s));

      cur->delay = delay;
      cur->fun = fun;

      schedule_event_(cur);
}

static unsigned long schedule_time;

void schedule_simulate(void)
{
      schedule_time = 0;

      while (list) {

	      /* Pull the first item off the list. Fixup the last
		 pointer in the next cell, if necessary. */
	    struct event_s*cur = list;
	    list = cur->next;
	    if (cur->last != cur) {
		  assert(list->delay == 0);
		  list->last = cur->last;

	    } else {
		  schedule_time += cur->delay;
		  printf("TIME: %u\n", schedule_time);
	    }

	    if (cur->thr) {
		  vthread_run(cur->thr);

	    } else if (cur->fun) {
		    /* XXXX not implemented yet */

	    }

	    free(cur);
      }
}

/*
 * $Log: schedule.cc,v $
 * Revision 1.1  2001/03/11 00:29:39  steve
 *  Add the vvp engine to cvs.
 *
 */

