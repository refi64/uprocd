/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uprocd.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-attributes"
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#include <ruby.h>

#ifdef __GCC__
#pragma GCC diagnostic pop
#endif

void set_environment(uprocd_context *ctx) {
  VALUE rbenv = rb_const_get_at(rb_cObject, rb_intern("ENV"));
  rb_funcall(rbenv, rb_intern("clear"), 0);

  for (const char **env = uprocd_context_get_env(ctx); *env != NULL; env += 2) {
    VALUE key = rb_str_new_cstr(env[0]),
          value = rb_str_new_cstr(env[1]);
    rb_funcall(rbenv, rb_intern("store"), 2, key, value);
  }
}

static void check_error(const char *message) {
  VALUE exc = rb_errinfo();
  rb_set_errinfo(Qnil);

  VALUE msg = rb_funcall(exc, rb_intern("full_message"), 0);
  rb_funcall(rb_mKernel, rb_intern("puts"), 2, rb_str_new_cstr(message), msg);
}

VALUE ruby_entry(VALUE udata) {
  VALUE verbose = ruby_verbose;
  ruby_verbose = Qnil;

  const char *preload = uprocd_config_string("Preload");
  const char *load_options[] = {"ruby", "-I", uprocd_module_directory(),
                                "-r_uprocd_requires", "-e", preload};
  int state = ruby_exec_node(
    ruby_options(sizeof(load_options) / sizeof(load_options[0]), (char**)load_options));
  if (state) {
    check_error("Error running preload code:");
    return 0;
  }

  uprocd_context *ctx = uprocd_run();
  uprocd_context_enter(ctx);

  set_environment(ctx);

  int argc;
  char **argv;
  uprocd_context_get_args(ctx, &argc, &argv);

  const char *run = uprocd_config_string("Run");
  int has_run_offset = 0;
  if (strlen(run) != 0) {
    has_run_offset = 3;
  }

  int ruby_argc = argc + has_run_offset;
  const char *ruby_argv[ruby_argc];

  if (has_run_offset) {
    ruby_argv[0] = "ruby";
    ruby_argv[1] = "-e";
    ruby_argv[2] = run;
  }

  for (int i = 0; i < argc; i++) {
    ruby_argv[i + has_run_offset] = argv[i];
  }

  void *node = ruby_options(ruby_argc, (char**)ruby_argv);

  uprocd_context_free(ctx);

  ruby_verbose = verbose;
  return ruby_run_node(node);
}

UPROCD_EXPORT void uprocd_module_entry() {
  ruby_init();
  ruby_init_loadpath();

  int state;
  int ret = rb_protect(ruby_entry, 0, &state);
  if (state) {
    check_error("Error:");
    exit(1);
  } else {
    exit(ret);
  }
}
