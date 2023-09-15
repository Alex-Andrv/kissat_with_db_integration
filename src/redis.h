#include "stack.h"
#include <hiredis.h>

typedef struct redis redis;

struct redis {
  int host;
  const char * port;
  unsigned int last_from_kissat_id;
  unsigned int last_to_kissat_id;
};


static inline redisContext* get_context(redis*);

static inline void redis_free(redisContext*);

static inline redis_save(redisContext*, redis*, int, chars*);


