/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2017, ruki All rights reserved.
 *
 * @author      ruki
 * @file        scheduler.c
 * @ingroup     coroutine
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "scheduler"
#define TB_TRACE_MODULE_DEBUG           (0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "scheduler.h"
#include "coroutine.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

// the dead cache maximum count
#ifdef __tb_small__
#   define TB_SCHEDULER_DEAD_CACHE_MAXN     (64)
#else
#   define TB_SCHEDULER_DEAD_CACHE_MAXN     (256)
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
static tb_void_t tb_scheduler_dead(tb_scheduler_t* scheduler, tb_coroutine_t* coroutine)
{
    // check
    tb_assert(scheduler && coroutine);

    // trace
    tb_trace_d("dead coroutine(%p)", coroutine);

    // cannot be original coroutine
    tb_assert(!tb_coroutine_is_original(coroutine));

#ifdef __tb_debug__
    // check it
    tb_coroutine_check(coroutine);
#endif

    // mark this coroutine as dead
    tb_coroutine_state_set(coroutine, TB_STATE_DEAD);

    // append this coroutine to dead coroutines
    tb_single_list_entry_insert_tail(&scheduler->coroutines_dead, &coroutine->entry);
}
static tb_void_t tb_scheduler_ready(tb_scheduler_t* scheduler, tb_coroutine_t* coroutine)
{
    // check
    tb_assert(scheduler && coroutine);

    // trace
    tb_trace_d("ready coroutine(%p)", coroutine);

    // mark this coroutine as ready
    tb_coroutine_state_set(coroutine, TB_STATE_READY);

    // append this coroutine to ready coroutines
    tb_single_list_entry_insert_tail(&scheduler->coroutines_ready, &coroutine->entry);
}
static tb_coroutine_t* tb_scheduler_next(tb_scheduler_t* scheduler)
{
    // check
    tb_assert(scheduler);
 
    // no more?
    if (!tb_single_list_entry_size(&scheduler->coroutines_ready)) return tb_null;

    // get the next entry from head
    tb_single_list_entry_ref_t entry = tb_single_list_entry_head(&scheduler->coroutines_ready);
    tb_assert(entry);

    // remove it from the ready coroutines
    tb_single_list_entry_remove_head(&scheduler->coroutines_ready);

    // trace
    tb_trace_d("get next coroutine(%p)", tb_single_list_entry(&scheduler->coroutines_ready, entry));

    // return this coroutine
    return (tb_coroutine_t*)tb_single_list_entry(&scheduler->coroutines_ready, entry);
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_bool_t tb_scheduler_start(tb_scheduler_t* scheduler, tb_coroutine_func_t func, tb_cpointer_t priv, tb_size_t stacksize)
{
    // check
    tb_assert(func);

    // done
    tb_bool_t       ok = tb_false;
    tb_coroutine_t* coroutine = tb_null;
    do
    {
        // trace
        tb_trace_d("start ..");

        // uses the current scheduler if be null
        if (!scheduler) scheduler = (tb_scheduler_t*)tb_scheduler_self();
        tb_assert_and_check_break(scheduler);

        // reuses dead coroutines in init function
        if (tb_single_list_entry_size(&scheduler->coroutines_dead))
        {
            // get the next entry from head
            tb_single_list_entry_ref_t entry = tb_single_list_entry_head(&scheduler->coroutines_dead);
            tb_assert_and_check_break(entry);

            // remove it from the ready coroutines
            tb_single_list_entry_remove_head(&scheduler->coroutines_dead);

            // get the dead coroutine
            tb_coroutine_t* coroutine_dead = (tb_coroutine_t*)tb_single_list_entry(&scheduler->coroutines_dead, entry);

            // reinit this coroutine
            coroutine = tb_coroutine_reinit(coroutine_dead, func, priv, stacksize);

            // failed? exit this coroutine
            if (!coroutine) tb_coroutine_exit(coroutine_dead);
        }

        // init coroutine
        if (!coroutine) coroutine = tb_coroutine_init((tb_scheduler_ref_t)scheduler, func, priv, stacksize);
        tb_assert_and_check_break(coroutine);

        // ready coroutine
        tb_scheduler_ready(scheduler, coroutine);

        // the dead coroutines is too much? free some coroutines
        while (tb_single_list_entry_size(&scheduler->coroutines_dead) > TB_SCHEDULER_DEAD_CACHE_MAXN)
        {
            // get the next entry from head
            tb_single_list_entry_ref_t entry = tb_single_list_entry_head(&scheduler->coroutines_dead);
            tb_assert(entry);

            // remove it from the ready coroutines
            tb_single_list_entry_remove_head(&scheduler->coroutines_dead);

            // exit this coroutine
            tb_coroutine_exit((tb_coroutine_t*)tb_single_list_entry(&scheduler->coroutines_dead, entry));
        }

        // ok
        ok = tb_true;

    } while (0);

    // trace
    tb_trace_d("start %s", ok? "ok" : "no");

    // ok?
    return ok;
}
tb_void_t tb_scheduler_yield(tb_scheduler_t* scheduler)
{
    // check
    tb_assert(scheduler);

    // trace
    tb_trace_d("yield coroutine(%p)", tb_coroutine_self());

    // no more ready coroutines? return it directly and continue to run this coroutine
    if (!tb_single_list_entry_size(&scheduler->coroutines_ready))
    {
        // trace
        tb_trace_d("continue to run current coroutine(%p)", tb_coroutine_self());
        return ;
    }

    // ready the running coroutine
    tb_scheduler_ready(scheduler, scheduler->running);

    // get the next coroutine 
    tb_coroutine_t* coroutine = tb_scheduler_next(scheduler);
    tb_assert(coroutine);

    // switch to the next coroutine
    tb_scheduler_switch(scheduler, coroutine);
}
tb_void_t tb_scheduler_finish(tb_scheduler_t* scheduler)
{
    // check
    tb_assert(scheduler);

    // trace
    tb_trace_d("finish coroutine(%p)", tb_coroutine_self());

    // make the running coroutine as dead
    tb_scheduler_dead(scheduler, scheduler->running);

    // switch to other coroutine? 
    if (tb_single_list_entry_size(&scheduler->coroutines_ready))
    {
        // get the next coroutine 
        tb_coroutine_t* coroutine = tb_scheduler_next(scheduler);
        tb_assert(coroutine);

        // switch to the next coroutine
        tb_scheduler_switch(scheduler, coroutine);
    }
    // no more ready coroutines? 
    else
    {
        // trace
        tb_trace_d("switch to original from coroutine(%p)", tb_coroutine_self());

        // switch to the original coroutine
        tb_scheduler_switch(scheduler, &scheduler->original);
    }
}
tb_void_t tb_scheduler_sleep(tb_scheduler_t* scheduler, tb_size_t interval)
{
    // check
    tb_assert(scheduler);

    // sleep the coroutine
    // TODO
}
tb_coroutine_t* tb_scheduler_switch(tb_scheduler_t* scheduler, tb_coroutine_t* coroutine)
{
    // check
    tb_assert(scheduler && coroutine);

    // check
    tb_assert(coroutine && coroutine->context);

    // the current running coroutine
    tb_coroutine_t* running = scheduler->running;

    // mark the given coroutine as running
    tb_coroutine_state_set(coroutine, TB_STATE_RUNNING);
    scheduler->running = coroutine;

    // trace
    tb_trace_d("switch to coroutine(%p) from coroutine(%p)", coroutine, running);

    // jump to the given coroutine
    tb_context_from_t from = tb_context_jump(coroutine->context, running);

    // the from-coroutine 
    tb_coroutine_t* coroutine_from = (tb_coroutine_t*)from.priv;
    tb_assert(coroutine_from && from.context);

    // update the context
    coroutine_from->context = from.context;

#ifdef __tb_debug__
    // check it
    tb_coroutine_check(coroutine_from);
#endif

    // return the from-coroutine
    return coroutine_from;
}
