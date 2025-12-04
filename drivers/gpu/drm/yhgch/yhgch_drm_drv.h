/* SPDX-License-Identifier: GPL-2.0 */

#ifndef YHGCH_DRM_DRV_H
#define YHGCH_DRM_DRV_H

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo_driver.h>

struct yhgch_framebuffer {
	struct drm_framebuffer fb;
	struct drm_gem_object *obj;
};

struct yhgch_fbdev {
	struct drm_fb_helper helper;
	struct yhgch_framebuffer *fb;
	int size;
};

struct yhgch_cursor {
	struct yhgch_bo *cursor_1;
	struct yhgch_bo *cursor_2;
	struct yhgch_bo *cursor_current;
};

struct yhgch_drm_private {
	/* hw */
	void __iomem *mmio;
	void __iomem *fb_map;
	unsigned long fb_base;
	unsigned long fb_size;

	/* drm */
	struct drm_device *dev;
	bool mode_config_initialized;

	/* ttm */
	struct drm_global_reference mem_global_ref;
	struct ttm_bo_global_ref bo_global_ref;
	struct ttm_bo_device bdev;
	bool initialized;

	/* fbdev */
	struct yhgch_fbdev *fbdev;
	bool mm_inited;

	/* hw cursor */
	struct yhgch_cursor cursor;
};

#define to_yhgch_framebuffer(x) container_of(x, struct yhgch_framebuffer, fb)

struct yhgch_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	struct ttm_bo_kmap_obj kmap;
	struct drm_gem_object gem;
	struct ttm_place placements[3];
	int pin_count;
};

static inline struct yhgch_bo *yhgch_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct yhgch_bo, bo);
}

static inline struct yhgch_bo *gem_to_yhgch_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct yhgch_bo, gem);
}

void yhgch_set_power_mode(struct yhgch_drm_private *priv,
			   unsigned int power_mode);
void yhgch_set_current_gate(struct yhgch_drm_private *priv,
			     unsigned int gate);
int yhgch_load(struct drm_device *dev, unsigned long flags);
void yhgch_unload(struct drm_device *dev);

int yhgch_de_init(struct yhgch_drm_private *priv);
int yhgch_vdac_init(struct yhgch_drm_private *priv);
int yhgch_fbdev_init(struct yhgch_drm_private *priv);
void yhgch_fbdev_fini(struct yhgch_drm_private *priv);

int yhgch_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		      struct drm_gem_object **obj);
struct yhgch_framebuffer *yhgch_framebuffer_init(struct drm_device *dev,
						   const struct drm_mode_fb_cmd2
						   *mode_cmd,
						   struct drm_gem_object *obj);

int yhgch_mm_init(struct yhgch_drm_private *yhgch);
void yhgch_mm_fini(struct yhgch_drm_private *yhgch);
int yhgch_bo_pin(struct yhgch_bo *bo, u32 pl_flag, u64 *gpu_addr);
int yhgch_bo_unpin(struct yhgch_bo *bo);
void yhgch_gem_free_object(struct drm_gem_object *obj);
int yhgch_bo_create(struct drm_device *dev, int size, int align, u32 flags,
		     struct yhgch_bo **phibmcbo);
int yhgch_dumb_create(struct drm_file *file, struct drm_device *dev,
		       struct drm_mode_create_dumb *args);
int yhgch_dumb_mmap_offset(struct drm_file *file, struct drm_device *dev,
			    u32 handle, u64 *offset);
int yhgch_mmap(struct file *filp, struct vm_area_struct *vma);

extern const struct drm_mode_config_funcs yhgch_mode_funcs;

#endif
