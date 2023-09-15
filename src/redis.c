#include "redis.h"


static inline redisContext* get_context(redis* redis) {
  redisContext *c = redisConnect(redis->host, redis->port);
  return c;
}

static inline void redis_free(redisContext* c) {
  redisFree(c);
}

static inline redis_save(redisContext* context, redis* redis, int subst_len, chars* substr_start) {
  redisReply *reply = redisCommand(context,"SET from_kissat:%d %.*s", redis->last_from_kissat_id, subst_len, substr_start);
}
