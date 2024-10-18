// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * algif_hash: User-space interface for hash algorithms
 *
 * This file provides the user-space API for hash algorithms.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/hash.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

#define HASH_COPY_IOVEC_LEN     1
#define HASH_UPDATE_IOVEC_LEN   2

struct hash_alg_sgl {
	struct af_alg_sgl sgl;
	struct list_head list;
};

struct hash_ctx {
	struct af_alg_sgl sgl;

	struct hash_alg_sgl first_sgl;
	struct hash_alg_sgl *last_sgl;
	struct list_head tsgl_list;

	u8 *result;

	struct crypto_wait wait;

	unsigned int len;
	bool more;

	struct ahash_request req;
};

static int hash_alloc_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;

	if (ctx->result)
		return 0;

	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));

	ctx->result = sock_kmalloc(sk, ds, GFP_KERNEL);
	if (!ctx->result)
		return -ENOMEM;

	memset(ctx->result, 0, ds);

	return 0;
}

static void hash_free_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;

	if (!ctx->result)
		return;

	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));

	sock_kzfree_s(sk, ctx->result, ds);
	ctx->result = NULL;
}

#ifdef CONFIG_X86
static int hash_alg_get_tsgl(struct sock *sk, struct hash_ctx *ctx,
			     struct msghdr *msg, size_t size)
{
	struct hash_alg_sgl *tsgl;
	int err;

	while (size > 0) {
		if (list_empty(&ctx->tsgl_list)) {
			tsgl = &ctx->first_sgl;
		} else {
			tsgl = sock_kmalloc(sk, sizeof(*tsgl), GFP_KERNEL);
			if (!tsgl)
				return -ENOMEM;
		}

		list_add_tail(&tsgl->list, &ctx->tsgl_list);

		err = af_alg_make_sg(&tsgl->sgl, &msg->msg_iter, size);
		if (err < 0)
			return err;

		if (ctx->last_sgl)
			af_alg_sgl_link(&ctx->last_sgl->sgl, &tsgl->sgl);

		ctx->last_sgl = tsgl;
		size -= err;
		iov_iter_advance(&msg->msg_iter, err);
	}

	return 0;
}

static void hash_alg_put_tsgl(struct sock *sk, struct hash_ctx *ctx, int sg_err)
{
	struct hash_alg_sgl *tsgl, *tmp;

	list_for_each_entry_safe(tsgl, tmp, &ctx->tsgl_list, list) {
		if (!sg_err)
			af_alg_free_sg(&tsgl->sgl);

		list_del(&tsgl->list);
		if (tsgl != &ctx->first_sgl)
			sock_kfree_s(sk, tsgl, sizeof(*tsgl));
	}
	ctx->last_sgl = NULL;
}

static int hash_alg_cmsg_send(struct hash_ctx *ctx, struct msghdr *msg, int *set)
{
	struct af_alg_control con = {};
	unsigned int ds;
	int err;

	err = af_alg_ctrl_cmsg_send(msg, &con);
	if (err)
		return err;

	if (con.iv) {
		ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
		if (con.iv->ivlen != ds)
			return -EINVAL;
		*set = 1;
	}

	return 0;
}

static int hash_sendmsg_ex(struct socket *sock, struct msghdr *msg)
{
	int limit = ALG_MAX_PAGES * ALG_MAX_PAGES * PAGE_SIZE;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	char state[HASH_MAX_STATESIZE];
	unsigned statesize =
		crypto_ahash_statesize(crypto_ahash_reqtfm(&ctx->req));
	long copied = 0;
	int setiv = 0;
	int remain = 0;
	int err;

	lock_sock(sk);
	if (!ctx->more) {
		if ((msg->msg_flags & MSG_MORE))
			hash_free_result(sk, ctx);

		err = crypto_wait_req(crypto_ahash_init(&ctx->req), &ctx->wait);
		if (err)
			goto unlock;
	}

	err = hash_alg_cmsg_send(ctx, msg, &setiv);
	if (err)
		goto unlock;

	/* setiv == 1: need exchange context data with userspace */
	if (setiv) {
		/* msg->msg_iter.nr_segs == 1: copy context data from userspace
		 * msg->msg_iter.nr_segs == 2: transfer context data to userspace
		 */
		if (msg->msg_iter.nr_segs == HASH_COPY_IOVEC_LEN) {
			err = memcpy_from_msg((void *)state, msg, statesize);
			if (err)
				goto unlock;
			err = crypto_ahash_import(&ctx->req, state);
			if (err)
				goto unlock;
		} else if (msg->msg_iter.nr_segs == HASH_UPDATE_IOVEC_LEN) {
			remain = statesize;
		} else {
			err = -EINVAL;
			goto unlock;
		}
	}

	ctx->more = 0;

	while (msg_data_left(msg) - remain) {
		int len = msg_data_left(msg) - remain;

		if (len > limit)
			len = limit;

		err = hash_alg_get_tsgl(sk, ctx, msg, len);
		if (err < 0) {
			err = copied ? 0 : err;
			hash_alg_put_tsgl(sk, ctx, 1);
			goto unlock;
		}

		ahash_request_set_crypt(&ctx->req, ctx->first_sgl.sgl.sg, NULL, len);

		err = crypto_wait_req(crypto_ahash_update(&ctx->req),
				      &ctx->wait);
		if (err) {
			hash_alg_put_tsgl(sk, ctx, 0);
			goto unlock;
		}

		hash_alg_put_tsgl(sk, ctx, 0);
		copied += len;
	}

	if (remain) {
		err = crypto_ahash_export(&ctx->req, state);
		if (err)
			goto unlock;
		memcpy_to_msg(msg, (void *)state, statesize);
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more) {
		err = hash_alloc_result(sk, ctx);
		if (err)
			goto unlock;

		ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
	}

unlock:
	release_sock(sk);

	return err ?: copied;
}
#endif

static int hash_sendmsg(struct socket *sock, struct msghdr *msg,
			size_t ignored)
{
	int limit = ALG_MAX_PAGES * PAGE_SIZE;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	long copied = 0;
	int err;

#ifdef CONFIG_X86
	if (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		return hash_sendmsg_ex(sock, msg);
#endif

	if (limit > sk->sk_sndbuf)
		limit = sk->sk_sndbuf;

	lock_sock(sk);
	if (!ctx->more) {
		if ((msg->msg_flags & MSG_MORE))
			hash_free_result(sk, ctx);

		err = crypto_wait_req(crypto_ahash_init(&ctx->req), &ctx->wait);
		if (err)
			goto unlock;
	}

	ctx->more = false;

	while (msg_data_left(msg)) {
		int len = msg_data_left(msg);

		if (len > limit)
			len = limit;

		len = af_alg_make_sg(&ctx->sgl, &msg->msg_iter, len);
		if (len < 0) {
			err = copied ? 0 : len;
			goto unlock;
		}

		ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, NULL, len);

		err = crypto_wait_req(crypto_ahash_update(&ctx->req),
				      &ctx->wait);
		af_alg_free_sg(&ctx->sgl);
		if (err)
			goto unlock;

		copied += len;
		iov_iter_advance(&msg->msg_iter, len);
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more) {
		err = hash_alloc_result(sk, ctx);
		if (err)
			goto unlock;

		ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
	}

unlock:
	release_sock(sk);

	return err ?: copied;
}

static ssize_t hash_sendpage(struct socket *sock, struct page *page,
			     int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	int err;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	lock_sock(sk);
	sg_init_table(ctx->sgl.sg, 1);
	sg_set_page(ctx->sgl.sg, page, size, offset);

	if (!(flags & MSG_MORE)) {
		err = hash_alloc_result(sk, ctx);
		if (err)
			goto unlock;
	} else if (!ctx->more)
		hash_free_result(sk, ctx);

	ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, ctx->result, size);

	if (!(flags & MSG_MORE)) {
		if (ctx->more)
			err = crypto_ahash_finup(&ctx->req);
		else
			err = crypto_ahash_digest(&ctx->req);
	} else {
		if (!ctx->more) {
			err = crypto_ahash_init(&ctx->req);
			err = crypto_wait_req(err, &ctx->wait);
			if (err)
				goto unlock;
		}

		err = crypto_ahash_update(&ctx->req);
	}

	err = crypto_wait_req(err, &ctx->wait);
	if (err)
		goto unlock;

	ctx->more = flags & MSG_MORE;

unlock:
	release_sock(sk);

	return err ?: size;
}

static int hash_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	unsigned ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	bool result;
	int err;

	if (len > ds)
		len = ds;
	else if (len < ds)
		msg->msg_flags |= MSG_TRUNC;

	lock_sock(sk);
	result = ctx->result;
	err = hash_alloc_result(sk, ctx);
	if (err)
		goto unlock;

	ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);

	if (!result && !ctx->more) {
		err = crypto_wait_req(crypto_ahash_init(&ctx->req),
				      &ctx->wait);
		if (err)
			goto unlock;
	}

	if (!result || ctx->more) {
		ctx->more = false;
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
		if (err)
			goto unlock;
	}

	err = memcpy_to_msg(msg, ctx->result, len);

unlock:
	hash_free_result(sk, ctx);
	release_sock(sk);

	return err ?: len;
}

static int hash_accept(struct socket *sock, struct socket *newsock, int flags,
		       bool kern)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	struct ahash_request *req = &ctx->req;
	char state[HASH_MAX_STATESIZE];
	struct sock *sk2;
	struct alg_sock *ask2;
	struct hash_ctx *ctx2;
	bool more;
	int err;

	lock_sock(sk);
	more = ctx->more;
	err = more ? crypto_ahash_export(req, state) : 0;
	release_sock(sk);

	if (err)
		return err;

	err = af_alg_accept(ask->parent, newsock, kern);
	if (err)
		return err;

	sk2 = newsock->sk;
	ask2 = alg_sk(sk2);
	ctx2 = ask2->private;
	ctx2->more = more;

	if (!more)
		return err;

	err = crypto_ahash_import(&ctx2->req, state);
	if (err) {
		sock_orphan(sk2);
		sock_put(sk2);
	}

	return err;
}

static struct proto_ops algif_hash_ops = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,

	.release	=	af_alg_release,
	.sendmsg	=	hash_sendmsg,
	.sendpage	=	hash_sendpage,
	.recvmsg	=	hash_recvmsg,
	.accept		=	hash_accept,
};

static int hash_check_key(struct socket *sock)
{
	int err = 0;
	struct sock *psk;
	struct alg_sock *pask;
	struct crypto_ahash *tfm;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);

	lock_sock(sk);
	if (!atomic_read(&ask->nokey_refcnt))
		goto unlock_child;

	psk = ask->parent;
	pask = alg_sk(ask->parent);
	tfm = pask->private;

	err = -ENOKEY;
	lock_sock_nested(psk, SINGLE_DEPTH_NESTING);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		goto unlock;

	atomic_dec(&pask->nokey_refcnt);
	atomic_set(&ask->nokey_refcnt, 0);

	err = 0;

unlock:
	release_sock(psk);
unlock_child:
	release_sock(sk);

	return err;
}

static int hash_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
			      size_t size)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_sendmsg(sock, msg, size);
}

static ssize_t hash_sendpage_nokey(struct socket *sock, struct page *page,
				   int offset, size_t size, int flags)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_sendpage(sock, page, offset, size, flags);
}

static int hash_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
			      size_t ignored, int flags)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_recvmsg(sock, msg, ignored, flags);
}

static int hash_accept_nokey(struct socket *sock, struct socket *newsock,
			     int flags, bool kern)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_accept(sock, newsock, flags, kern);
}

static struct proto_ops algif_hash_ops_nokey = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,

	.release	=	af_alg_release,
	.sendmsg	=	hash_sendmsg_nokey,
	.sendpage	=	hash_sendpage_nokey,
	.recvmsg	=	hash_recvmsg_nokey,
	.accept		=	hash_accept_nokey,
};

static void *hash_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_ahash(name, type, mask);
}

static void hash_release(void *private)
{
	crypto_free_ahash(private);
}

static int hash_setkey(void *private, const u8 *key, unsigned int keylen)
{
	return crypto_ahash_setkey(private, key, keylen);
}

static void hash_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;

	hash_free_result(sk, ctx);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int hash_accept_parent_nokey(void *private, struct sock *sk)
{
	struct crypto_ahash *tfm = private;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx;
	unsigned int len = sizeof(*ctx) + crypto_ahash_reqsize(tfm);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->result = NULL;
	ctx->len = len;
	ctx->more = false;
	crypto_init_wait(&ctx->wait);
	ctx->last_sgl = NULL;
	INIT_LIST_HEAD(&ctx->tsgl_list);

	ask->private = ctx;

	ahash_request_set_tfm(&ctx->req, tfm);
	ahash_request_set_callback(&ctx->req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &ctx->wait);

	sk->sk_destruct = hash_sock_destruct;

	return 0;
}

static int hash_accept_parent(void *private, struct sock *sk)
{
	struct crypto_ahash *tfm = private;

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return hash_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_hash = {
	.bind		=	hash_bind,
	.release	=	hash_release,
	.setkey		=	hash_setkey,
	.accept		=	hash_accept_parent,
	.accept_nokey	=	hash_accept_parent_nokey,
	.ops		=	&algif_hash_ops,
	.ops_nokey	=	&algif_hash_ops_nokey,
	.name		=	"hash",
	.owner		=	THIS_MODULE
};

static int __init algif_hash_init(void)
{
	return af_alg_register_type(&algif_type_hash);
}

static void __exit algif_hash_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_hash);
	BUG_ON(err);
}

module_init(algif_hash_init);
module_exit(algif_hash_exit);
MODULE_LICENSE("GPL");
