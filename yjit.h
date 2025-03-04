//
// This file contains definitions YJIT exposes to the CRuby codebase
//

#ifndef YJIT_H
#define YJIT_H 1

#include "ruby/internal/config.h"
#include "ruby_assert.h" // for RUBY_DEBUG
#include "vm_core.h"
#include "method.h"

#ifdef _WIN32
#define PLATFORM_SUPPORTED_P 0
#else
#define PLATFORM_SUPPORTED_P 1
#endif

#define JIT_ENABLED USE_MJIT

#ifndef YJIT_CHECK_MODE
#define YJIT_CHECK_MODE 0
#endif

// >= 1: print when output code invalidation happens
// >= 2: dump list of instructions when regions compile
#ifndef YJIT_DUMP_MODE
#define YJIT_DUMP_MODE 0
#endif

#ifndef YJIT_STATS
# define YJIT_STATS RUBY_DEBUG
#endif // ifndef YJIT_STATS

#ifndef rb_iseq_t
typedef struct rb_iseq_struct rb_iseq_t;
#define rb_iseq_t rb_iseq_t
#endif

struct rb_yjit_options {
    // Enable compilation with YJIT
    bool yjit_enabled;

    // Size of the executable memory block to allocate in MiB
    unsigned exec_mem_size;

    // Number of method calls after which to start generating code
    // Threshold==1 means compile on first execution
    unsigned call_threshold;

    // Generate versions greedily until the limit is hit
    bool greedy_versioning;

    // Maximum number of versions per block
    // 1 means always create generic versions
    unsigned max_versions;

    // Capture and print out stats
    bool gen_stats;

    // Run backend tests
    bool test_backend;
};

RUBY_SYMBOL_EXPORT_BEGIN
bool rb_yjit_enabled_p(void);
unsigned rb_yjit_call_threshold(void);
RUBY_SYMBOL_EXPORT_END

void rb_yjit_invalidate_all_method_lookup_assumptions(void);
void rb_yjit_method_lookup_change(VALUE klass, ID mid);
void rb_yjit_cme_invalidate(VALUE cme);
void rb_yjit_collect_vm_usage_insn(int insn);
void rb_yjit_collect_binding_alloc(void);
void rb_yjit_collect_binding_set(void);
bool rb_yjit_compile_iseq(const rb_iseq_t *iseq, rb_execution_context_t *ec);
void rb_yjit_init(struct rb_yjit_options *options);
void rb_yjit_bop_redefined(VALUE klass, const rb_method_entry_t *me, enum ruby_basic_operators bop);
void rb_yjit_constant_state_changed(void);
void rb_yjit_iseq_update_references(const struct rb_iseq_constant_body *body);
void rb_yjit_iseq_free(const struct rb_iseq_constant_body *body);
void rb_yjit_before_ractor_spawn(void);
void yjit_constant_ic_update(const rb_iseq_t *iseq, IC ic);
void yjit_tracing_invalidate_all(void);
void rb_yjit_mark_iseq_entry_blocks(const rb_iseq_t *iseq);

#endif // #ifndef YJIT_H
