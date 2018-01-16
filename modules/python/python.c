/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "api/uprocd.h"

#include <Python.h>

UPROCD_EXPORT void uprocd_module_entry() {
  Py_SetProgramName(L"python");
  Py_Initialize();

  char *modules_path = uprocd_module_path("_uprocd_modules.py");
  FILE *modules = fopen(modules_path, "r");
  if (modules == NULL) {
    fprintf(stderr, "Error loading _uprocd_modules.py: %s\n", strerror(errno));
    return;
  }

  PyRun_SimpleFileEx(modules, modules_path, 1);
  uprocd_module_path_free(modules_path);

  const char *preload = uprocd_config_string("Preload");
  PyRun_SimpleString(preload);

  uprocd_context *ctx = uprocd_run();
  uprocd_context_enter(ctx);

  int argc, has_run = 0;
  char **argv;
  uprocd_context_get_args(ctx, &argc, &argv);

  const char *run = uprocd_config_string("Run");
  if (strlen(run) == 0) {
    has_run = 1;
  }

  wchar_t **wargv = PyMem_RawMalloc(sizeof(wchar_t*) * (argc + (has_run ? 2 : 0) + 1));
  if (wargv == NULL) {
    fprintf(stderr, "Error initializing Python interpreter: Out of memory.\n");
    return;
  }

  if (has_run) {
    wargv[0] = L"-c";
    wargv[1] = Py_DecodeLocale(run, NULL);
    if (wargv[1] == NULL) {
      fprintf(stderr, "Error decoding Run argument.\n");
      return;
    }
  }

  for (int i = has_run ? 2 : 0; i < argc; i++) {
    wargv[i] = Py_DecodeLocale(argv[i], NULL);
    if (wargv[i] == NULL) {
      fprintf(stderr, "Error initializing Python interpreter: Error decoding argv.\n");
      return;
    }
  }

  wargv[argc] = NULL;
  Py_Main(argc, wargv);

  uprocd_context_free(ctx);
}
