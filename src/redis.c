#include "internal.h"
#include "import.h"
#include "redis.h"


redisContext* get_context(redis* redis) {
  redisContext *c = redisConnect(redis->host, redis->port);
  if (c == NULL || c->err) {
    if (c) {
      printf("Error: %s\n", c->errstr);
      // handle error
    } else {
      printf("Can't allocate redis context\n");
    }
  }
  return c;
}

void redis_free(redisContext* c) {
  redisFree(c);
}

bool load_clause(kissat *solver, redisContext* context, redis* redis) {
  assert (EMPTY_STACK(solver->clause));
  redisReply *reply = redisCommand(context,"LRANGE to_kissat:%d 0 -1", redis->last_to_kissat_id);

  if (reply == NULL || reply->type != REDIS_REPLY_ARRAY || reply->element == NULL) {
    return false;
  }

  for (int i = 0; i < reply->elements; i++) {
    int elit = atoi(reply->element[i]->str);
    unsigned ilit = kissat_import_literal (solver, elit);
    PUSH_STACK (solver->clause, ilit);
  }
  return true;
}

void redis_save(redisContext* context, redis* redis, unsigned char* substr_start) {
  redisReply *reply = redisCommand(context,"SET from_kissat:%d %s", redis->last_from_kissat_id, substr_start);
  if (reply == NULL) {
    if (context) {
      printf("Error: %s\n", context->errstr);
      // handle error
    } else {
      printf("Can't allocate redis context\n");
    }
  }
}
