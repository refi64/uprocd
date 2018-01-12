/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "api/uprocd.h"

#include <stdio.h>
#include <unistd.h>

UPROCD_EXPORT void uprocd_module_entry() {
  puts("Inside uprocd_module_entry");
  uprocd_context *ctx = uprocd_run();
  puts("Got uprocd_context...");
  uprocd_context_enter(ctx);
  puts("Entered context!");
  sleep(1);
  puts("Finished sleep!");
}
