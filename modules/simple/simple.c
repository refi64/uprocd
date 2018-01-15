/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "api/uprocd.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

void sig(int s) {
  puts("Got signal");
}

UPROCD_EXPORT void uprocd_module_entry() {
  puts("Inside uprocd_module_entry");
  uprocd_context *ctx = uprocd_run();
  puts("Got uprocd_context...");
  uprocd_context_enter(ctx);
  puts("Entered context!");

  uprocd_config *cfg = uprocd_context_get_config(ctx);
  printf("String: %s\n", uprocd_config_string(cfg, "String"));
  printf("Number: %f\n", uprocd_config_number(cfg, "Number"));

  for (int i = 0; i < uprocd_config_list_size(cfg, "StringList"); i++) {
    printf("StringList %s\n", uprocd_config_string_at(cfg, "StringList", i));
  }

  for (int i = 0; i < uprocd_config_list_size(cfg, "NumberList"); i++) {
    printf("NumberList %f\n", uprocd_config_number_at(cfg, "NumberList", i));
  }

  signal(SIGINT, sig);
  sleep(10);
  puts("Finished sleep!");
}
