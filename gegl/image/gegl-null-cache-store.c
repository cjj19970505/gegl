/*
 *   This file is part of GEGL.
 *
 *    GEGL is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    GEGL is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with GEGL; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Copyright 2003-2004 Daniel S. Rogers
 *
 */

#include "config.h"

#include <glib-object.h>

#include "gegl-image-types.h"

#include "gegl-entry-record.h"
#include "gegl-null-cache-store.h"


static void   gegl_null_cache_store_class_init (GeglNullCacheStoreClass *klass);
static void   gegl_null_cache_store_init       (GeglNullCacheStore      *self);

static void             add           (GeglCacheStore  *self,
                                       GeglEntryRecord *record);
static void             remove        (GeglCacheStore  *self,
                                       GeglEntryRecord *record);
static void             zap           (GeglCacheStore  *self,
                                       GeglEntryRecord *record);
static gint64           size          (GeglCacheStore  *self);
static GeglEntryRecord *pop           (GeglCacheStore  *self);
static GeglEntryRecord *peek          (GeglCacheStore  *self);


G_DEFINE_TYPE (GeglNullCacheStore, gegl_null_cache_store, GEGL_TYPE_CACHE_STORE)


static void
gegl_null_cache_store_class_init (GeglNullCacheStoreClass *klass)
{
  GeglCacheStoreClass *store_class = GEGL_CACHE_STORE_CLASS (klass);

  store_class->add    = add;
  store_class->remove = remove;
  store_class->size   = size;
  store_class->pop    = pop;
  store_class->peek   = peek;
  store_class->zap    = zap;
}

static void
gegl_null_cache_store_init (GeglNullCacheStore *self)
{
  self->status      = GEGL_UNDEFINED;
  self->record_head = NULL;
}

GeglNullCacheStore *
gegl_null_cache_store_new (GeglCacheStatus status)
{
  GeglNullCacheStore *store = g_object_new (GEGL_TYPE_NULL_CACHE_STORE, NULL);

  store->status = status;

  return store;
}

static void
add (GeglCacheStore  *store,
     GeglEntryRecord *record)
{
  GeglNullCacheStore *self = GEGL_NULL_CACHE_STORE (store);
  GList              *record_list;

  gegl_entry_record_set_cache_store (record, store);

  record_list = g_list_append (NULL, record);

  self->record_head = g_list_concat (self->record_head, record_list);

  gegl_entry_record_add_store_data (record, store, record_list);

  record->status = self->status;

  if (record->entry)
    {
      g_object_unref (record->entry);
      record->entry = NULL;
    }
}

static void
remove (GeglCacheStore  *store,
        GeglEntryRecord *record)
{
  GeglNullCacheStore *self = GEGL_NULL_CACHE_STORE (store);
  GList              *record_list;

  record_list = gegl_entry_record_get_store_data (record, store);

  self->record_head = g_list_delete_link (self->record_head, record_list);

  gegl_entry_record_remove_store_data (record, store, FALSE);
  gegl_entry_record_set_cache_store (record, NULL);

  record->status = GEGL_UNDEFINED;
}

static void
zap (GeglCacheStore  *store,
     GeglEntryRecord *record)
{
  remove (store, record);
  gegl_entry_record_free (record);
}

static gint64
size (GeglCacheStore *self)
{
  return 0;
}

static GeglEntryRecord *
pop (GeglCacheStore *store)
{
  GeglEntryRecord *record = peek (store);

  if (! record)
    return NULL;

  remove (store, record);

  return record;
}

static GeglEntryRecord *
peek (GeglCacheStore *store)
{
  GeglNullCacheStore *self = GEGL_NULL_CACHE_STORE (store);
  GeglEntryRecord    *record;

  if (! self->record_head)
    return NULL;

  record = self->record_head->data;

  g_return_val_if_fail (record != NULL, NULL);

  return record;
}
