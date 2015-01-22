#include "dphold.h"
#include <assert.h>

tw_peid
phold_map(tw_lpid gid)
{
	return (tw_peid) gid / g_tw_nlp;
}

void
phold_init(phold_state * s, tw_lp * lp)
{
	int              i;

	if( stagger )
	  {
	    for (i = 0; i < g_phold_start_events; i++)
	      {
		tw_event_send(
			      tw_event_new(lp->gid, 
					   tw_rand_exponential(lp->rng, mean) + lookahead + (tw_stime)(lp->gid % (unsigned int)g_tw_ts_end), 
					   lp));
	      }
	  }
	else
	  {
	    for (i = 0; i < g_phold_start_events; i++)
	      {
		tw_event_send(
			      tw_event_new(lp->gid, 
					   tw_rand_exponential(lp->rng, mean) + lookahead, 
					   lp));
	      }
	  }
}

void
phold_pre_run(phold_state * s, tw_lp * lp)
{
    tw_lpid	 dest;

	if(tw_rand_unif(lp->rng) <= percent_remote)
	{
		dest = tw_rand_integer(lp->rng, 0, ttl_lps - 1);
	} else
	{
		dest = lp->gid;
	}

	if(dest < 0 || dest >= (g_tw_nlp * tw_nnodes()))
		tw_error(TW_LOC, "bad dest");

	tw_event_send(tw_event_new(dest, tw_rand_exponential(lp->rng, mean) + lookahead, lp));
}

void
phold_event_handler(phold_state * s, tw_bf * bf, phold_message * m, tw_lp * lp)
{
    long delta_size;
    unsigned count;
	tw_lpid	 dest;
    long start_count = lp->rng->count;

    // This should be the FIRST thing to do in your event handler
    if (g_tw_synchronization_protocol == OPTIMISTIC ||
        g_tw_synchronization_protocol == OPTIMISTIC_DEBUG) {
        // Only do this in OPTIMISTIC mode
        tw_snapshot(lp, lp->type.state_sz);
    }

    count = tw_rand_ulong(lp->rng, 1, 100);
    
    while (count--) {
        double new_val;
        unsigned index = tw_rand_ulong(lp->rng, 0, DUMMY_SIZE - 1);
        if ((new_val = tw_rand_exponential(lp->rng, 1.0) < 0.5)) {
            s->dummy_state[index] = new_val;
        }
    }
    
	if(tw_rand_unif(lp->rng) <= percent_remote)
	{
		dest = tw_rand_integer(lp->rng, 0, ttl_lps - 1);
		// Makes PHOLD non-deterministic across processors! Don't uncomment
		/* dest += offset_lpid; */
		/* if(dest >= ttl_lps) */
		/* 	dest -= ttl_lps; */
	} else
	{
		dest = lp->gid;
	}

	if(dest < 0 || dest >= (g_tw_nlp * tw_nnodes()))
		tw_error(TW_LOC, "bad dest");

	tw_event_send(tw_event_new(dest, tw_rand_exponential(lp->rng, mean) + lookahead, lp));

    // This should be the LAST thing you do in your event handler
    // (Take care to cover all possible exits!)
    if (g_tw_synchronization_protocol == OPTIMISTIC ||
        g_tw_synchronization_protocol == OPTIMISTIC_DEBUG) {
        // Only do this in OPTIMISTIC mode
        delta_size = tw_snapshot_delta(lp, lp->type.state_sz);
        m->rng_count = lp->rng->count - start_count;
    }
}

void
phold_event_handler_rc(phold_state * s, tw_bf * bf, phold_message * m, tw_lp * lp)
{
    // We don't need to check g_tw_synchronization_protocol here since if
    // this gets called, we must be in an OPTIMISTIC mode anyway
    long count = m->rng_count;
    // This should be the FIRST thing to do in your reverse event handler
    tw_snapshot_restore(lp, lp->type.state_sz, lp->pe->cur_event->delta_buddy, lp->pe->cur_event->delta_size);
    while (count--) {
        tw_rand_reverse_unif(lp->rng);
    }
}

void
phold_finish(phold_state * s, tw_lp * lp)
{
}

tw_lptype       mylps[] = {
	{(init_f) phold_init,
     /* (pre_run_f) phold_pre_run, */
     (pre_run_f) NULL,
	 (event_f) phold_event_handler,
	 (revent_f) phold_event_handler_rc,
	 (final_f) phold_finish,
	 (map_f) phold_map,
	sizeof(phold_state)},
	{0},
};

const tw_optdef app_opt[] =
{
	TWOPT_GROUP("PHOLD Model"),
	TWOPT_STIME("remote", percent_remote, "desired remote event rate"),
	TWOPT_UINT("nlp", nlp_per_pe, "number of LPs per processor"),
	TWOPT_STIME("mean", mean, "exponential distribution mean for timestamps"),
	TWOPT_STIME("mult", mult, "multiplier for event memory allocation"),
	TWOPT_STIME("lookahead", lookahead, "lookahead for events"),
	TWOPT_UINT("start-events", g_phold_start_events, "number of initial messages per LP"),
	TWOPT_UINT("stagger", stagger, "Set to 1 to stagger event uniformly across 0 to end time."),
	TWOPT_UINT("memory", optimistic_memory, "additional memory buffers"),
	TWOPT_CHAR("run", run_id, "user supplied run name"),
	TWOPT_END()
};

int
main(int argc, char **argv, char **env)
{
	int		 i;

        // get rid of error if compiled w/ MEMORY queues
        g_tw_memory_nqueues=1;
	// set a min lookahead of 1.0
	lookahead = 1.0;
	tw_opt_add(app_opt);
	tw_init(&argc, &argv);

	if( lookahead > 1.0 )
	  tw_error(TW_LOC, "Lookahead > 1.0 .. needs to be less\n");

	//reset mean based on lookahead
        mean = mean - lookahead;

        g_tw_memory_nqueues = 16; // give at least 16 memory queue event

	offset_lpid = g_tw_mynode * nlp_per_pe;
	ttl_lps = tw_nnodes() * g_tw_npe * nlp_per_pe;
	g_tw_events_per_pe = (mult * nlp_per_pe * g_phold_start_events) + 
				optimistic_memory;
	//g_tw_rng_default = TW_FALSE;
	g_tw_lookahead = lookahead;

	tw_define_lps(nlp_per_pe, sizeof(phold_message), 0);

	for(i = 0; i < g_tw_nlp; i++)
		tw_lp_settype(i, &mylps[0]);

        if( g_tw_mynode == 0 )
	  {
	    printf("========================================\n");
	    printf("PHOLD Model Configuration..............\n");
	    printf("   Lookahead..............%lf\n", lookahead);
	    printf("   Start-events...........%u\n", g_phold_start_events);
	    printf("   stagger................%u\n", stagger);
	    printf("   Mean...................%lf\n", mean);
	    printf("   Mult...................%lf\n", mult);
	    printf("   Memory.................%u\n", optimistic_memory);
	    printf("   Remote.................%lf\n", percent_remote);
	    printf("========================================\n\n");
	  }

	tw_run();
	tw_end();

	return 0;
}
