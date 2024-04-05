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
 * Copyright 2006, 2010 Øyvind Kolås <pippin@gimp.org>
 * 2024 Sam Lester (random seed and knock out abilities like my SSG plugin.
 */

#include "config.h"
#include <glib/gi18n-lib.h>


#ifdef GEGL_PROPERTIES

enum_start (gegl_dropshadow_grow_shape)
  enum_value (GEGL_DROPSHADOW_GROW_SHAPE_SQUARE,  "square",  N_("Square"))
  enum_value (GEGL_DROPSHADOW_GROW_SHAPE_CIRCLE,  "circle",  N_("Circle"))
  enum_value (GEGL_DROPSHADOW_GROW_SHAPE_DIAMOND, "diamond", N_("Diamond"))
enum_end (GeglDropshadowGrowShape)

property_double (x, _("X"), 20.0)
  description   (_("Horizontal shadow offset"))
  ui_range      (-40.0, 40.0)
  ui_steps      (1, 10)
  ui_meta       ("unit", "pixel-distance")
  ui_meta       ("axis", "x")

property_double (y, _("Y"), 20.0)
  description   (_("Vertical shadow offset"))
  ui_range      (-40.0, 40.0)
  ui_steps      (1, 10)
  ui_meta       ("unit", "pixel-distance")
  ui_meta       ("axis", "y")

property_double (radius, _("Blur radius"), 10.0)
  value_range   (0.0, G_MAXDOUBLE)
  ui_range      (0.0, 300.0)
  ui_steps      (1, 5)
  ui_gamma      (1.5)
  ui_meta       ("unit", "pixel-distance")

property_enum   (grow_shape, _("Grow shape"),
                 GeglDropshadowGrowShape, gegl_dropshadow_grow_shape,
                 GEGL_DROPSHADOW_GROW_SHAPE_CIRCLE)
  description   (_("The shape to expand or contract the shadow in"))

property_double (grow_radius, _("Grow radius"), 0.0)
  value_range   (-100.0, 100.0)
  ui_range      (-50.0, 50.0)
  ui_digits     (0)
  ui_steps      (1, 5)
  ui_gamma      (1.5)
  ui_meta       ("unit", "pixel-distance")
  description (_("The distance to expand the shadow before blurring; a negative value will contract the shadow instead"))

property_color  (color, _("Color"), "black")
    /* TRANSLATORS: the string 'black' should not be translated */
  description   (_("The shadow's color (defaults to 'black')"))

/* It does make sense to sometimes have opacities > 1 (see GEGL logo
 * for example)
 */
property_double (opacity, _("Opacity"), 0.5)
  value_range   (0.0, 2.0)
  ui_steps      (0.01, 0.10)

property_boolean (enable_knockout, _("Enable Knockout"), FALSE)
    description (_("Remove the original content while keeping the shadow"))


#else

#define GEGL_OP_META
#define GEGL_OP_NAME     dropshadow
#define GEGL_OP_C_SOURCE dropshadow.c

#include "gegl-op.h"

typedef struct
{
  GeglNode *input;
  GeglNode *grow;
  GeglNode *darken;
  GeglNode *blur;
  GeglNode *opacity;
  GeglNode *color;
  GeglNode *translate;
  GeglNode *over;
  GeglNode *dstout;
  GeglNode *nothing;
  GeglNode *output;
} State;

static void
update_graph (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  State *state = o->user_data;
  if (!state) return;
  GeglNode *xgrow;
  GeglNode *xover;

  if (fabs (o->grow_radius) > 0.0001)
  xgrow = state->grow;
  else
  xgrow = state->nothing;

  if (o->enable_knockout) xover  = state->dstout;
  if (!o->enable_knockout) xover  = state->over;

  gegl_node_link_many (state->input, xgrow, state->darken,  state->blur,  state->opacity, state->translate, xover, state->output,
                       NULL);
  gegl_node_connect (xover, "aux", state->input, "output");
  gegl_node_connect (state->darken, "aux", state->color, "output");

}

/* in attach we hook into graph adding the needed nodes */
static void
attach (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  GeglNode  *gegl = operation->node;
  GeglNode  *input, *output, /* *over, *translate, *opacity, */ *grow, *blur, *darken, *color;
  GeglColor *black_color = gegl_color_new ("rgb(0.0,0.0,0.0)");

  input     = gegl_node_get_input_proxy (gegl, "input");
  output    = gegl_node_get_output_proxy (gegl, "output");
/*  over      = gegl_node_new_child (gegl, "operation", "gegl:over", NULL);
  translate = gegl_node_new_child (gegl, "operation", "gegl:translate", NULL);
  opacity   = gegl_node_new_child (gegl, "operation", "gegl:opacity", NULL);*/
  blur      = gegl_node_new_child (gegl, "operation", "gegl:gaussian-blur",
                                         "clip-extent", FALSE,
                                         "abyss-policy", 0,
                                         NULL);
  grow      = gegl_node_new_child (gegl, "operation", "gegl:median-blur",
                                         "percentile",       100.0,
                                         "alpha-percentile", 100.0,
                                         "abyss-policy",     GEGL_ABYSS_NONE,
                                         NULL);
  darken    = gegl_node_new_child (gegl, "operation", "gegl:src-in", NULL);
  color     = gegl_node_new_child (gegl, "operation", "gegl:color",
                                   "value", black_color,
                                   NULL);
  State *state = g_malloc0 (sizeof (State));
  o->user_data = state;
  state->grow = grow;
  state->darken = darken;
  state->blur = blur;
  state->color  = color;
  state->input     = input     = gegl_node_get_input_proxy (gegl, "input");
  state->output    = output    = gegl_node_get_output_proxy (gegl, "output");
  state->over  = gegl_node_new_child (gegl, "operation", "gegl:over", NULL);
  state->translate  = gegl_node_new_child (gegl, "operation", "gegl:translate", NULL);
  state->dstout  = gegl_node_new_child (gegl, "operation", "gegl:dst-out", NULL);
  state->nothing    = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);
  state->opacity    = gegl_node_new_child (gegl, "operation", "gegl:opacity", NULL);

  g_object_unref (black_color);

  gegl_node_link_many (input, grow, darken, blur, /*opacity,  translate, over,*/ output,
                       NULL);
/*  gegl_node_connect (over, "aux", input, "output"); */
  gegl_node_connect (darken, "aux", color, "output");

  gegl_operation_meta_redirect (operation, "grow-shape", grow, "neighborhood");
  gegl_operation_meta_redirect (operation, "grow-radius", grow, "radius");
  gegl_operation_meta_redirect (operation, "radius", blur, "std-dev-x");
  gegl_operation_meta_redirect (operation, "radius", blur, "std-dev-y");
  gegl_operation_meta_redirect (operation, "x", state->translate, "x");
  gegl_operation_meta_redirect (operation, "y", state->translate, "y");
  gegl_operation_meta_redirect (operation, "color", color, "value");
  gegl_operation_meta_redirect (operation, "opacity", state->opacity, "value");
}

static void
dispose (GObject *object)
{
   GeglProperties  *o = GEGL_PROPERTIES (object);
   g_clear_pointer (&o->user_data, g_free);
   G_OBJECT_CLASS (gegl_op_parent_class)->dispose (object);
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GObjectClass           *object_class;
  GeglOperationClass     *operation_class      = GEGL_OPERATION_CLASS (klass);
  GeglOperationMetaClass *operation_meta_class = GEGL_OPERATION_META_CLASS (klass);

  operation_class->attach      = attach;
  operation_meta_class->update = update_graph;

  object_class               = G_OBJECT_CLASS (klass);
  object_class->dispose      = dispose;

  gegl_operation_class_set_keys (operation_class,
    "name",        "gegl:dropshadow",
    "title",       _("Dropshadow"),
    "categories",  "light",
    "reference-hash", "1784365a0e801041189309f3a4866b1a",
    "description",
    _("Creates a dropshadow effect on the input buffer"),
    NULL);
}

#endif