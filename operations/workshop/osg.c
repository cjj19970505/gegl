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
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 * 2024 Sam Lester (GEGL OSG (outline, shadow, glow) ) based on a 2023 plugin called SSG (stroke shadow glow)
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES
/*
June 25 2023 - Recreation of osg's GEGL Graph. If you feed this info to Gimp's
GEGL Graph it will allow to to text the plugin without installing it.

id=1
opacity value=1.5
median-blur alpha-percentile=0 radius=1 
gaussian-blur std-dev-x=1 std-dev-y=1
dropshadow x=0 y=0 radius=0 grow-radius=9 opacity=2
dst-out aux=[ ref=1 ]
color-overlay value=#ffffff
id=2 src-atop aux=[ ref=2 layer gaussian-blur std-dev-x=0 std-dev-y=0 ]
 */

/* Should correspond to GeglMedianBlurNeighborhood in median-blur.c */
enum_start (gegl_outline_base_shape)
  enum_value (GEGL_outline_GROW_SHAPE_SQUARE,  "square",  N_("Square"))
  enum_value (GEGL_outline_GROW_SHAPE_CIRCLE,  "circle",  N_("Circle"))
  enum_value (GEGL_outline_GROW_SHAPE_DIAMOND, "diamond", N_("Diamond"))
enum_end (GeglOutlineBaseShape)

property_enum   (grow_shape, _("Grow shape"),
                 GeglOutlineBaseShape, gegl_outline_base_shape,
                 GEGL_outline_GROW_SHAPE_CIRCLE)
  description   (_("The shape to expand or contract the OSG in"))

property_color (color, _("Color"), "#ffffff")

property_double (x, _("Horizontal position"), 0.0)
  description   (_("Horizontal OSG offset"))
  ui_range      (-40, 40)
  ui_steps      (1, 5)
  ui_meta       ("unit", "pixel-distance")
  ui_meta       ("axis", "x")

property_double (y, _("Vertical position"), 0.0)
  description   (_("Vertical OSG offset"))
  ui_range      (-40, 40)
  ui_steps      (1, 5)
  ui_meta       ("unit", "pixel-distance")
  ui_meta       ("axis", "y")

property_double (blur_outline, _("Blur radius"), 0.5)
  description   (_("Blurring an outline will make a shadow/glow effect"))
  value_range   (0.0, G_MAXDOUBLE)
  ui_range      (0.0, 100.0)
  ui_steps      (1, 5)
  ui_gamma      (1.5)
  ui_meta       ("unit", "pixel-distance")

property_double (grow_outline, _("Grow radius"), 9.0)
  value_range   (0, 300.0)
  ui_range      (0, 50.0)
  ui_digits     (0)
  ui_steps      (1, 5)
  ui_gamma      (1.5)
  ui_meta       ("unit", "pixel-distance")
  description (_("The distance to expand the OSG before blurring; a negative value will contract the shadow instead"))

property_double (opacity, _("Opacity"), 2)
   description  (_("Opacity of OSG"))
  value_range   (0.0, 2.0)
  ui_steps      (0.0, 2.0)

property_file_path(image, _("Image file upload"), "")
    description (_("Add an image file overlay. Allows (png, jpg, raw, svg, bmp, tif, ...)"))

property_double (blur_image, _("Blur image file upload"), 0.0)
   description  (_("A light blur to smooth the image file overlay. For use in cases where it may be applied as a multi-colored shadow glow"))
  value_range (0, 40.0)
  ui_range (0, 40.0)
  ui_gamma (1.5)

#else

#define GEGL_OP_META
#define GEGL_OP_NAME     osg
#define GEGL_OP_C_SOURCE osg.c

#include "gegl-op.h"

static void attach (GeglOperation *operation)
{
  GeglNode *gegl = operation->node;
  GeglNode *input, *output, *median, *blur, *id1, *hopacity, *shadow, *knockout, *color, *atop, *image, *blurimage, *opacity;
  GeglColor *osg_hidden_color = gegl_color_new ("#000000");


  input    = gegl_node_get_input_proxy (gegl, "input");
  output   = gegl_node_get_output_proxy (gegl, "output");

  image   = gegl_node_new_child (gegl,
                                  "operation", "gegl:layer",
                                  NULL);
  color   = gegl_node_new_child (gegl,
                                  "operation", "gegl:color-overlay",
                                  NULL);
  atop   = gegl_node_new_child (gegl,
                                  "operation", "gegl:src-atop",
                                  NULL);
  opacity   = gegl_node_new_child (gegl,
                                  "operation", "gegl:opacity",  "value", 2.0,
                                  NULL);
  median   = gegl_node_new_child (gegl,
                                  "operation", "gegl:median-blur", "alpha-percentile", 0.0, "radius", 1,
                                  NULL);
  shadow    = gegl_node_new_child (gegl,
                                  "operation", "gegl:dropshadow",
                                   "color", osg_hidden_color, NULL);
  blur    = gegl_node_new_child (gegl,
                                  "operation", "gegl:gaussian-blur", "std-dev-x", 0.5, "std-dev-y", 0.5,
                                  NULL);
  blurimage    = gegl_node_new_child (gegl,
                                  "operation", "gegl:gaussian-blur",
                                  NULL);
  id1    = gegl_node_new_child (gegl,
                                  "operation", "gegl:nop",
                                  NULL);
  hopacity    = gegl_node_new_child (gegl,
                                  "operation", "gegl:opacity", "value", 1.5,
                                  NULL);
  knockout = gegl_node_new_child (gegl,
                              "operation", "gegl:dst-out",  NULL);

  gegl_operation_meta_redirect (operation, "color", color, "value");
  gegl_operation_meta_redirect (operation, "opacity", opacity, "value");
  gegl_operation_meta_redirect (operation, "grow_outline", shadow, "grow-radius");
  gegl_operation_meta_redirect (operation, "blur_outline", shadow, "radius");
  gegl_operation_meta_redirect (operation, "x", shadow, "x");
  gegl_operation_meta_redirect (operation, "y", shadow, "y");
  gegl_operation_meta_redirect (operation, "grow_shape", shadow, "grow-shape");
  gegl_operation_meta_redirect (operation, "image", image, "src");
  gegl_operation_meta_redirect (operation, "blur_image", blurimage, "std-dev-x");
  gegl_operation_meta_redirect (operation, "blur_image", blurimage, "std-dev-y");

  gegl_node_link_many (input,  id1, hopacity, median, blur, shadow, knockout, color, atop, opacity, output, NULL);
  gegl_node_link_many (image, blurimage, NULL);
  gegl_node_connect (knockout, "aux", id1, "output");
  gegl_node_connect (atop, "aux", blurimage, "output");

}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass *operation_class;
  operation_class = GEGL_OPERATION_CLASS (klass);
  operation_class->attach = attach;
  gegl_operation_class_set_keys (operation_class,
    "name",        "gegl:osg",
    "title",       _("Outline Shadow Glow"),
    "reference-hash", "badopstsg00x03vv512ac",
    "description", _("A outline, shadow or glow effect that disregards the original image.  "
                     ""),
    "gimp:menu-path", "<Image>/Filters/Light and Shadow",
    "gimp:menu-label", _("Outline Shadow Glow (osg)..."),
    NULL);
}

#endif