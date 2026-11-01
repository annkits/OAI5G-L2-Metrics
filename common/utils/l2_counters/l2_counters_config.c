/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "l2_counters.h"

#include <pthread.h>
#include <stdlib.h>

#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"

#define L2C_CONFIG_SECTION "l2_counters"

static l2c_registry_t *registry;

static void stop_at_exit(void)
{
  // final dump; the registry itself is left to the OS
  l2c_exporter_stop(registry);
}

static void init_once(const char *node)
{
  int enabled = 0;
  int period_ms = 0;
  char *path = NULL;
  // clang-format off
  paramdef_t params[] = {
    {"enabled",   "enable L2 counters JSON export",           0, .iptr = &enabled,   .defintval = 0,       TYPE_INT,    0},
    {"period_ms", "L2 counters export period in ms",          0, .iptr = &period_ms, .defintval = 1000,    TYPE_INT,    0},
    {"path",      "L2 counters JSON file (default l2_counters_<node>.json)", 0, .strptr = &path, .defstrval = NULL, TYPE_STRING, 0},
  };
  // clang-format on
  int ret = config_get(config_get_if(), params, sizeofArray(params), L2C_CONFIG_SECTION);
  AssertFatal(ret >= 0, "cannot read config section %s\n", L2C_CONFIG_SECTION);
  if (!enabled)
    return;
  AssertFatal(period_ms > 0, "%s.period_ms must be > 0, got %d\n", L2C_CONFIG_SECTION, period_ms);

  char default_path[L2C_NAME_MAX + 32];
  if (!path || !*path) {
    snprintf(default_path, sizeof(default_path), "l2_counters_%s.json", node);
    path = default_path;
  }

  registry = l2c_registry_create(node);
  AssertFatal(registry, "cannot create L2 counters registry \"%s\"\n", node);
  AssertFatal(l2c_exporter_start(registry, path, period_ms) == 0, "cannot start L2 counters exporter\n");
  atexit(stop_at_exit);
  LOG_I(UTIL, "L2 counters: exporting to %s every %d ms\n", path, period_ms);
}

l2c_registry_t *l2c_init_from_config(const char *node)
{
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool done = false;
  pthread_mutex_lock(&lock);
  if (!done) {
    init_once(node);
    done = true;
  }
  pthread_mutex_unlock(&lock);
  return registry;
}
