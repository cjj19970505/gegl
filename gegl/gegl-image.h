#ifndef __GEGL_IMAGE_H__
#define __GEGL_IMAGE_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gegl-op.h"

#ifndef __TYPEDEF_GEGL_COLOR_MODEL__
#define __TYPEDEF_GEGL_COLOR_MODEL__
typedef struct _GeglColorModel  GeglColorModel;
#endif

#ifndef __TYPEDEF_GEGL_TILE__
#define __TYPEDEF_GEGL_TILE__
typedef struct _GeglTile  GeglTile;
#endif

#define GEGL_TYPE_IMAGE               (gegl_image_get_type ())
#define GEGL_IMAGE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GEGL_TYPE_IMAGE, GeglImage))
#define GEGL_IMAGE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass),  GEGL_TYPE_IMAGE, GeglImageClass))
#define GEGL_IS_IMAGE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GEGL_TYPE_IMAGE))
#define GEGL_IS_IMAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass),  GEGL_TYPE_IMAGE))
#define GEGL_IMAGE_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj),  GEGL_TYPE_IMAGE, GeglImageClass))

#ifndef __TYPEDEF_GEGL_IMAGE__
#define __TYPEDEF_GEGL_IMAGE__
typedef struct _GeglImage GeglImage;
#endif
struct _GeglImage 
{
   GeglOp __parent__;

   /*< private >*/

   GeglColorModel * color_model;
   GeglTile * tile;

   GeglColorModel * derived_color_model;
};

typedef struct _GeglImageClass GeglImageClass;
struct _GeglImageClass 
{
   GeglOpClass __parent__;
};

GType gegl_image_get_type                     (void);

GeglColorModel*  gegl_image_color_model                  (GeglImage * self);
void             gegl_image_set_color_model              (GeglImage * self, 
                                                          GeglColorModel * cm);


void             gegl_image_set_derived_color_model      (GeglImage * self, 
                                                          GeglColorModel * cm);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
