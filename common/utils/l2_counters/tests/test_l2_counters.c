/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "l2_counters.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// not assert(): must stay active with NDEBUG
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      abort();                                                                 \
    }                                                                          \
  } while (0)

static char *dump_to_string(l2c_registry_t *r)
{
  char *buf = NULL;
  size_t len = 0;
  FILE *f = open_memstream(&buf, &len);
  CHECK(f);
  CHECK(l2c_dump_json(r, f) == 0);
  fclose(f);
  return buf;
}

static char *read_file(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f)
    return NULL;
  char *buf = calloc(1, 1 << 16);
  CHECK(fread(buf, 1, (1 << 16) - 1, f) > 0);
  fclose(f);
  return buf;
}

static void test_lifecycle(void)
{
  l2c_registry_t *r = l2c_registry_create("gNB");
  CHECK(r);

  l2c_counter_t *tx = l2c_counter_create(r, "mac", "dl_tx_bytes", L2C_COUNTER);
  l2c_counter_t *ue = l2c_counter_create(r, "mac", "num_ue", L2C_GAUGE);
  CHECK(tx && ue);
  CHECK(l2c_get_type(ue) == L2C_GAUGE);

  // duplicate and invalid names are rejected
  CHECK(!l2c_counter_create(r, "mac", "dl_tx_bytes", L2C_COUNTER));
  CHECK(!l2c_counter_create(r, "mac", "bad name", L2C_COUNTER));
  CHECK(!l2c_counter_create(r, "", "x", L2C_COUNTER));
  CHECK(!l2c_counter_create(r, "mac", "quote\"", L2C_COUNTER));

  CHECK(l2c_get(tx) == 0);
  l2c_add(tx, 100);
  l2c_add(tx, 23);
  CHECK(l2c_get(tx) == 123);
  l2c_set(ue, 4);
  CHECK(l2c_get(ue) == 4);

  CHECK(l2c_counter_find(r, "mac", "dl_tx_bytes") == tx);
  CHECK(!l2c_counter_find(r, "rlc", "dl_tx_bytes"));

  l2c_counter_reset(tx);
  CHECK(l2c_get(tx) == 0);
  CHECK(l2c_get(ue) == 4);
  l2c_registry_reset(r);
  CHECK(l2c_get(ue) == 0);

  l2c_counter_delete(tx);
  CHECK(!l2c_counter_find(r, "mac", "dl_tx_bytes"));
  // can be recreated after deletion
  tx = l2c_counter_create(r, "mac", "dl_tx_bytes", L2C_COUNTER);
  CHECK(tx);

  l2c_registry_destroy(r);
}

static void test_null_safe(void)
{
  l2c_set(NULL, 1);
  l2c_add(NULL, 1);
  l2c_counter_reset(NULL);
  l2c_counter_delete(NULL);
  CHECK(l2c_get(NULL) == 0);
  CHECK(!l2c_counter_create(NULL, "mac", "x", L2C_COUNTER));
  CHECK(l2c_dump_json(NULL, stdout) == -1);
  l2c_registry_destroy(NULL);
}

static void test_json(void)
{
  l2c_registry_t *r = l2c_registry_create("UE");
  char *s = dump_to_string(r);
  CHECK(strstr(s, "\"node\": \"UE\""));
  CHECK(strstr(s, "\"counters\": {\n  }"));
  free(s);

  // counters of the same group stay together even if created interleaved
  l2c_set(l2c_counter_create(r, "mac", "a", L2C_COUNTER), 1);
  l2c_set(l2c_counter_create(r, "rlc", "b", L2C_COUNTER), 2);
  l2c_set(l2c_counter_create(r, "mac", "c", L2C_GAUGE), -3);
  s = dump_to_string(r);
  const char *expected = "  \"counters\": {\n"
                         "    \"mac\": {\n"
                         "      \"a\": 1,\n"
                         "      \"c\": -3\n"
                         "    },\n"
                         "    \"rlc\": {\n"
                         "      \"b\": 2\n"
                         "    }\n"
                         "  }\n"
                         "}\n";
  CHECK(strstr(s, expected));
  free(s);
  l2c_registry_destroy(r);
}

#define NUM_THREADS 4
#define NUM_ADDS 200000

static void *adder(void *arg)
{
  for (int i = 0; i < NUM_ADDS; i++)
    l2c_add(arg, 1);
  return NULL;
}

static void test_exporter_concurrent(void)
{
  char path[] = "/tmp/test_l2_counters_XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  close(fd);

  l2c_registry_t *r = l2c_registry_create("gNB");
  l2c_counter_t *c = l2c_counter_create(r, "mac", "dl_tx_bytes", L2C_COUNTER);
  CHECK(l2c_exporter_start(r, path, 5) == 0);
  CHECK(l2c_exporter_start(r, path, 5) == -1);
  CHECK(l2c_exporter_start(r, path, 0) == -1);

  pthread_t th[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++)
    pthread_create(&th[i], NULL, adder, c);
  // create/delete concurrently with the exporter walking the list
  for (int i = 0; i < 1000; i++)
    l2c_counter_delete(l2c_counter_create(r, "ue_1234", "tmp", L2C_GAUGE));
  for (int i = 0; i < NUM_THREADS; i++)
    pthread_join(th[i], NULL);
  CHECK(l2c_get(c) == (int64_t)NUM_THREADS * NUM_ADDS);

  usleep(20000);
  l2c_exporter_stop(r); // writes a last dump
  char *s = read_file(path);
  CHECK(s);
  CHECK(strstr(s, "\"period_ms\": 5"));
  char expected[64];
  snprintf(expected, sizeof(expected), "\"dl_tx_bytes\": %d", NUM_THREADS * NUM_ADDS);
  CHECK(strstr(s, expected));
  free(s);

  // restart after stop
  CHECK(l2c_exporter_start(r, path, 5) == 0);
  l2c_registry_destroy(r); // stops the exporter
  remove(path);
}

int main(void)
{
  test_lifecycle();
  test_null_safe();
  test_json();
  test_exporter_concurrent();
  printf("test_l2_counters: OK\n");
  return 0;
}
