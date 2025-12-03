// SPDX-License-Identifier: GPL-2.0

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/hashtable.h>
#include <linux/glob.h>
#include <linux/proc_mnt_mask.h>

#include "mount.h"

#define MAX_MNT_MSK_COUNT (128)
#define MNT_MASK_BUFFER_SIZE 256

struct mnt_mask_list {
	struct list_head list;
	unsigned int mnt_mask_count;
	rwlock_t list_lock;
};

static struct mnt_mask_list *root_mask;
static DEFINE_MUTEX(root_mask_lock);
static inline struct mnt_mask_list *find_mnt_mask_list(void)
{
	if (!root_mask) {
		mutex_lock(&root_mask_lock);
		if (!root_mask) {
			mutex_unlock(&root_mask_lock);
			return NULL;
		}
		mutex_unlock(&root_mask_lock);
	}

	return root_mask->mnt_mask_count ? root_mask : NULL;
}

static inline struct mnt_mask_list *get_mnt_mask_list(void)
{
	if (root_mask) {
		return root_mask;
	} else {
		mutex_lock(&root_mask_lock);
		if (!root_mask) {
			root_mask = kzalloc(sizeof(struct mnt_mask_list), GFP_KERNEL);
			if (!root_mask)
				goto unlock;
			INIT_LIST_HEAD(&root_mask->list);
			rwlock_init(&root_mask->list_lock);
		}
	unlock:
		mutex_unlock(&root_mask_lock);
		return root_mask;
	}
}

struct mnt_msk *mnt_mask(const char *dev_path, const char *mount_path)
{
	struct mnt_msk *m, *n = NULL;
	struct mnt_mask_list *mask_list;

	mask_list = find_mnt_mask_list();
	if (!mask_list)
		return NULL;

	read_lock(&mask_list->list_lock);
	list_for_each_entry(m, &mask_list->list, list) {
		if ((!strcmp(m->target, dev_path) &&
			 glob_match(m->mnt_path, mount_path)) ||
			(!strcmp(m->target, "mountoption") &&
			 !strcmp(m->mnt_path, mount_path))) {
			n = m;
			break;
		}
	}
	read_unlock(&mask_list->list_lock);
	return n;
}

#ifdef CONFIG_RICH_CONTAINER
#ifndef CONFIG_RICH_CONTAINER_CG_SWITCH
static int set_mnt_mask(struct mnt_msk *item)
{
	int ret = 0;
	int buf_size = 0;
	struct mnt_msk *m, *new;
	struct mnt_mask_list *mask_list;

	mask_list = get_mnt_mask_list();
	if (!mask_list)
		return -ENOMEM;

	write_lock(&mask_list->list_lock);
	list_for_each_entry(m, &mask_list->list, list) {
		buf_size += (strlen(m->target) +
			strlen(m->replace) + strlen(m->mnt_path) + 3);
		if (!strcmp(m->target, item->target) &&
			!strcmp(m->mnt_path, item->mnt_path)) {
			ret = -EEXIST;
			goto unlock;
		}
	}

	buf_size += (strlen(item->target) +
		strlen(item->replace) + strlen(item->mnt_path) + 3);

	if (buf_size > PAGE_SIZE || mask_list->mnt_mask_count >= MAX_MNT_MSK_COUNT) {
		ret = -EOVERFLOW;
		goto unlock;
	}

	new = kmalloc(sizeof(struct mnt_msk), GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto unlock;
	}

	memcpy(new, item, sizeof(struct mnt_msk));
	list_add_tail(&new->list, &mask_list->list);
	mask_list->mnt_mask_count++;
unlock:
	write_unlock(&mask_list->list_lock);
	return ret;
}

static int clear_mnt_mask(struct mnt_msk *item)
{
	bool clear = false;
	struct mnt_msk *m;
	struct mnt_mask_list *mask_list;

	mask_list = get_mnt_mask_list();
	if (!mask_list)
		return -ENOMEM;

	write_lock(&mask_list->list_lock);
	list_for_each_entry(m, &mask_list->list, list) {
		if (!strcmp(m->target, item->target) &&
			!strcmp(m->mnt_path, item->mnt_path)) {
			list_del(&m->list);
			clear = true;
			mask_list->mnt_mask_count--;
			goto unlock;
		}
	}
unlock:
	write_unlock(&mask_list->list_lock);
	if (clear)
		kfree(m);

	return 0;
}

static void str_escape(char *s, const char *esc)
{
	char *p = s;

	while (p && *p != '\0') {
		char c = *p++;

		while (c != '\0' && strchr(esc, c))
			c = *p++;

		*s++ = c;
	}
	*s = '\0';
}

static int mnt_mask_parse(char *buf, bool *is_set, struct mnt_msk *item)
{
	char *token;

	str_escape(buf, "\t\n");
	buf = skip_spaces(buf);
	token = strsep(&buf, " ");
	if (!token || !*token || !buf)
		goto error;

	if (!strcmp(token, "set")) {
		*is_set = true;
	} else if (!strcmp(token, "clear")) {
		*is_set = false;
	} else {
		pr_err("set parse error\n");
		goto error;
	}

	buf = skip_spaces(buf);
	token = strsep(&buf, " ");
	if (!buf || !token || !*token || strlen(token) > (MNT_MASK_PATH_MAX - 1)) {
		pr_err("target parse failed\n");
		goto error;
	}
	memcpy(item->target, token, strlen(token) + 1);

	buf = skip_spaces(buf);
	token = strsep(&buf, " ");
	if (!buf || !token || strlen(token) > (MNT_MASK_PATH_MAX - 1)) {
		pr_err("replace parse failed\n");
		goto error;
	}
	memcpy(item->replace, token, strlen(token) + 1);

	buf = strim(buf);
	if (!buf || !*buf) {
		pr_err("mountpoint is empty\n");
		goto error;
	}
	if (strlen(buf) >= MNT_MASK_PATH_MAX) {
		pr_err("mountpoint is too long\n");
		goto error;
	}
	memcpy(item->mnt_path, buf, strlen(buf) + 1);

	return 0;
error:
	pr_err("Failed to parse mount mask\n");
	return -EINVAL;
}

static int mnt_mask_read(struct ctl_table *table, int write,
					void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	struct mnt_msk *msk;
	struct mnt_mask_list *mask_list;
	struct ctl_table fake_table;
	char *kbuf = NULL;
	size_t offset = 0;

	kbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mask_list = find_mnt_mask_list();
	if (!mask_list) {
		offset = snprintf(kbuf, PAGE_SIZE, "empty mnt mask list\n");
	} else {
		read_lock(&mask_list->list_lock);
		list_for_each_entry(msk, &mask_list->list, list) {
			int n = snprintf(kbuf + offset, PAGE_SIZE - offset,
				"%s on %s\n", msk->target, msk->mnt_path);
			if (n < 0 || offset + n >= PAGE_SIZE) {
				read_unlock(&mask_list->list_lock);
				ret = -EOVERFLOW;
				goto out;
			}
			offset += n;
		}
		read_unlock(&mask_list->list_lock);
	}

	fake_table.data = kbuf;
	fake_table.maxlen = PAGE_SIZE;

	ret = proc_dostring(&fake_table, write, buffer, lenp, ppos);

out:
	kfree(kbuf);
	return ret;
}

static int mnt_mask_write(struct ctl_table *table, int write,
					void *buf, size_t *lenp,
					loff_t *ppos)
{
	int ret;
	bool is_set;
	char *buffer;
	struct mnt_msk item;
	struct ctl_table fake_table;

	if (!buf || (lenp && (*lenp > (MNT_MASK_BUF_MAX - 1))))
		return -EINVAL;

	buffer = kmalloc(MNT_MASK_BUF_MAX, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	fake_table.data = buffer;
	fake_table.maxlen = MNT_MASK_BUF_MAX;

	ret = proc_dostring(&fake_table, write, buf, lenp, ppos);
	if (ret)
		goto out;

	ret = mnt_mask_parse(buffer, &is_set, &item);
	if (ret)
		goto out;

	if (is_set)
		ret = set_mnt_mask(&item);
	else
		ret = clear_mnt_mask(&item);

out:
	kfree(buffer);
	return ret;
}

int rich_container_mountinfo_mask_handler(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	if (write)
		return mnt_mask_write(table, write, buffer, lenp, ppos);
	else
		return mnt_mask_read(table, write, buffer, lenp, ppos);
}
#endif
#endif
