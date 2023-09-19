#ifndef NPROOFS

#include "allocate.h"
#include "error.h"
#include "file.h"
#include "inline.h"
#include "redis.h"

#undef NDEBUG

#ifndef NDEBUG
#include <string.h>
#endif

#define size_buffer (1u << 20)

struct write_buffer {
  unsigned char chars[size_buffer];
  size_t pos;
};

typedef struct write_buffer write_buffer;

struct proof {
  write_buffer buffer;
  kissat *solver;
  redis *redis;
  bool binary;
  ints line;
  uint64_t added;
  uint64_t deleted;
  uint64_t lines;
  uint64_t literals;
#ifndef NDEBUG
  bool empty;
  char *units;
  size_t size_units;
#endif
#if !defined(NDEBUG) || defined(LOGGING)
  unsigneds imported;
#endif
};

#undef LOGPREFIX
#define LOGPREFIX "PROOF"

#define LOGIMPORTED3(...) \
  LOGLITS3 (SIZE_STACK (proof->imported), BEGIN_STACK (proof->imported), \
            __VA_ARGS__)

#define LOGLINE3(...) \
  LOGINTS3 (SIZE_STACK (proof->line), BEGIN_STACK (proof->line), \
            __VA_ARGS__)

void kissat_init_proof (kissat *solver, bool binary) {
  assert (!solver->proof);
  assert (solver->redis);
  proof *proof = kissat_calloc (solver, 1, sizeof (struct proof));
  proof->binary = binary;
  proof->solver = solver;
  solver->proof = proof;
  proof->redis = solver->redis;
  LOG ("starting to trace %s proof", binary ? "binary" : "non-binary");
}

static void save_in_database_with_split(proof *proof, size_t bytes, unsigned char sep) {
  redisContext* context = get_context(&proof->redis);

  write_buffer *write_buffer = &proof->buffer;
  int len = 0;
  unsigned char* substr_start = write_buffer->chars;

  for (int i = 0; i < bytes; i++) {
    if (write_buffer->chars[i] != sep) {
      len++;
    } else {
      substr_start[len] =  '\0'; // dirty hack
      redis_save(context, &proof->redis, substr_start);
      proof->redis->last_from_kissat_id += 1;
      substr_start += len + 1;
      len = 0;
    }
  }
  redis_free(context);

  strncpy(write_buffer->chars, substr_start, len);
  write_buffer->pos = len;
}

static void save_in_database(proof *proof, size_t bytes) {
  if (proof->binary) {
    save_in_database_with_split(proof, bytes, 0);
  } else {
    save_in_database_with_split(proof, bytes, '\n');
  }
}

static void flush_buffer (proof *proof) {
  size_t bytes = proof->buffer.pos;
  if (!bytes)
    return;
  save_in_database (proof, bytes);
}

void kissat_release_proof (kissat *solver) {
  proof *proof = solver->proof;
  assert (proof);
  LOG ("stopping to trace proof");
  flush_buffer (proof);
  RELEASE_STACK (proof->line);
#ifndef NDEBUG
  kissat_free (solver, proof->units, proof->size_units);
#endif
#if !defined(NDEBUG) || defined(LOGGING)
  RELEASE_STACK (proof->imported);
#endif
  kissat_free (solver, proof, sizeof (struct proof));
  solver->proof = 0;
}

#ifndef QUIET

#include <inttypes.h>

#define PERCENT_LINES(NAME) kissat_percent (proof->NAME, proof->lines)

void kissat_print_proof_statistics (kissat *solver, bool verbose) {
  proof *proof = solver->proof;
  PRINT_STAT ("proof_added", proof->added, PERCENT_LINES (added), "%",
              "per line");
  PRINT_STAT ("proof_deleted", proof->deleted, PERCENT_LINES (deleted), "%",
              "per line");
  if (verbose)
    PRINT_STAT ("proof_lines", proof->lines, 100, "%", "");
  if (verbose)
    PRINT_STAT ("proof_literals", proof->literals,
                kissat_average (proof->literals, proof->lines), "",
                "per line");
}

#endif

// clang-format off

static inline void write_char (proof *, unsigned char)
  ATTRIBUTE_ALWAYS_INLINE;

static inline void import_external_proof_literal (kissat *, proof *, int)
  ATTRIBUTE_ALWAYS_INLINE;

static inline void
import_internal_proof_literal (kissat *, proof *, unsigned)
  ATTRIBUTE_ALWAYS_INLINE;

// clang-format on

static inline void write_char (proof *proof, unsigned char ch) {
  write_buffer *buffer = &proof->buffer;
  if (buffer->pos == size_buffer)
    flush_buffer (proof);
  buffer->chars[buffer->pos++] = ch;
}

static inline void import_internal_proof_literal (kissat *solver,
                                                  proof *proof,
                                                  unsigned ilit) {
  int elit = kissat_export_literal (solver, ilit);
  assert (elit);
  PUSH_STACK (proof->line, elit);
  proof->literals++;
#if !defined(NDEBUG) || defined(LOGGING)
  PUSH_STACK (proof->imported, ilit);
#endif
}

static inline void import_external_proof_literal (kissat *solver,
                                                  proof *proof, int elit) {
  assert (elit);
  PUSH_STACK (proof->line, elit);
  proof->literals++;
#ifndef NDEBUG
  assert (EMPTY_STACK (proof->imported));
#endif
}

static void import_internal_proof_binary (kissat *solver, proof *proof,
                                          unsigned a, unsigned b) {
  assert (EMPTY_STACK (proof->line));
  import_internal_proof_literal (solver, proof, a);
  import_internal_proof_literal (solver, proof, b);
}

static void import_internal_proof_literals (kissat *solver, proof *proof,
                                            size_t size,
                                            const unsigned *ilits) {
  assert (EMPTY_STACK (proof->line));
  assert (size <= UINT_MAX);
  for (size_t i = 0; i < size; i++)
    import_internal_proof_literal (solver, proof, ilits[i]);
}

static void import_external_proof_literals (kissat *solver, proof *proof,
                                            size_t size, const int *elits) {
  assert (EMPTY_STACK (proof->line));
  assert (size <= UINT_MAX);
  for (size_t i = 0; i < size; i++)
    import_external_proof_literal (solver, proof, elits[i]);
}

static void import_proof_clause (kissat *solver, proof *proof,
                                 const clause *c) {
  import_internal_proof_literals (solver, proof, c->size, c->lits);
}

static void print_binary_proof_line (proof *proof) {
  assert (proof->binary);
  for (all_stack (int, elit, proof->line)) {
    unsigned x = 2u * ABS (elit) + (elit < 0);
    unsigned char ch;
    while (x & ~0x7f) {
      ch = (x & 0x7f) | 0x80;
      write_char (proof, ch);
      x >>= 7;
    }
    write_char (proof, x);
  }
  write_char (proof, 0);
}

static void print_non_binary_proof_line (proof *proof) {
  assert (!proof->binary);
  char buffer[16];
  char *end_of_buffer = buffer + sizeof buffer;
  *--end_of_buffer = 0;
  for (all_stack (int, elit, proof->line)) {
    char *p = end_of_buffer;
    assert (!*p);
    assert (elit);
    assert (elit != INT_MIN);
    unsigned eidx;
    if (elit < 0) {
      write_char (proof, '-');
      eidx = -elit;
    } else
      eidx = elit;
    for (unsigned tmp = eidx; tmp; tmp /= 10)
      *--p = '0' + (tmp % 10);
    while (p != end_of_buffer)
      write_char (proof, *p++);
    write_char (proof, ' ');
  }
  write_char (proof, '0');
  write_char (proof, '\n');
}

static void print_proof_line (proof *proof) {
  proof->lines++;
  if (proof->binary)
    print_binary_proof_line (proof);
  else
    print_non_binary_proof_line (proof);
  CLEAR_STACK (proof->line);
#if !defined(NDEBUG) || defined(LOGGING)
  CLEAR_STACK (proof->imported);
#endif
#ifndef NOPTIONS
  kissat *solver = proof->solver;
#endif
  if (GET_OPTION (flushproof)) {
    flush_buffer (proof);
  }
}

#ifndef NDEBUG

static unsigned external_to_proof_literal (int elit) {
  assert (elit);
  assert (elit != INT_MIN);
  return 2u * (abs (elit) - 1) + (elit < 0);
}

static void resize_proof_units (proof *proof, unsigned plit) {
  kissat *solver = proof->solver;
  const size_t old_size = proof->size_units;
  size_t new_size = old_size ? old_size : 2;
  while (new_size <= plit)
    new_size *= 2;
  char *new_units = kissat_calloc (solver, new_size, 1);
  if (old_size)
    memcpy (new_units, proof->units, old_size);
  kissat_dealloc (solver, proof->units, old_size, 1);
  proof->units = new_units;
  proof->size_units = new_size;
}

static void check_repeated_proof_lines (proof *proof) {
  size_t size = SIZE_STACK (proof->line);
  if (!size) {
    assert (!proof->empty);
    proof->empty = true;
  } else if (size == 1) {
    const int eunit = PEEK_STACK (proof->line, 0);
    const unsigned punit = external_to_proof_literal (eunit);
    assert (punit != INVALID_LIT);
    if (!proof->size_units || proof->size_units <= punit)
      resize_proof_units (proof, punit);
    proof->units[punit] = 1;
  }
}

#endif

static void print_added_proof_line (proof *proof) {
  proof->added++;
#ifdef LOGGING
  struct kissat *solver = proof->solver;
  assert (SIZE_STACK (proof->imported) == SIZE_STACK (proof->line));
  LOGIMPORTED3 ("added proof line");
  LOGLINE3 ("added proof line");
#endif
#ifndef NDEBUG
  check_repeated_proof_lines (proof);
#endif
  if (proof->binary)
    write_char (proof, 'a');
  print_proof_line (proof);
}

static void print_delete_proof_line (proof *proof) {
  proof->deleted++;
#ifdef LOGGING
  struct kissat *solver = proof->solver;
  if (SIZE_STACK (proof->imported) == SIZE_STACK (proof->line))
    LOGIMPORTED3 ("added internal proof line");
  LOGLINE3 ("deleted external proof line");
#endif
  write_char (proof, 'd');
  if (!proof->binary)
    write_char (proof, ' ');
  print_proof_line (proof);
}

void kissat_add_binary_to_proof (kissat *solver, unsigned a, unsigned b) {
  proof *proof = solver->proof;
  assert (proof);
  import_internal_proof_binary (solver, proof, a, b);
  print_added_proof_line (proof);
}

void kissat_add_clause_to_proof (kissat *solver, const clause *c) {
  proof *proof = solver->proof;
  assert (proof);
  import_proof_clause (solver, proof, c);
  print_added_proof_line (proof);
}

void kissat_add_empty_to_proof (kissat *solver) {
  proof *proof = solver->proof;
  assert (proof);
  assert (EMPTY_STACK (proof->line));
  print_added_proof_line (proof);
}

void kissat_add_lits_to_proof (kissat *solver, size_t size,
                               const unsigned *ilits) {
  proof *proof = solver->proof;
  assert (proof);
  import_internal_proof_literals (solver, proof, size, ilits);
  print_added_proof_line (proof);
}

void kissat_add_unit_to_proof (kissat *solver, unsigned ilit) {
  proof *proof = solver->proof;
  assert (proof);
  assert (EMPTY_STACK (proof->line));
  import_internal_proof_literal (solver, proof, ilit);
  print_added_proof_line (proof);
}

void kissat_shrink_clause_in_proof (kissat *solver, const clause *c,
                                    unsigned remove, unsigned keep) {
  proof *proof = solver->proof;
  const value *const values = solver->values;
  assert (EMPTY_STACK (proof->line));
  const unsigned *ilits = c->lits;
  const unsigned size = c->size;
  for (unsigned i = 0; i != size; i++) {
    const unsigned ilit = ilits[i];
    if (ilit == remove)
      continue;
    if (ilit != keep && values[ilit] < 0 && !LEVEL (ilit))
      continue;
    import_internal_proof_literal (solver, proof, ilit);
  }
  print_added_proof_line (proof);
  import_proof_clause (solver, proof, c);
  print_delete_proof_line (proof);
}

void kissat_delete_binary_from_proof (kissat *solver, unsigned a,
                                      unsigned b) {
  proof *proof = solver->proof;
  assert (proof);
  import_internal_proof_binary (solver, proof, a, b);
  print_delete_proof_line (proof);
}

void kissat_delete_clause_from_proof (kissat *solver, const clause *c) {
  proof *proof = solver->proof;
  assert (proof);
  import_proof_clause (solver, proof, c);
  print_delete_proof_line (proof);
}

void kissat_delete_external_from_proof (kissat *solver, size_t size,
                                        const int *elits) {
  proof *proof = solver->proof;
  assert (proof);
  LOGINTS3 (size, elits, "explicitly deleted");
  import_external_proof_literals (solver, proof, size, elits);
  print_delete_proof_line (proof);
}

void kissat_delete_internal_from_proof (kissat *solver, size_t size,
                                        const unsigned *ilits) {
  proof *proof = solver->proof;
  assert (proof);
  import_internal_proof_literals (solver, proof, size, ilits);
  print_delete_proof_line (proof);
}

#else
int kissat_proof_dummy_to_avoid_warning;
#endif
