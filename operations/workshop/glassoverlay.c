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
 * 2024 Beaver Glass Overlay - based on a 2023 plugin "glass over text"
 */

/*
GEGL Graph to recreate below

color-overlay  value=#ffffff
gaussian-blur std-dev-x=1 std-dev-y=1
emboss azimuth=44 depth=29 
id=1
dst-out aux=[ ref=1 dst-over aux=[ color value=#000000  ]  crop  color-to-alpha opacity-threshold=0.14 color-overlay value=#000000 ]  ]
color-overlay value=#ffffff
gaussian-blur std-dev-x=0 std-dev-y=0
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES


#define GLASSTEXT \
" id=1 dst-out aux=[ ref=1 dst-over aux=[ color value=#000000  ]  crop  color-to-alpha opacity-threshold=0.14 color-overlay value=#000000 ]  ]  "\

property_double (azimuth, _("Azimuth"), 30.0)
    description (_("Light angle of the shine"))
    value_range (0, 360)
    ui_meta ("unit", "degree")
    ui_meta ("direction", "ccw")
  ui_steps      (0.5, 0.50)


property_int (depth, _("Depth"), 20.0)
    description (_("An internal emboss depth controls the shine"))
    value_range (10, 100)

property_double (elevation, _("Elevation"), 45.0)
    description (_("Elevation angle of the shine"))
    value_range (40, 46)
    ui_meta ("unit", "degree")
  ui_steps      (0.1, 0.50)

property_double (retract, _("Retract shine"), 3.0)
   description (_("Retract the shine"))
   value_range (1, 3)
   ui_range    (1, 3)
   ui_gamma    (3.0)
   ui_meta     ("unit", "pixel-distance")
  ui_steps      (0.1, 0.50)

property_double (blur, _("Faint blur on Shine"), 0.5)
   description (_("Apply a faint blur on the shine"))
   value_range (0.5, 1)
   ui_range    (0.5, 1)
   ui_gamma    (3.0)
  ui_steps      (0.1, 0.50)
   ui_meta     ("unit", "pixel-distance")

property_color (color, _("Color of shine"), "#ffffff")
    description (_("The color to paint over the input. White is recommended"))

property_double (opacity, _("Hyper Opacity"), 1.0)
    description (_("Opacity meter that goes above 100% to make the shine more intense"))
    value_range (0.3, 1.5)
    ui_range    (1.0, 1.5)
  ui_steps      (0.1, 0.50)

#else

#define GEGL_OP_META
#define GEGL_OP_NAME     glassoverlay
#define GEGL_OP_C_SOURCE glassoverlay.c

#include "gegl-op.h"

static void attach (GeglOperation *operation) 
{
  GeglNode *gegl = operation->node; 
  GeglNode *input, *output, *emboss, *color, *hiddencolor, *retract, *gaussian, *hyperopacity, *string;
  GeglColor *white_color = gegl_color_new ("rgb(1.1,1.1,1.1)");

  input    = gegl_node_get_input_proxy (gegl, "input");
  output   = gegl_node_get_output_proxy (gegl, "output");


  emboss = gegl_node_new_child (gegl,
                                  "operation", "gegl:emboss",
                                  NULL);

  string = gegl_node_new_child (gegl,
                                  "operation", "gegl:gegl", "string", GLASSTEXT,
                                  NULL);

  hyperopacity = gegl_node_new_child (gegl,
                                  "operation", "gegl:opacity",
                                  NULL);

  retract    = gegl_node_new_child (gegl,
                                  "operation", "gegl:gaussian-blur",
                                  NULL);

  gaussian    = gegl_node_new_child (gegl,
                                  "operation", "gegl:gaussian-blur",
                                  NULL);


  color    = gegl_node_new_child (gegl,
                                  "operation", "gegl:color-overlay",
                                  NULL);

/*hidden color needed so glass overlay works*/
  hiddencolor     = gegl_node_new_child (gegl, "operation", "gegl:color-overlay",
                                   "value", white_color,
                                  NULL);

  gegl_operation_meta_redirect (operation, "retract", retract, "std-dev-x");
  gegl_operation_meta_redirect (operation, "retract", retract, "std-dev-y");
  gegl_operation_meta_redirect (operation, "blur", gaussian, "std-dev-x");
  gegl_operation_meta_redirect (operation, "blur", gaussian, "std-dev-y");
  gegl_operation_meta_redirect (operation, "azimuth", emboss, "azimuth");
  gegl_operation_meta_redirect (operation, "elevation", emboss, "elevation");
  gegl_operation_meta_redirect (operation, "depth", emboss, "depth");
  gegl_operation_meta_redirect (operation, "color", color, "value");
  gegl_operation_meta_redirect (operation, "opacity", hyperopacity, "value");

  gegl_node_link_many (input, hiddencolor, retract, emboss, string, color, hyperopacity, gaussian, output, NULL);

}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass *operation_class;
  operation_class = GEGL_OPERATION_CLASS (klass);
  operation_class->attach = attach;
  gegl_operation_class_set_keys (operation_class,
    "name",        "gegl:glass-overlay",
    "title",       _("Glass Overlay"),
    "reference-hash", "otg2ap25pst2540sg01bmm31c",
    "description", _("Glass Overlay - Use with GIMP's blending options on alpha defined shapes"),
    "gimp:menu-path", "<Image>/Filters/Light and Shadow",
    "gimp:menu-label", _("Glass Overlay..."),
    NULL);
}

#endif