/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "l2_counters.h"
#include "l2_counters_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool valid_name(const char *s)
{
  if (!s || !*s || strlen(s) >= L2C_NAME_MAX)
    return false;
  for (; *s; s++) {
    char ch = *s;
    if (!isalnum((unsigned char)ch) && ch != '_' && ch != '.' && ch != '-')
      return false;
  }
  return true;
}

l2c_registry_t *l2c_registry_create(const char *node)
{
  if (!valid_name(node))
    return NULL;
  l2c_registry_t *r = calloc(1, sizeof(*r));
  if (!r)
    return NULL;
  strcpy(r->node, node);
  pthread_mutex_init(&r->lock, NULL);
  pthread_mutex_init(&r->exp_lock, NULL);
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(&r->exp_cond, &attr);
  pthread_condattr_destroy(&attr);
  return r;
}

void l2c_registry_destroy(l2c_registry_t *r)
{
  if (!r)
    return;
  l2c_exporter_stop(r);
  l2c_counter_t *c = r->head;
  while (c) {
    l2c_counter_t *next = c->next;
    free(c);
    c = next;
  }
  pthread_cond_destroy(&r->exp_cond);
  pthread_mutex_destroy(&r->exp_lock);
  pthread_mutex_destroy(&r->lock);
  free(r);
}

void l2c_registry_reset(l2c_registry_t *r)
{
  if (!r)
    return;
  pthread_mutex_lock(&r->lock);
  for (l2c_counter_t *c = r->head; c; c = c->next)
    atomic_store_explicit(&c->value, 0, memory_order_relaxed);
  pthread_mutex_unlock(&r->lock);
}

static l2c_counter_t *find_locked(l2c_registry_t *r, const char *group, const char *name)
{
  for (l2c_counter_t *c = r->head; c; c = c->next)
    if (strcmp(c->group, group) == 0 && strcmp(c->name, name) == 0)
      return c;
  return NULL;
}

l2c_counter_t *l2c_counter_create(l2c_registry_t *r, const char *group, const char *name, l2c_type_t type)
{
  if (!r || !valid_name(group) || !valid_name(name))
    return NULL;
  l2c_counter_t *c = calloc(1, sizeof(*c));
  if (!c)
    return NULL;
  strcpy(c->group, group);
  strcpy(c->name, name);
  c->type = type;
  c->reg = r;

  pthread_mutex_lock(&r->lock);
  if (find_locked(r, group, name)) {
    pthread_mutex_unlock(&r->lock);
    free(c);
    return NULL;
  }
  // keep counters of one group contiguous: insert after the last one of the group
  l2c_counter_t *last_in_group = NULL;
  for (l2c_counter_t *it = r->head; it; it = it->next)
    if (strcmp(it->group, group) == 0)
      last_in_group = it;
  if (last_in_group) {
    c->next = last_in_group->next;
    last_in_group->next = c;
  } else if (r->head) {
    l2c_counter_t *tail = r->head;
    while (tail->next)
      tail = tail->next;
    tail->next = c;
  } else {
    r->head = c;
  }
  pthread_mutex_unlock(&r->lock);
  return c;
}

void l2c_counter_delete(l2c_counter_t *c)
{
  if (!c)
    return;
  l2c_registry_t *r = c->reg;
  pthread_mutex_lock(&r->lock);
  for (l2c_counter_t **pp = &r->head; *pp; pp = &(*pp)->next) {
    if (*pp == c) {
      *pp = c->next;
      break;
    }
  }
  pthread_mutex_unlock(&r->lock);
  free(c);
}

void l2c_counter_reset(l2c_counter_t *c)
{
  l2c_set(c, 0);
}

l2c_counter_t *l2c_counter_find(l2c_registry_t *r, const char *group, const char *name)
{
  if (!r || !group || !name)
    return NULL;
  pthread_mutex_lock(&r->lock);
  l2c_counter_t *c = find_locked(r, group, name);
  pthread_mutex_unlock(&r->lock);
  return c;
}

void l2c_set(l2c_counter_t *c, int64_t v)
{
  if (c)
    atomic_store_explicit(&c->value, v, memory_order_relaxed);
}

void l2c_add(l2c_counter_t *c, int64_t delta)
{
  if (c)
    atomic_fetch_add_explicit(&c->value, delta, memory_order_relaxed);
}

int64_t l2c_get(const l2c_counter_t *c)
{
  return c ? atomic_load_explicit(&c->value, memory_order_relaxed) : 0;
}

l2c_type_t l2c_get_type(const l2c_counter_t *c)
{
  return c ? c->type : L2C_COUNTER;
}
