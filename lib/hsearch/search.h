/* Declarations for System V style searching functions.
   Copyright (C) 1995-1999, 2000, 2012 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _SEARCH_H_
#define	_SEARCH_H_

#define __need_size_t
#include <stddef.h>

/* For use with hsearch(3).  */
#ifndef __COMPAR_FN_T
# define __COMPAR_FN_T
typedef int (*__compar_fn_t) (const void *, const void *);

# ifdef	__USE_GNU
typedef __compar_fn_t comparison_fn_t;
# endif
#endif

/* Action which shall be performed in the call the hsearch.  */
typedef enum
  {
    FIND,
    ENTER
  }
ACTION;

typedef struct entry
  {
    char *key;
    void *data;
  }
ENTRY;

/* Opaque type for internal use.  */
struct _ENTRY;

/* Family of hash table handling functions.  The functions also
   have reentrant counterparts ending with _r.  The non-reentrant
   functions all work on a signle internal hashing table.  */

/* Search for entry matching ITEM.key in internal hash table.  If
   ACTION is `FIND' return found entry or signal error by returning
   NULL.  If ACTION is `ENTER' replace existing data (if any) with
   ITEM.data.  */
extern ENTRY *hsearch (ENTRY __item, ACTION __action);

/* Create a new hashing table which will at most contain NEL elements.  */
extern int hcreate (size_t __nel);

/* Destroy current internal hashing table.  */
extern void hdestroy (void);

/* Data type for reentrant functions.  */
struct hsearch_data
  {
    struct _ENTRY *table;
    unsigned int size;
    unsigned int filled;
  };

/* Reentrant versions which can handle multiple hashing tables at the
   same time.  */
int hsearch_r (ENTRY __item, ACTION __action, ENTRY **__retval,
		      struct hsearch_data *__htab);
int hcreate_r (size_t __nel, struct hsearch_data *__htab);
void hdestroy_r (struct hsearch_data *__htab);

#endif /* search.h */
