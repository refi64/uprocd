/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sds.h>

#define UPROCD_SERVICE_PREFIX "com.refi64.uprocd"
#define UPROCD_OBJECT_PREFIX "/com/refi64/uprocd"

#define UPROCD_DBUS_RUN_ARGUMENTS "a{ss}sx"
#define UPROCD_DBUS_RUN_RETURN "x"

void * alloc(size_t sz);
#define newa(ty, n) ((ty*)alloc(sizeof(ty) * n))
#define new(ty) newa(ty, 1)

/* typedef struct error error; */
/* struct error { */
/*   enum { */
/*     ERR_SUCCESS, */
/*     ERR_MISC, */
/*     ERR_ERRNO, */
/*     ERR_DL, */
/*     ERR_PARSE, */
/*   } what; */

/*   union { */
/*     int errno_; */
/*     struct { */
/*       int line; */
/*     } parse; */
/*   }; */

/*   sds reason; */
/* }; */


/* #define ERROR_SUCCESS() (error){.what = ERR_SUCCESS, .reason = NULL} */
/* #define ERROR_MISC(reason_) (error){.what = ERR_MISC, .reason = sdsnew(reason_)} */
/* #define ERROR_ERRNO() (error){.what = ERR_ERRNO, .errno_ = errno, .reason = NULL} */

/* const char * error_reason(const error *err); */
/* void error_clear(error *perr); */

#endif
