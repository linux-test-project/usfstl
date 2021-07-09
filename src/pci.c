/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/socket.h>
#include <usfstl/pci.h>
#include <linux/virtio_pcidev.h>
#include <endian.h>
#include <stdio.h>

void usfstl_vhost_pci_connected(struct usfstl_vhost_user_dev *dev)
{
	struct usfstl_pci_device_ops *ops = dev->server->data;
	struct usfstl_pci_device *pcidev;

	pcidev = ops->connected();
	USFSTL_ASSERT(pcidev);
	dev->data = pcidev;
	pcidev->dev = dev;

	USFSTL_ASSERT(ops->cfg_space_read ||
		      ops->cfg_space_read_deferred ||
		      (pcidev->config_space && pcidev->config_space_size));
	USFSTL_ASSERT(ops->cfg_space_write ||
		      ops->cfg_space_write_deferred ||
		      (pcidev->config_space && pcidev->config_space_mask &&
		       pcidev->config_space_size));
}

void usfstl_vhost_pci_cfg_read(struct usfstl_pci_device_ops *ops,
			       struct usfstl_pci_device *pcidev,
			       struct virtio_pcidev_msg *msg,
			       struct usfstl_vhost_user_buf *buf)
{
	void *out = buf->in_sg[0].iov_base;

	if (!ops->cfg_space_read && !ops->cfg_space_read_deferred) {
		if (msg->addr + msg->size > pcidev->config_space_size) {
			memset(out, 0, msg->size);
			return;
		}

		memcpy(out, (uint8_t *)pcidev->config_space + msg->addr,
		       msg->size);
		usfstl_pci_send_response(pcidev, buf);
		return;
	}

	if (ops->cfg_space_read_deferred) {
		ops->cfg_space_read_deferred(pcidev, out,
					     msg->addr, msg->size,
					     buf);
		return;
	}

	switch (msg->size) {
	case 1:
		*(uint8_t *)out = ops->cfg_space_read(pcidev, msg->addr,
						      msg->size);
		break;
	case 2:
		*(uint16_t *)out = htole16(ops->cfg_space_read(pcidev,
							       msg->addr,
							       msg->size));
		break;
	case 4:
		*(uint32_t *)out = htole32(ops->cfg_space_read(pcidev,
							       msg->addr,
							       msg->size));
		break;
	case 8:
		*(uint64_t *)out = htole64(ops->cfg_space_read(pcidev,
							       msg->addr,
							       msg->size));
		break;
	default:
		USFSTL_ASSERT(0);
	}

	usfstl_pci_send_response(pcidev, buf);
}

void usfstl_vhost_pci_cfg_write(struct usfstl_pci_device_ops *ops,
				struct usfstl_pci_device *pcidev,
				struct virtio_pcidev_msg *msg,
				struct usfstl_vhost_user_buf *buf)
{
	uint64_t value;

	if (!ops->cfg_space_write && !ops->cfg_space_write_deferred) {
		const uint8_t *mask = pcidev->config_space_mask;
		uint8_t *val = pcidev->config_space;
		uint8_t *data = msg->data;
		uint32_t i;

		if (msg->addr + msg->size > pcidev->config_space_size)
			return;

		for (i = 0; i < msg->size; i++) {
			val[msg->addr + i] &= ~mask[msg->addr + i];
			val[msg->addr + i] |= data[i] & mask[msg->addr + i];
		}
		usfstl_pci_send_response(pcidev, buf);
		return;
	}

	if (ops->cfg_space_write_deferred) {
		ops->cfg_space_write_deferred(pcidev, msg->addr,
					      msg->data, msg->size,
					      buf);
		return;
	}

	switch (msg->size) {
	case 1:
		value = msg->data[0];
		break;
	case 2:
		value = le16toh(*(uint16_t *)msg->data);
		break;
	case 4:
		value = le32toh(*(uint32_t *)msg->data);
		break;
	case 8:
		value = le64toh(*(uint64_t *)msg->data);
		break;
	default:
		USFSTL_ASSERT(0);
	}

	ops->cfg_space_write(pcidev, msg->addr, msg->size, value);

	usfstl_pci_send_response(pcidev, buf);
}

static void usfstl_vhost_pci_handle(struct usfstl_vhost_user_dev *dev,
				    struct usfstl_vhost_user_buf *buf,
				    unsigned int vring)
{
	struct usfstl_pci_device_ops *ops = dev->server->data;
	struct usfstl_pci_device *pcidev = dev->data;
	struct virtio_pcidev_msg *msg = buf->out_sg[0].iov_base;
	uint8_t *write_buf;

	USFSTL_ASSERT(buf->n_out_sg >= 1 && buf->out_sg[0].iov_len >= sizeof(*msg));

	switch (msg->op) {
	case VIRTIO_PCIDEV_OP_CFG_READ:
		USFSTL_ASSERT(buf->n_in_sg && buf->in_sg[0].iov_len >= msg->size);
		usfstl_vhost_pci_cfg_read(ops, pcidev, msg, buf);
		break;
	case VIRTIO_PCIDEV_OP_CFG_WRITE:
		USFSTL_ASSERT(buf->out_sg[0].iov_len >= sizeof(*msg) + msg->size);
		usfstl_vhost_pci_cfg_write(ops, pcidev, msg, buf);
		break;
	case VIRTIO_PCIDEV_OP_MMIO_READ:
		USFSTL_ASSERT(buf->in_sg[0].iov_len >= msg->size);
		memset(buf->in_sg[0].iov_base, 0xff, msg->size);
		USFSTL_ASSERT(ops->mmio_read || ops->mmio_read_deferred);
		if (ops->mmio_read) {
			ops->mmio_read(pcidev, msg->bar, buf->in_sg[0].iov_base,
				       msg->addr, msg->size);
			usfstl_pci_send_response(pcidev, buf);
		} else {
			ops->mmio_read_deferred(pcidev, msg->bar,
						buf->in_sg[0].iov_base,
						msg->addr, msg->size,
						buf);
		}
		break;
	case VIRTIO_PCIDEV_OP_MMIO_WRITE:
		if (buf->out_sg[0].iov_len > sizeof(*msg)) {
			write_buf = msg->data;
			USFSTL_ASSERT(buf->out_sg[0].iov_len >= sizeof(*msg) + msg->size);
		} else {
			write_buf = buf->out_sg[1].iov_base;
			USFSTL_ASSERT(buf->out_sg[1].iov_len >= msg->size);
		}
		USFSTL_ASSERT(ops->mmio_write || ops->mmio_write_deferred);
		if (ops->mmio_write) {
			ops->mmio_write(pcidev, msg->bar, msg->addr,
					write_buf, msg->size);
			usfstl_pci_send_response(pcidev, buf);
		} else {
			ops->mmio_write_deferred(pcidev, msg->bar, msg->addr,
						 write_buf, msg->size, buf);
		}
		break;
	case VIRTIO_PCIDEV_OP_MMIO_MEMSET:
		if (buf->out_sg[0].iov_len > sizeof(*msg)) {
			write_buf = msg->data;
			USFSTL_ASSERT(buf->out_sg[0].iov_len >= sizeof(*msg) + 1);
		} else {
			write_buf = buf->out_sg[1].iov_base;
			USFSTL_ASSERT(buf->out_sg[1].iov_len >= 1);
		}
		USFSTL_ASSERT(ops->mmio_set || ops->mmio_set_deferred);
		if (ops->mmio_set) {
			ops->mmio_set(pcidev, msg->bar, msg->addr,
				      write_buf[0], msg->size);
			usfstl_pci_send_response(pcidev, buf);
		} else {
			ops->mmio_set_deferred(pcidev, msg->bar, msg->addr,
					       write_buf[0], msg->size, buf);
		}
		break;
	}
}

void usfstl_vhost_pci_disconnected(struct usfstl_vhost_user_dev *dev)
{
	struct usfstl_pci_device_ops *ops = dev->server->data;

	ops->disconnected(dev->data);
}

const struct usfstl_vhost_user_ops usfstl_vhost_user_ops_pci = {
	  .connected = usfstl_vhost_pci_connected,
	  .handle = usfstl_vhost_pci_handle,
	  .disconnected = usfstl_vhost_pci_disconnected,
};

void usfstl_pci_send_int(struct usfstl_pci_device *pcidev, int number)
{
	struct virtio_pcidev_msg msg = {
		.op = VIRTIO_PCIDEV_OP_INT,
		.addr = number,
	};

	USFSTL_ASSERT(number >= 1 && number <= 4);

	usfstl_vhost_user_dev_notify(pcidev->dev, 1, (void *)&msg, sizeof(msg));
}

void usfstl_pci_send_msi(struct usfstl_pci_device *pcidev,
			 uint64_t addr, bool msix, uint32_t data)
{
	struct {
		struct virtio_pcidev_msg hdr;
		uint8_t data[4]; // max size
	} msg = {
		.hdr = {
			.op = VIRTIO_PCIDEV_OP_MSI,
			.addr = addr,
			.size = msix ? 4 : 2,
		},
	};

	if (msix)
		*(uint32_t *)msg.data = htole32(data);
	else
		*(uint16_t *)msg.data = htole16(data);

	usfstl_vhost_user_dev_notify(pcidev->dev, 1, (void *)&msg, sizeof(msg));
}

void usfstl_pci_send_pme(struct usfstl_pci_device *pcidev)
{
	struct virtio_pcidev_msg msg = {
		.op = VIRTIO_PCIDEV_OP_PME,
	};

	usfstl_vhost_user_dev_notify(pcidev->dev, 1, (void *)&msg, sizeof(msg));
}
