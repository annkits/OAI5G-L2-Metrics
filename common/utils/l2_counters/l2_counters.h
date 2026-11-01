/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef L2_COUNTERS_H
#define L2_COUNTERS_H

/*
 * Named L2 counters, identified by (group, name), with periodic JSON export.
 * set/add/get are lock-free and accept NULL, so call sites need no check
 * when the export is disabled. create/delete take the registry lock.
 */

#include <stdint.h>
#include <stdio.h>

#define L2C_NAME_MAX 64

typedef struct l2c_registry l2c_registry_t;
typedef struct l2c_counter l2c_counter_t;

typedef enum {
  L2C_COUNTER, // monotonic, e.g. transmitted bytes
  L2C_GAUGE, // instantaneous, e.g. buffer occupancy
} l2c_type_t;

// node is written as "node" in the JSON output
l2c_registry_t *l2c_registry_create(const char *node);
// also stops the exporter
void l2c_registry_destroy(l2c_registry_t *r);
void l2c_registry_reset(l2c_registry_t *r);

// names may contain [A-Za-z0-9_.-] only; returns NULL on invalid or duplicate name
l2c_counter_t *l2c_counter_create(l2c_registry_t *r, const char *group, const char *name, l2c_type_t type);
// no other thread may use the counter after this call
void l2c_counter_delete(l2c_counter_t *c);
void l2c_counter_reset(l2c_counter_t *c);
l2c_counter_t *l2c_counter_find(l2c_registry_t *r, const char *group, const char *name);

void l2c_set(l2c_counter_t *c, int64_t v);
void l2c_add(l2c_counter_t *c, int64_t delta);
int64_t l2c_get(const l2c_counter_t *c);
l2c_type_t l2c_get_type(const l2c_counter_t *c);

// return 0 on success, -1 on error
int l2c_dump_json(l2c_registry_t *r, FILE *f);
int l2c_dump_json_file(l2c_registry_t *r, const char *path);

// dump the registry to path every period_ms; returns -1 if already running
int l2c_exporter_start(l2c_registry_t *r, const char *path, int period_ms);
// writes a last dump before returning
void l2c_exporter_stop(l2c_registry_t *r);

// process-wide registry configured from section "l2_counters", NULL if disabled
l2c_registry_t *l2c_init_from_config(const char *node);

#endif
