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
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2013 Carlos Zubieta <czubieta.dev@gmail.com>
 */

static const char* red_eye_removal_cl_source =
"#define RED_FACTOR    0.5133333                                               \n"
"#define GREEN_FACTOR  1                                                       \n"
"#define BLUE_FACTOR   0.1933333                                               \n"
"                                                                              \n"
"__kernel void cl_red_eye_removal(__global const float4 *in,                   \n"
"                                 __global       float4 *out,                  \n"
"                                                float threshold)              \n"
"{                                                                             \n"
"  int gid     = get_global_id(0);                                             \n"
"  float4 in_v = in[gid];                                                      \n"
"  float adjusted_red       = in_v.x * RED_FACTOR;                             \n"
"  float adjusted_green     = in_v.y * GREEN_FACTOR;                           \n"
"  float adjusted_blue      = in_v.z * BLUE_FACTOR;                            \n"
"  float adjusted_threshold = (threshold - 0.4) * 2;                           \n"
"  float tmp;                                                                  \n"
"                                                                              \n"
"  if (adjusted_red >= adjusted_green - adjusted_threshold &&                  \n"
"      adjusted_red >= adjusted_blue  - adjusted_threshold)                    \n"
"    {                                                                         \n"
"      tmp = (adjusted_green + adjusted_blue) / (2.0 * RED_FACTOR);            \n"
"      in_v.x = clamp(tmp, 0.0, 1.0);                                          \n"
"    }                                                                         \n"
"  out[gid]  = in_v;                                                           \n"
"}                                                                             \n"
;
