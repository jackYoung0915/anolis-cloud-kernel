// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/virtio.h>
#include <linux/virtio_blk.h>

/* ext feature bit definition */
#define VIRTIO_BLK_EXT_F_RING_PAIR	(1U << 0)
#define VIRTIO_BLK_EXT_F_RING_NO_ALIGN	(1U << 1)
#define VIRTIO_BLK_EXT_F_INVAL		(-1)

#ifdef CONFIG_VIRTIO_BLK_RING_PAIR
void virtblk_set_ext_feature(void __iomem *ioaddr, u32 guest_ext_features);

int virtblk_get_ext_feature(void __iomem *ioaddr, u32 *host_features);

int virtblk_get_ext_feature_bar(struct virtio_device *vdev, u32 *bar_offset);
#endif
