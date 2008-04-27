/* This file is part of GEGL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006, 2007, 2008 Øyvind Kolås <pippin@gimp.org>
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>
#include <errno.h>

#include <glib-object.h>
#include <glib/gprintf.h>

#include "gegl-tile-backend.h"
#include "gegl-tile-backend-file.h"
#include "gegl-buffer-index.h"

#include "gegl-debug.h"

/*#define HACKED_GIO_WITH_READWRITE 1
*/

struct _GeglTileBackendFile
{
  GeglTileBackend  parent_instance;

  gchar           *path;    /* the path to our buffer */
  GFile           *file;    /* gfile refering to our buffer */
  GOutputStream   *o;       /* for writing */
  GInputStream    *i;       /* for reading */
  gboolean         exist;   /* the file exist (and we've thus been able
                             * to initialize i and o, the utility_call ensure_exist
                             * should be called before any code using i and o)
                             */

  GHashTable      *index;   /* hashtable containing all entries
                             * of buffer, the index is
                             * written to the swapfile conforming
                             * to the structures laid out in
                             * gegl-buffer-index.h
                             */

  GSList          *free_list; /* list of offsets to tiles that are free */

  guint            next_pre_alloc; /* offset to next pre allocated tile slot */
  guint            total;          /* total size of file */

  /* duplicated from gegl-buffer-save */

  GeglBufferHeader header;     /* a local copy of the header that will be
                                * written to the file, in a multiple user
                                * per buffer scenario, the flags in the
                                * header might be used for locking/signalling
                                */

  gint             offset;     /* current offset, used when writing the
                                * index,
                                */
  GeglBufferBlock *in_holding; /* when writing buffer blocks the writer
                                * keeps one block unwritten at all times
                                * to be able to keep track of the ->next
                                * offsets in the blocks.
                                */

  /* loading buffer */

  GList *tiles;
};

static void ensure_exist (GeglTileBackendFile *self);

static gboolean
write_block (GeglTileBackendFile *self,
             GeglBufferBlock     *block);

static void dbg_alloc (int size);
static void dbg_dealloc (int size);

static void inline
file_entry_read (GeglTileBackendFile *self,
                 GeglBufferTile      *entry,
                 guchar              *dest)
{
  gint     to_be_read;
  gboolean success;
  gint     tile_size = GEGL_TILE_BACKEND (self)->tile_size;
  goffset  offset = entry->offset;

  ensure_exist (self);

  success = g_seekable_seek (G_SEEKABLE (self->i), 
                             offset, G_SEEK_SET,
                             NULL, NULL);
  if (success == FALSE)
    {
      g_warning ("unable to seek to tile in buffer: %s", g_strerror (errno));
      return;
    }
  to_be_read = tile_size;

  while (to_be_read > 0)
    {
      gint read;

      read = g_input_stream_read (G_INPUT_STREAM (self->i),
                                 dest + tile_size - to_be_read, to_be_read,
                                 NULL, NULL);
      if (read <= 0)
        {
          g_message ("unable to read tile data from self: "
                     "%s (%d/%d bytes read)",
                     g_strerror (errno), read, to_be_read);
          return;
        }
      to_be_read -= read;
    }


  GEGL_NOTE (TILE_BACKEND, "read entry %i,%i,%i at %i", entry->x, entry->y, entry->z, (gint)offset);
}

static void inline
file_entry_write (GeglTileBackendFile *self,
                  GeglBufferTile      *entry,
                  guchar              *source)
{
  gint     to_be_written;
  gboolean success;
  gint     tile_size = GEGL_TILE_BACKEND (self)->tile_size;
  goffset  offset = entry->offset;

  ensure_exist (self);

  success = g_seekable_seek (G_SEEKABLE (self->o), 
                             offset, G_SEEK_SET,
                             NULL, NULL);
  if (success == FALSE)
    {
      g_warning ("unable to seek to tile in buffer: %s", g_strerror (errno));
      return;
    }
  to_be_written = tile_size;

  while (to_be_written > 0)
    {
      gint wrote;
      wrote = g_output_stream_write (self->o,
                                     source + tile_size - to_be_written,
                                     to_be_written, NULL, NULL);

      if (wrote <= 0)
        {
          g_message ("unable to write tile data to self: "
                     "%s (%d/%d bytes written)",
                     g_strerror (errno), wrote, to_be_written);
          return;
        }
      to_be_written -= wrote;
    }
   GEGL_NOTE (TILE_BACKEND, "wrote entry %i,%i,%i at %i", entry->x, entry->y, entry->z, (gint)offset);
}

static inline GeglBufferTile *
file_entry_new (GeglTileBackendFile *self)
{
  GeglBufferTile *entry = gegl_tile_entry_new (0,0,0);

  GEGL_NOTE (TILE_BACKEND, "Creating new entry");

  ensure_exist (self);

  if (self->free_list)
    {
      /* XXX: losing precision ? */
      gint offset = GPOINTER_TO_INT (self->free_list->data);
      entry->offset = offset;
      self->free_list = g_slist_remove (self->free_list, self->free_list->data);
      
      GEGL_NOTE (TILE_BACKEND, "  set offset %i from free list", ((gint)entry->offset));
    }
  else
    {
      gint tile_size = GEGL_TILE_BACKEND (self)->tile_size;

      entry->offset = self->next_pre_alloc;
      GEGL_NOTE (TILE_BACKEND, "  set offset %i (next allocation)", (gint)entry->offset);
      self->next_pre_alloc += tile_size;

      if (self->next_pre_alloc >= self->total)
        {
          self->total = self->total + 32 * tile_size;
        
          GEGL_NOTE (TILE_BACKEND, "growing file to %i bytes", (gint)self->total);

          g_assert (g_seekable_truncate (G_SEEKABLE (self->o),
                    self->total, NULL,NULL));
        }
    }
  dbg_alloc (GEGL_TILE_BACKEND (self)->tile_size);
  return entry;
}

static inline void
file_entry_destroy (GeglBufferTile      *entry,
                    GeglTileBackendFile *self)
{
  /* XXX: EEEk, throwing away bits */
  guint offset = entry->offset;
  self->free_list = g_slist_prepend (self->free_list,
                                     GUINT_TO_POINTER (offset));
  g_hash_table_remove (self->index, entry);

  dbg_dealloc (GEGL_TILE_BACKEND (self)->tile_size);
  g_slice_free (GeglBufferTile, entry);
}

static gboolean write_header (GeglTileBackendFile *self)
{
  gboolean success;

  ensure_exist (self);

  success = g_seekable_seek (G_SEEKABLE (self->o), 0, G_SEEK_SET,
                             NULL, NULL);
  if (success == FALSE)
    {
      g_warning ("unable to seek in buffer");
      return FALSE;
    }
  strcpy (&(self->header.description[0]) + 20, "fnord");
  g_output_stream_write (self->o, &(self->header), 256, NULL, NULL);
  GEGL_NOTE (TILE_BACKEND, "Wrote header, next=%i", (gint)self->header.next);
  return TRUE;
}

static gboolean
write_block (GeglTileBackendFile *self,
             GeglBufferBlock     *block)
{
  ensure_exist (self);
   if (self->in_holding)
     {
       guint64 next_allocation = self->offset + self->in_holding->length;

       /* update the next offset pointer in the previous block */
       self->in_holding->next = next_allocation;

       if (block == NULL) /* the previous block was the last block */
         {
           self->in_holding->next = 0;
         }

       if(!g_seekable_seek (G_SEEKABLE (self->o),
                            self->offset, G_SEEK_SET,
                            NULL, NULL))
         goto fail;

       GEGL_NOTE (TILE_BACKEND, "Wrote block: length:%i flags:%i next:%i at offset %i",
             self->in_holding->length,
             self->in_holding->flags,
             (gint)self->in_holding->next,
             (gint)self->offset);
       self->offset += g_output_stream_write (self->o, self->in_holding,
                                              self->in_holding->length, 
                                              NULL, NULL);

       g_assert (next_allocation == self->offset); /* true as long as
                                                      the simple allocation
                                                      scheme is used */

       self->offset = next_allocation;
     }
   else
     {
        /* we're setting up for the first write */

        self->offset = self->next_pre_alloc; /* start writing header at end
                                              * of file, worry about writing
                                              * header inside free list later
                                              */

        if(!g_seekable_seek (G_SEEKABLE (self->o), 
                             (goffset) self->offset, G_SEEK_SET,
                             NULL, NULL))
          goto fail;
     }
   self->in_holding = block;

   return TRUE;
fail:
   g_warning ("gegl buffer index writing problems for %s",
              self->path);
   return FALSE;
}

G_DEFINE_TYPE (GeglTileBackendFile, gegl_tile_backend_file, GEGL_TYPE_TILE_BACKEND)
static GObjectClass * parent_class = NULL;

/* this debugging is across all buffers */

static gint allocs         = 0;
static gint file_size      = 0;
static gint peak_allocs    = 0;
static gint peak_file_size = 0;

void
gegl_tile_backend_file_stats (void)
{
  g_warning ("leaked: %i chunks (%f mb)  peak: %i (%i bytes %fmb))",
             allocs, file_size / 1024 / 1024.0,
             peak_allocs, peak_file_size, peak_file_size / 1024 / 1024.0);
}

static void
dbg_alloc (gint size)
{
  allocs++;
  file_size += size;
  if (allocs > peak_allocs)
    peak_allocs = allocs;
  if (file_size > peak_file_size)
    peak_file_size = file_size;
}

static void
dbg_dealloc (gint size)
{
  allocs--;
  file_size -= size;
}

static inline GeglBufferTile *
lookup_entry (GeglTileBackendFile *self,
              gint                 x,
              gint                 y,
              gint                 z)
{
  GeglBufferTile *ret;
  GeglBufferTile *key = gegl_tile_entry_new (x,y,z);
  ret = g_hash_table_lookup (self->index, key);
  gegl_tile_entry_destroy (key);
  return ret;
}

/* this is the only place that actually should
 * instantiate tiles, when the cache is large enough
 * that should make sure we don't hit this function
 * too often.
 */
static GeglTile *
get_tile (GeglTileSource *self,
          gint            x,
          gint            y,
          gint            z)
{

  GeglTileBackend     *backend;
  GeglTileBackendFile *tile_backend_file;
  GeglBufferTile      *entry;
  GeglTile            *tile = NULL;

  backend           = GEGL_TILE_BACKEND (self);
  tile_backend_file = GEGL_TILE_BACKEND_FILE (backend);
  entry             = lookup_entry (tile_backend_file, x, y, z);

  if (!entry)
    return NULL;

  tile             = gegl_tile_new (backend->tile_size);
  tile->stored_rev = 1;
  tile->rev        = 1;

  file_entry_read (tile_backend_file, entry, tile->data);
  return tile;
}

static gpointer
set_tile (GeglTileSource *self,
          GeglTile       *tile,
          gint            x,
          gint            y,
          gint            z)
{
  GeglTileBackend     *backend;
  GeglTileBackendFile *tile_backend_file;
  GeglBufferTile      *entry;

  backend           = GEGL_TILE_BACKEND (self);
  tile_backend_file = GEGL_TILE_BACKEND_FILE (backend);
  entry             = lookup_entry (tile_backend_file, x, y, z);

  if (entry == NULL)
    {
      entry    = file_entry_new (tile_backend_file);
      entry->x = x;
      entry->y = y;
      entry->z = z;
      g_hash_table_insert (tile_backend_file->index, entry, entry);
    }

  file_entry_write (tile_backend_file, entry, tile->data);
  tile->stored_rev = tile->rev;
  return NULL;
}

static gpointer
void_tile (GeglTileSource *self,
           GeglTile       *tile,
           gint            x,
           gint            y,
           gint            z)
{
  GeglTileBackend     *backend;
  GeglTileBackendFile *tile_backend_file;
  GeglBufferTile      *entry;

  backend           = GEGL_TILE_BACKEND (self);
  tile_backend_file = GEGL_TILE_BACKEND_FILE (backend);
  entry             = lookup_entry (tile_backend_file, x, y, z);

  if (entry != NULL)
    {
      file_entry_destroy (entry, tile_backend_file);
    }

  return NULL;
}

static gpointer
exist_tile (GeglTileSource *self,
            GeglTile       *tile,
            gint            x,
            gint            y,
            gint            z)
{
  GeglTileBackend         *backend;
  GeglTileBackendFile *tile_backend_file;
  GeglBufferTile               *entry;

  backend               = GEGL_TILE_BACKEND (self);
  tile_backend_file = GEGL_TILE_BACKEND_FILE (backend);
  entry                 = lookup_entry (tile_backend_file, x, y, z);

  return entry!=NULL?((gpointer)0x1):NULL;
}

#include "gegl-buffer-index.h"

static gpointer
flush (GeglTileSource *source,
       GeglTile       *tile,
       gint            x,
       gint            y,
       gint            z)
{
  GeglTileBackend     *backend;
  GeglTileBackendFile *self;
  GList               *tiles;

  backend  = GEGL_TILE_BACKEND (source);
  self     = GEGL_TILE_BACKEND_FILE (backend);

  ensure_exist (self);

  GEGL_NOTE (TILE_BACKEND, "flushing %s", self->path);


  self->header.next = self->next_pre_alloc; /* this is the offset
                                               we start handing
                                               out headers from*/
  tiles = g_hash_table_get_keys (self->index);

  if (tiles == NULL)
    self->header.next = 0;
  else
    {
      GList *iter;
      for (iter = tiles; iter; iter = iter->next)
        {
          GeglBufferItem *item = iter->data;

          write_block (self, &item->block);
        }
      write_block (self, NULL); /* terminate the index */
      g_list_free (tiles);
    }

  write_header (self);
  g_output_stream_flush (self->o, NULL, NULL);

  GEGL_NOTE (TILE_BACKEND, "flushed %s", self->path);

  return (gpointer)0xf0f;
}

enum
{
  PROP_0,
  PROP_PATH
};

static gpointer
command (GeglTileSource  *self,
         GeglTileCommand  command,
         gint             x,
         gint             y,
         gint             z,
         gpointer         data)
{
  switch (command)
    {
      case GEGL_TILE_GET:
        return get_tile (self, x, y, z);
      case GEGL_TILE_SET:
        return set_tile (self, data, x, y, z);

      case GEGL_TILE_IDLE:
        return NULL;       /* we could perhaps lazily be writing indexes
                              at some intervals, making it work as an
                              autosave for the buffer?
                            */

      case GEGL_TILE_VOID:
        return void_tile (self, data, x, y, z);

      case GEGL_TILE_EXIST:
        return exist_tile (self, data, x, y, z);
      case GEGL_TILE_FLUSH:
        return flush (self, data, x, y, z);

      default:
        g_assert (command < GEGL_TILE_LAST_COMMAND &&
                  command >= 0);
    }
  return FALSE;
}

static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  GeglTileBackendFile *self = GEGL_TILE_BACKEND_FILE (object);

  switch (property_id)
    {
      case PROP_PATH:
        if (self->path)
          g_free (self->path);
        self->path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       property_id,
              GValue     *value,
              GParamSpec *pspec)
{
  GeglTileBackendFile *self = GEGL_TILE_BACKEND_FILE (object);

  switch (property_id)
    {
      case PROP_PATH:
        g_value_set_string (value, self->path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
  GeglTileBackendFile *self = (GeglTileBackendFile *) object;


  if (self->index)
    g_hash_table_unref (self->index);
  if (self->exist)
    {
      GEGL_NOTE (TILE_BACKEND, "finalizing buffer %s", self->path);

      if (self->i)
        g_object_unref (self->i);
      if (self->o)
        g_object_unref (self->o);

      if (self->file)
        {
          g_file_delete  (self->file, NULL, NULL);
          g_object_unref (self->file);
        }
    }

  if (self->path)
    g_free (self->path);


  (*G_OBJECT_CLASS (parent_class)->finalize)(object);
}

static guint
hashfunc (gconstpointer key)
{
  const GeglBufferTile *e = key;
  guint            hash;
  gint             i;
  gint             srcA = e->x;
  gint             srcB = e->y;
  gint             srcC = e->z;

  /* interleave the 10 least significant bits of all coordinates,
   * this gives us Z-order / morton order of the space and should
   * work well as a hash
   */
  hash = 0;
  for (i = 9; i >= 0; i--)
    {
#define ADD_BIT(bit)    do { hash |= (((bit) != 0) ? 1 : 0); hash <<= 1; \
    } \
  while (0)
      ADD_BIT (srcA & (1 << i));
      ADD_BIT (srcB & (1 << i));
      ADD_BIT (srcC & (1 << i));
#undef ADD_BIT
    }
  return hash;
}

static gboolean
equalfunc (gconstpointer a,
           gconstpointer b)
{
  const GeglBufferTile *ea = a;
  const GeglBufferTile *eb = b;

  if (ea->x == eb->x &&
      ea->y == eb->y &&
      ea->z == eb->z)
    return TRUE;

  return FALSE;
}

static GObject *
gegl_tile_backend_file_constructor (GType                  type,
                                    guint                  n_params,
                                    GObjectConstructParam *params)
{
  GObject      *object;
  GeglTileBackendFile *self;
  GeglTileBackend *backend;

  object = G_OBJECT_CLASS (parent_class)->constructor (type, n_params, params);
  self   = GEGL_TILE_BACKEND_FILE (object);
  backend = GEGL_TILE_BACKEND (object);

  GEGL_NOTE (TILE_BACKEND, "constructing file backend: %s", self->path);
  self->file = g_file_new_for_commandline_arg (self->path);
  
  self->index = g_hash_table_new (hashfunc, equalfunc);

  /* if the file already exist we try to open it for appending instead of replacing */
  if (g_file_query_exists (self->file, NULL))
    {
      goffset offset;

#ifdef HACKED_GIO_WITH_READWRITE
      self->o = G_OUTPUT_STREAM (g_file_append_to (self->file, G_FILE_CREATE_READWRITE, NULL, NULL));
      self->i = g_object_get_data (G_OBJECT (self->o), "istream");
#else
      /* don't know how to deal with this properly with normal GIO */
      self->i = G_INPUT_STREAM (g_file_read (self->file, NULL, NULL));
#endif
      /*self->i = G_INPUT_STREAM (g_file_read (self->file, NULL, NULL));*/
      self->header = gegl_buffer_read_header (self->i, &offset)->header;
      backend->tile_width = self->header.tile_width;
      backend->tile_height = self->header.tile_height;
      backend->format = babl_format (self->header.description);
      /* we are overriding all of the work of the actual constructor here */
      backend->px_size = backend->format->format.bytes_per_pixel;
      backend->tile_size = backend->tile_width * backend->tile_height * backend->px_size;

      offset = self->header.next;
      self->tiles = gegl_buffer_read_index (self->i, &offset);

      /* insert each of the entries into the hash table */
      {
        /* compute total from and next pre alloc by monitoring tiles as they
         * are added here
         */
        goffset max=0;
        GList *iter;
        for (iter = self->tiles; iter; iter=iter->next)
          {
            GeglBufferItem *item = iter->data;
            if (item->tile.offset > max)
              max = item->tile.offset + backend->tile_size;
            g_hash_table_insert (self->index, iter->data, iter->data);
          }
        g_list_free (self->tiles);
        self->next_pre_alloc = max;
        self->total          = max;
        self->tiles = NULL;

      }
      self->exist = TRUE;
      g_assert (self->i);
      g_assert (self->o);
    }
  else
    {
      self->exist = FALSE; /* this is also the default, the file will be created on demand */
    }

  g_assert (self->file);


  backend->header = &self->header;

  return object;
}

static void ensure_exist (GeglTileBackendFile *self)
{
  if (!self->exist)
    {
      GeglTileBackend *backend;

      self->exist = TRUE;
      backend = GEGL_TILE_BACKEND (self);

      GEGL_NOTE (TILE_BACKEND, "creating swapfile  %s", self->path);
#ifdef HACKED_GIO_WITH_READWRITE

      self->o = G_OUTPUT_STREAM (g_file_append_to (self->file, G_FILE_CREATE_READWRITE, NULL, NULL));
      gegl_buffer_header_init (&self->header,
                               backend->tile_width,
                               backend->tile_height,
                               backend->px_size,
                               backend->format
                             );
      write_header (self);
      g_output_stream_flush (self->o, NULL, NULL);
      self->i = g_object_get_data (G_OBJECT (self->o), "istream");
#else
      self->o = G_OUTPUT_STREAM (g_file_replace (self->file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL));
      g_output_stream_flush (self->o, NULL, NULL);

      self->next_pre_alloc = 256;  /* reserved space for header */
      self->total          = 256;  /* reserved space for header */
      g_assert(g_seekable_seek (G_SEEKABLE (self->o), 256, G_SEEK_SET, NULL, NULL));

      gegl_buffer_header_init (&self->header,
                               backend->tile_width,
                               backend->tile_height,
                               backend->px_size,
                               backend->format
                               );
      write_header (self);
      g_output_stream_flush (self->o, NULL, NULL);
      self->i = G_INPUT_STREAM (g_file_read (self->file, NULL, NULL));
#endif
      /*self->i = G_INPUT_STREAM (g_file_read (self->file, NULL, NULL));*/
      self->next_pre_alloc = 256;  /* reserved space for header */
      self->total          = 256;  /* reserved space for header */
      g_assert (self->i);
      g_assert (self->o);
    }
}

static void
gegl_tile_backend_file_class_init (GeglTileBackendFileClass *klass)
{
  GObjectClass    *gobject_class     = G_OBJECT_CLASS (klass);
  GeglTileSourceClass *gegl_tile_source_class = GEGL_TILE_SOURCE_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = get_property;
  gobject_class->set_property = set_property;
  gobject_class->constructor  = gegl_tile_backend_file_constructor;
  gobject_class->finalize     = finalize;

  gegl_tile_source_class->command  = command;


  g_object_class_install_property (gobject_class, PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "path",
                                                        "The base path for this backing file for a buffer",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));
}

static void
gegl_tile_backend_file_init (GeglTileBackendFile *self)
{
  self->path           = NULL;
  self->file           = NULL;
  self->i              = NULL;
  self->o              = NULL;
  self->index          = NULL;
  self->free_list      = NULL;
  self->next_pre_alloc = 256;  /* reserved space for header */
  self->total          = 256;  /* reserved space for header */
}
