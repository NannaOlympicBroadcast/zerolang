#include "capability_summary.h"
#include "std_sig.h"

#include <string.h>

typedef enum {
  CAPABILITY_NAME_ARGS,
  CAPABILITY_NAME_ENV,
  CAPABILITY_NAME_FS,
  CAPABILITY_NAME_MEMORY,
  CAPABILITY_NAME_ALLOC,
  CAPABILITY_NAME_PATH,
  CAPABILITY_NAME_CODEC,
  CAPABILITY_NAME_PARSE,
  CAPABILITY_NAME_TIME,
  CAPABILITY_NAME_RAND,
  CAPABILITY_NAME_NET,
  CAPABILITY_NAME_PROC,
  CAPABILITY_NAME_WEB,
  CAPABILITY_NAME_WORLD
} CapabilityName;

typedef struct {
  const char *name;
  CapabilityName capability;
} CapabilityNameEntry;

static bool capability_name_equals(const char *left, const char *right) {
  return left && right && strcmp(left, right) == 0;
}

static bool capability_name_starts_with(const char *name, const char *prefix) {
  return name && prefix && strncmp(name, prefix, strlen(prefix)) == 0;
}

static void capability_summary_set_id(CapabilitySummary *caps, CapabilityName capability) {
  if (!caps) return;
  switch (capability) {
    case CAPABILITY_NAME_ARGS: caps->args = true; break;
    case CAPABILITY_NAME_ENV: caps->env = true; break;
    case CAPABILITY_NAME_FS: caps->fs = true; break;
    case CAPABILITY_NAME_MEMORY: caps->memory = true; break;
    case CAPABILITY_NAME_ALLOC:
      caps->alloc = true;
      caps->memory = true;
      break;
    case CAPABILITY_NAME_PATH: caps->path = true; break;
    case CAPABILITY_NAME_CODEC: caps->codec = true; break;
    case CAPABILITY_NAME_PARSE: caps->parse = true; break;
    case CAPABILITY_NAME_TIME: caps->time = true; break;
    case CAPABILITY_NAME_RAND: caps->rand = true; break;
    case CAPABILITY_NAME_NET: caps->net = true; break;
    case CAPABILITY_NAME_PROC: caps->proc = true; break;
    case CAPABILITY_NAME_WEB: caps->web = true; break;
    case CAPABILITY_NAME_WORLD: caps->world = true; break;
  }
}

static bool capability_summary_apply_exact(const CapabilityNameEntry *entries, size_t len, const char *name, CapabilitySummary *caps) {
  if (!name || !caps) return false;
  for (size_t i = 0; i < len; i++) {
    if (!capability_name_equals(name, entries[i].name)) continue;
    capability_summary_set_id(caps, entries[i].capability);
    return true;
  }
  return false;
}

static bool capability_summary_apply_prefix(const CapabilityNameEntry *entries, size_t len, const char *name, CapabilitySummary *caps) {
  if (!name || !caps) return false;
  for (size_t i = 0; i < len; i++) {
    if (!capability_name_starts_with(name, entries[i].name)) continue;
    capability_summary_set_id(caps, entries[i].capability);
    return true;
  }
  return false;
}

void z_capability_summary_set(CapabilitySummary *caps, const char *capability) {
  static const CapabilityNameEntry names[] = {
    {"args", CAPABILITY_NAME_ARGS},
    {"env", CAPABILITY_NAME_ENV},
    {"fs", CAPABILITY_NAME_FS},
    {"memory", CAPABILITY_NAME_MEMORY},
    {"alloc", CAPABILITY_NAME_ALLOC},
    {"path", CAPABILITY_NAME_PATH},
    {"codec", CAPABILITY_NAME_CODEC},
    {"parse", CAPABILITY_NAME_PARSE},
    {"time", CAPABILITY_NAME_TIME},
    {"rand", CAPABILITY_NAME_RAND},
    {"net", CAPABILITY_NAME_NET},
    {"proc", CAPABILITY_NAME_PROC},
    {"web", CAPABILITY_NAME_WEB},
    {"world", CAPABILITY_NAME_WORLD},
  };
  capability_summary_apply_exact(names, sizeof(names) / sizeof(names[0]), capability, caps);
}

static void capability_summary_collect_std_mem_name(const char *name, CapabilitySummary *caps) {
  static const CapabilityNameEntry allocator_names[] = {
    {"std.mem.nullAlloc", CAPABILITY_NAME_ALLOC},
    {"std.mem.fixedBufAlloc", CAPABILITY_NAME_ALLOC},
    {"std.mem.arena", CAPABILITY_NAME_ALLOC},
    {"std.mem.allocBytes", CAPABILITY_NAME_ALLOC},
    {"std.mem.byteBuf", CAPABILITY_NAME_ALLOC},
    {"std.mem.reset", CAPABILITY_NAME_ALLOC},
    {"std.mem.capacity", CAPABILITY_NAME_ALLOC},
  };
  if (!name || !caps || !capability_name_starts_with(name, "std.mem.")) return;
  caps->memory = true;
  capability_summary_apply_exact(allocator_names, sizeof(allocator_names) / sizeof(allocator_names[0]), name, caps);
}

void z_capability_summary_collect_std_name(const char *name, CapabilitySummary *caps) {
  static const CapabilityNameEntry prefixes[] = {
    {"std.args.", CAPABILITY_NAME_ARGS},
    {"std.env.", CAPABILITY_NAME_ENV},
    {"std.fs.", CAPABILITY_NAME_FS},
    {"std.path.", CAPABILITY_NAME_PATH},
    {"std.codec.", CAPABILITY_NAME_CODEC},
    {"std.parse.", CAPABILITY_NAME_PARSE},
    {"std.json.", CAPABILITY_NAME_PARSE},
    {"std.time.", CAPABILITY_NAME_TIME},
    {"std.rand.", CAPABILITY_NAME_RAND},
    {"std.proc.", CAPABILITY_NAME_PROC},
    {"std.crypto.secureRandom", CAPABILITY_NAME_RAND},
    {"std.crypto.", CAPABILITY_NAME_CODEC},
    {"std.net.", CAPABILITY_NAME_NET},
    {"std.http.", CAPABILITY_NAME_NET},
  };
  if (!name || !caps) return;
  const ZStdHelperInfo *helper = z_std_helper_find(name);
  if (helper) {
    z_capability_summary_set(caps, helper->capability);
    return;
  }
  if (capability_summary_apply_prefix(prefixes, sizeof(prefixes) / sizeof(prefixes[0]), name, caps)) {
    return;
  }
  if (capability_name_starts_with(name, "std.mem.")) {
    capability_summary_collect_std_mem_name(name, caps);
  }
}
