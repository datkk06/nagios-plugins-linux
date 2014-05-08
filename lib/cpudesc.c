/*
 * License: GPLv3+
 * Copyright (c) 2014 Davide Madrisan <davide.madrisan@gmail.com>
 *
 * A library for checking the CPU features
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "messages.h"
#include "procparser.h"
#include "xalloc.h"

#define PATH_PROC_CPUINFO	"/proc/cpuinfo"

/* Get the number of total and active cpus */

int
get_processor_number_total (void)
{
  return
#if defined (HAVE_GET_NPROCS_CONF)
    get_nprocs_conf ();
#elif defined (HAVE_SYSCONF__SC_NPROCESSORS_CONF)
    sysconf (_SC_NPROCESSORS_CONF);
#else
    -1;
#endif
}

int
get_processor_number_online (void)
{
  return
#if defined (HAVE_GET_NPROCS)
    get_nprocs ();
#elif defined (HAVE_SYSCONF__SC_NPROCESSORS_ONLN)
    sysconf (_SC_NPROCESSORS_ONLN);
#else
    -1;
#endif
}

enum	/* CPU modes */
{
  MODE_32BIT = (1 << 1),
  MODE_64BIT = (1 << 2)
};

struct cpu_desc
{
  int refcount;

  char *arch;
  char *vendor;
  char *family;
  char *model;
  char *modelname;
  char *virtflag;	/* virtualization flag (vmx, svm) */
  char *mhz;
  char *flags;		/* x86 */
  int mode;
  int ncpus;		/* number of present CPUs */
};

/* Allocates space for a new cpu_desc object.
 * Returns 0 if all went ok. Errors are returned as negative values.  */
int
cpu_desc_new (struct cpu_desc **cpudesc)
{
  struct cpu_desc *d;

  d = calloc (1, sizeof (struct cpu_desc));
  if (!d)
    return -ENOMEM;

  d->refcount = 1;

  *cpudesc = d;
  return 0;
}

/* Fill the cpu_desc structure pointed with the values found in the 
 * proc filesystem */

void
cpu_desc_read (struct cpu_desc *cpudesc)
{
  char *line = NULL, *buf;
  FILE *fp;
  size_t len = 0;
  ssize_t chread;
  struct utsname utsbuf;

  if (cpudesc == NULL)
    return;

  if ((fp = fopen (PATH_PROC_CPUINFO,  "r")) == NULL)
    plugin_error (STATE_UNKNOWN, errno, "error opening %s", PATH_PROC_CPUINFO);

  if (uname (&utsbuf) == -1)
    plugin_error (STATE_UNKNOWN, errno, "uname() failed");
  cpudesc->arch = xstrdup (utsbuf.machine);

  cpudesc->ncpus = get_processor_number_total ();

  cpudesc->mode = 0;
#if defined(__alpha__) || defined(__ia64__)
  cpudesc->mode |= MODE_64BIT;		/* 64bit platforms only */
#endif
  /* platforms with 64bit flag in /proc/cpuinfo, define
   * 32bit default here */
#if defined(__i386__) || defined(__x86_64__) || \
    defined(__s390x__) || defined(__s390__) || defined(__sparc_v9__)
  cpudesc->mode |= MODE_32BIT;
#endif

  while ((chread = getline (&line, &len, fp)) != -1)
    {
      if (linelookup (line, "vendor", &cpudesc->vendor));
      else if (linelookup (line, "vendor_id", &cpudesc->vendor));
      else if (linelookup (line, "vendor_id", &cpudesc->vendor));
      else if (linelookup (line, "family", &cpudesc->family));
      else if (linelookup (line, "cpu family", &cpudesc->family));
      else if (linelookup (line, "model", &cpudesc->model));
      else if (linelookup (line, "model name", &cpudesc->modelname)) ;
      else if (linelookup (line, "cpu MHz", &cpudesc->mhz)) ;
      else if (linelookup (line, "flags", &cpudesc->flags)) ;	/* x86 */
      else
	continue;
    }

  if (cpudesc->flags)
    {
      size_t buflen = sizeof (cpudesc->flags) + 2;
      buf = xmalloc (buflen);

      snprintf (buf, buflen, " %s ", cpudesc->flags);
      if (strstr (buf, " svm "))
        cpudesc->virtflag = xstrdup ("svm");
      else if (strstr (buf, " vmx "))
        cpudesc->virtflag = xstrdup ("vmx");
      if (strstr (buf, " lm "))
        cpudesc->mode |= MODE_32BIT | MODE_64BIT;  /* x86_64 */
      if (strstr (buf, " zarch "))
        cpudesc->mode |= MODE_32BIT | MODE_64BIT;  /* s390x */
      if (strstr (buf, " sun4v ") || strstr (buf, " sun4u "))
        cpudesc->mode |= MODE_32BIT | MODE_64BIT;  /* sparc64 */
  
      free (buf);
    }

  free (line);
}

/* Drop a reference of the cpu_desc library context. If the refcount of
 * reaches zero, the resources of the context will be released.  */
struct cpu_desc *
cpu_desc_unref (struct cpu_desc *cpudesc)
{
  if (cpudesc == NULL)
    return NULL;

  cpudesc->refcount--;
  if (cpudesc->refcount > 0)
    return cpudesc;

  free (cpudesc);
  return NULL;
}

char *
cpu_desc_get_architecture (struct cpu_desc *cpudesc)
{
  return cpudesc->arch;
}

char *
cpu_desc_get_vendor (struct cpu_desc *cpudesc)
{
  return cpudesc->vendor;
}

char *
cpu_desc_get_family (struct cpu_desc *cpudesc)
{
  return cpudesc->family;
}

char *
cpu_desc_get_model (struct cpu_desc *cpudesc)
{
  return cpudesc->model;
}

char *
cpu_desc_get_model_name (struct cpu_desc *cpudesc)
{
  return cpudesc->modelname;
}

char *
cpu_desc_get_virtualization_flag (struct cpu_desc *cpudesc)
{
  if (cpudesc->virtflag == NULL)
    return NULL;

  if (!strcmp (cpudesc->virtflag, "svm"))
    return "AMD-V";
  else if (!strcmp (cpudesc->virtflag, "vmx"))
    return "VT-x";
  /* should never been reached */
  else
    return cpudesc->virtflag;
}

char *
cpu_desc_get_mhz (struct cpu_desc *cpudesc)
{
  return cpudesc->mhz;
}

char *
cpu_desc_get_flags (struct cpu_desc *cpudesc)
{
  return cpudesc->flags;
}

int
cpu_desc_get_mode (struct cpu_desc *cpudesc)
{
  return cpudesc->mode;
}

int
cpu_desc_get_number_of_cpus (struct cpu_desc *cpudesc)
{
  return cpudesc->ncpus;
}