/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright 2010 Michael Muré <batolettre@gmail.com>
 *
 */

#define GEGL_ITERATOR2_API
#ifdef GEGL_PROPERTIES

property_enum (sampler_type, _("Resampling method"),
    GeglSamplerType, gegl_sampler_type,
    GEGL_SAMPLER_CUBIC)

property_enum (abyss_policy, _("Abyss policy"),
               GeglAbyssPolicy, gegl_abyss_policy,
               GEGL_ABYSS_NONE)

#else

#define GEGL_OP_COMPOSER
#define GEGL_OP_NAME     map_absolute
#define GEGL_OP_C_SOURCE map-absolute.c

#include "config.h"
#include <glib/gi18n-lib.h>
#include "gegl-op.h"


static void
prepare (GeglOperation *operation)
{
  const Babl *space = gegl_operation_get_source_space (operation, "input");
  const Babl *format = babl_format_with_space ("RGBA float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "aux", babl_format_n (babl_type ("float"), 2));
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *region)
{
  if (! strcmp (input_pad, "input"))
    return *gegl_operation_source_get_bounding_box (operation, "input");
  else
    return *region;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *aux,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties       *o = GEGL_PROPERTIES (operation);
  const Babl           *format_io, *format_coords;
  GeglSampler          *sampler;
  GeglBufferIterator   *it;
  gint                  index_in, index_out, index_coords;

  format_io = gegl_operation_get_format (operation,"output");
  format_coords = babl_format_n (babl_type ("float"), 2);

  sampler = gegl_buffer_sampler_new_at_level (input, format_io, o->sampler_type,
                                              level);

  if (aux != NULL)
    {
      it = gegl_buffer_iterator_new (output, result, level, format_io,
                                     GEGL_ACCESS_WRITE, GEGL_ABYSS_NONE, 3);
      index_out = 0;

      index_coords = gegl_buffer_iterator_add (it, aux, result, level, format_coords,
                                               GEGL_ACCESS_READ, GEGL_ABYSS_NONE);
      index_in = gegl_buffer_iterator_add (it, input, result, level, format_io,
                                           GEGL_ACCESS_READ, o->abyss_policy);

      while (gegl_buffer_iterator_next (it))
        {
          gint        w;
          gint        h;
          gfloat      x;
          gfloat      y;
          gfloat     *in = it->items[index_in].data;
          gfloat     *out = it->items[index_out].data;
          gfloat     *coords = it->items[index_coords].data;
          GeglRectangle *roi = &it->items[0].roi;

          y = roi->y + 0.5; /* initial y coordinate */

          for (h = roi->height; h; h--, y++)
            {
              x = roi->x + 0.5; /* initial x coordinate */

              for (w = roi->width; w; w--, x++)
                {
                  /* if the coordinate asked is an exact pixel, we fetch it
                   * directly, to avoid the blur of sampling */
                  if (coords[0] == x && coords[1] == y)
                    {
                      out[0] = in[0];
                      out[1] = in[1];
                      out[2] = in[2];
                      out[3] = in[3];
                    }
                  else
                    {
                      gegl_sampler_get (sampler, coords[0],
                                                 coords[1],
                                                 NULL, out,
                                                 o->abyss_policy);
                    }

                  coords += 2;
                  in += 4;
                  out += 4;
                }
            }
        }
    }
  else
    {
      gegl_buffer_copy (input, result, o->abyss_policy,
                        output, result);
    }

  g_object_unref (sampler);

  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass         *operation_class;
  GeglOperationComposerClass *composer_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  composer_class  = GEGL_OPERATION_COMPOSER_CLASS (klass);

  composer_class->process = process;
  operation_class->prepare = prepare;
  operation_class->get_required_for_output = get_required_for_output;

  gegl_operation_class_set_keys (operation_class,
    "name",              "gegl:map-absolute",
    "title",              _("Map Absolute"),
    "categories",        "map",
    "position-dependent", "true",
    "description", _("sample input with an auxiliary buffer that contain absolute source coordinates"),
    NULL);
}
#endif
