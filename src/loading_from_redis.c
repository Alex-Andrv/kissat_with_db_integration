#include "loading_from_redis.h"

#include "internal.h"

#define size_buffer (1u << 20)

struct read_buffer {
  unsigned char chars[size_buffer];
  size_t pos, end;
};

bool kissat_loading_from_redis (kissat *solver) {
    if (solver->redis->cnt_nope > 10000) {
      solver->redis->cnt_nope = 0;
      return true;
    }
    solver->redis->cnt_nope += 1;
    return false;
}

void kissat_load_from_redis (kissat *solver) {
    assert(EMPTY_STACK(solver->clause));
    redis *redis = solver->redis;
    redisContext* context = get_context(redis);
    clause* clause;
    // TODO что будет если индекс потеряется
    while (load_clause(solver, context, redis)) {
      const unsigned not_uip = PEEK_STACK (solver->clause, 0);
      const unsigned size = SIZE_STACK (solver->clause);
      //      some hack
      const size_t glue = size - 1;
      assert (glue <= UINT_MAX);
      assert (size > 0);
      if (size == 1)
        kissat_learned_unit (solver, not_uip);
      //      else if (size == 2)
      //        kissat_new_redundant_clause (solver, 1);
      else
        kissat_new_redundant_clause (solver, glue);
      CLEAR_STACK(solver->clause);
      redis->last_to_kissat_id += 1;
    }
    redis_free(context);
}
