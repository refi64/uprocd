/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uprocd.h"

#include <Python.h>

void set_environment(uprocd_context *ctx) {
  PyObject *osmod = NULL, *environ = NULL;

  osmod = PyImport_ImportModule("os");
  if (osmod == NULL) {
    PyErr_Print();
    goto end;
  }

  environ = PyObject_GetAttrString(osmod, "environ");
  if (environ == NULL) {
    PyErr_Print();
    goto end;
  }

  PyObject *clear_result = PyObject_CallMethod(environ, "clear", NULL);
  if (clear_result == NULL) {
    PyErr_Print();
    goto end;
  }
  Py_DECREF(clear_result);

  for (const char **env = uprocd_context_get_env(ctx); *env != NULL; env += 2) {
    PyObject *key = PyUnicode_FromString(env[0]),
             *value = PyUnicode_FromString(env[1]);
    int rc = PyObject_SetItem(environ, key, value);
    Py_DECREF(key);
    Py_DECREF(value);
    if (rc == -1) {
      PyErr_Print();
    }
  }

  end:
  if (environ) {
    Py_DECREF(environ);
  }
  if (osmod) {
    Py_DECREF(osmod);
  }
}

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

  set_environment(ctx);

  int argc, has_run_offset = 0;
  char **argv;
  uprocd_context_get_args(ctx, &argc, &argv);


  const char *run = uprocd_config_string("Run");
  if (strlen(run) != 0) {
    has_run_offset = 2;
  }

  argc += has_run_offset;

  wchar_t **wargv = PyMem_RawMalloc(sizeof(wchar_t*) * (argc + 1));
  if (wargv == NULL) {
    fprintf(stderr, "Error initializing Python interpreter: Out of memory.\n");
    return;
  }

  wargv[0] = L"python";

  if (has_run_offset) {
    wargv[1] = L"-c";
    wargv[2] = Py_DecodeLocale(run, NULL);
    if (wargv[2] == NULL) {
      fprintf(stderr, "Error decoding Run argument.\n");
      return;
    }
  }

  for (int i = has_run_offset + 1; i < argc; i++) {
    wargv[i] = Py_DecodeLocale(argv[i - has_run_offset], NULL);
    if (wargv[i] == NULL) {
      fprintf(stderr, "Error initializing Python interpreter: Error decoding argv.\n");
      return;
    }
  }

  wargv[argc] = NULL;
  Py_Main(argc, wargv);

  uprocd_context_free(ctx);
}
