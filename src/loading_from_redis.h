#ifndef KISSAT_WITH_DB_INTEGRATION_LOADING_FROM_REDIS_H
#define KISSAT_WITH_DB_INTEGRATION_LOADING_FROM_REDIS_H

#include <stdbool.h>


struct kissat;

bool kissat_loading_from_redis (struct kissat *);

void kissat_load_from_redis (struct kissat *);


#endif // KISSAT_WITH_DB_INTEGRATION_LOADING_FROM_REDIS_H
