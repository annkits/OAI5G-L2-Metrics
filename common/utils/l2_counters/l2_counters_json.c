/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#define _GNU_SOURCE // pthread_setname_np
#include "l2_counters.h"
#include "l2_counters_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Names are restricted to [A-Za-z0-9_.-] at creation, so no JSON escaping is needed. */
int l2c_dump_json(l2c_registry_t *r, FILE *f)
{
  if (!r || !f)
    return -1;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

  fprintf(f, "{\n  \"node\": \"%s\",\n  \"timestamp_ms\": %" PRId64 ",\n", r->node, now_ms);
  fprintf(f, "  \"period_ms\": %d,\n  \"counters\": {", r->exp_period_ms);

  pthread_mutex_lock(&r->lock);
  const char *cur_group = NULL;
  for (l2c_counter_t *c = r->head; c; c = c->next) {
    if (!cur_group || strcmp(cur_group, c->group) != 0) {
      fprintf(f, "%s\n    \"%s\": {", cur_group ? "\n    }," : "", c->group);
      cur_group = c->group;
    } else {
      fputc(',', f);
    }
    fprintf(f, "\n      \"%s\": %" PRId64, c->name, atomic_load_explicit(&c->value, memory_order_relaxed));
  }
  pthread_mutex_unlock(&r->lock);

  fprintf(f, "%s\n  }\n}\n", cur_group ? "\n    }" : "");
  return ferror(f) ? -1 : 0;
}

int l2c_dump_json_file(l2c_registry_t *r, const char *path)
{
  if (!r || !path)
    return -1;
  char tmp[strlen(path) + 5];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (!f)
    return -1;
  int ret = l2c_dump_json(r, f);
  if (fclose(f) != 0)
    ret = -1;
  if (ret == 0 && rename(tmp, path) != 0)
    ret = -1;
  if (ret != 0)
    remove(tmp);
  return ret;
}

static void timespec_add_ms(struct timespec *t, int ms)
{
  t->tv_sec += ms / 1000;
  t->tv_nsec += (long)(ms % 1000) * 1000000;
  if (t->tv_nsec >= 1000000000) {
    t->tv_sec++;
    t->tv_nsec -= 1000000000;
  }
}

static void *exporter_thread(void *arg)
{
  l2c_registry_t *r = arg;
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);

  pthread_mutex_lock(&r->exp_lock);
  while (!r->exp_stop) {
    timespec_add_ms(&next, r->exp_period_ms);
    int rc = 0;
    while (!r->exp_stop && rc != ETIMEDOUT)
      rc = pthread_cond_timedwait(&r->exp_cond, &r->exp_lock, &next);
    // write outside exp_lock so that stop() is not delayed by file I/O
    pthread_mutex_unlock(&r->exp_lock);
    if (l2c_dump_json_file(r, r->exp_path) != 0)
      fprintf(stderr, "l2_counters: cannot write %s: %s\n", r->exp_path, strerror(errno));
    pthread_mutex_lock(&r->exp_lock);
  }
  pthread_mutex_unlock(&r->exp_lock);
  return NULL;
}

int l2c_exporter_start(l2c_registry_t *r, const char *path, int period_ms)
{
  if (!r || !path || period_ms <= 0)
    return -1;
  pthread_mutex_lock(&r->exp_lock);
  if (r->exp_running) {
    pthread_mutex_unlock(&r->exp_lock);
    return -1;
  }
  r->exp_path = strdup(path);
  r->exp_period_ms = period_ms;
  r->exp_stop = false;
  if (!r->exp_path || pthread_create(&r->exp_thread, NULL, exporter_thread, r) != 0) {
    free(r->exp_path);
    r->exp_path = NULL;
    pthread_mutex_unlock(&r->exp_lock);
    return -1;
  }
  pthread_setname_np(r->exp_thread, "L2_COUNTERS");
  r->exp_running = true;
  pthread_mutex_unlock(&r->exp_lock);
  return 0;
}

void l2c_exporter_stop(l2c_registry_t *r)
{
  if (!r)
    return;
  pthread_mutex_lock(&r->exp_lock);
  if (!r->exp_running) {
    pthread_mutex_unlock(&r->exp_lock);
    return;
  }
  r->exp_stop = true;
  pthread_cond_signal(&r->exp_cond);
  pthread_mutex_unlock(&r->exp_lock);

  pthread_join(r->exp_thread, NULL);

  pthread_mutex_lock(&r->exp_lock);
  r->exp_running = false;
  free(r->exp_path);
  r->exp_path = NULL;
  pthread_mutex_unlock(&r->exp_lock);
}
