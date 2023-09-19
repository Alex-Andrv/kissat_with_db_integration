#ifndef REDIS_H
#define REDIS_H

#include "stack.h"

#include <hiredis.h>

typedef struct redis redis;

struct redis {
  const char * host;
  int port;
  unsigned int last_from_kissat_id;
  unsigned int last_to_kissat_id;
  unsigned int cnt_nope;
};


redisContext* get_context(redis*);

void redis_free(redisContext*);

void redis_save(redisContext*, redis*, unsigned char*);

bool load_clause(kissat*, redisContext*, redis*);

#endif // REDIS_H

