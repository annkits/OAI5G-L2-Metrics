/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef L2_COUNTERS_INTERNAL_H
#define L2_COUNTERS_INTERNAL_H

#include "l2_counters.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

struct l2c_counter {
  _Atomic int64_t value;
  l2c_type_t type;
  char group[L2C_NAME_MAX];
  char name[L2C_NAME_MAX];
  l2c_registry_t *reg;
  struct l2c_counter *next;
};

struct l2c_registry {
  char node[L2C_NAME_MAX];
  pthread_mutex_t lock; // protects the counter list
  l2c_counter_t *head; // counters of one group are contiguous

  // exporter
  pthread_mutex_t exp_lock;
  pthread_cond_t exp_cond; // CLOCK_MONOTONIC
  pthread_t exp_thread;
  bool exp_running;
  bool exp_stop;
  char *exp_path;
  int exp_period_ms;
};

#endif
