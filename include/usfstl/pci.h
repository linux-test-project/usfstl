/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_PCI_H_
#define _USFSTL_PCI_H_
#include "vhost.h"

/*
 * This cooperates with the virt-pci code in Linux's ARCH=um to simulate
 * PCI devices inside the UML virtual machine.
 *
 * To use it, declare a new struct usfstl_vhost_user_server, using the
 * USFSTL_VHOST_USER_SERVER_PCI macro to initialize it, like so:
 *
 * struct usfstl_pci_device_ops my_pci_device_ops = {
 *	...
 * };
 *
 * static struct usfstl_vhost_user_server my_vhost_user_server = {
 *	USFSTL_VHOST_USER_SERVER_PCI(my_pci_device_ops),
 *
 *	// need to initialize some fields such as
 *	// .socket = ...,
 *
 *	// and optionally:
 *	// .ctrl = ...,
 *	// .scheduler = ...,
 * };
 *
 * and then start that normally, i.e. usfstl_vhost_user_server_start().
 */

/**
 * struct usfstl_pci_device - PCI device structure
 * @config_space: configuration space data, if callbacks aren't used
 * @config_space_mask: configuration space mask (for writing), if callbacks
 *	aren't used
 * @config_space_size: configuration space size (if callbacks aren't used)
 * @dev: vhost user device, will be filled in by the generic code,
 *	you may use e.g. usfstl_vhost_user_to_va() with this (but must be
 *	careful to not do that for MSI(-X) interrupt writes)
 */
struct usfstl_pci_device {
	void *config_space;
	const void *config_space_mask;
	unsigned int config_space_size;
	struct usfstl_vhost_user_dev *dev;
};

/**
 * struct usfstl_pci_device_ops - PCI device simulation
 * @connected: A new virtio connection was made, so a new device was
 *	created. This must return a non-%NULL struct with the relevant
 *	data pre-populated.
 * @disconnected: the virtio connection to the given device was lost,
 *	this must be assigned to free the struct returned by @connected
 * @cfg_space_read: config space read, size is in bytes (1, 2, 4, 8),
 *	optional, but if not set the device's config_space pointers
 *	and size must be set.
 * @cfg_space_write: config space write, similar to @config_space_read,
 *	but could also be used instead of config_space_mask, i.e. the
 *	logic is to try @cfg_space_write first.
 * @cfg_space_read_deferred: like @cfg_space_read, but deferred, and the
 *	user must call usfstl_pci_send_response() with the given vubuf.
 *	Note that the output pointer should get the correct byte-order
 *	(little endian) value of the appropriate size.
 * @cfg_space_write_deferred: like @cfg_space_write, but deferred, and the
 *	user must call usfstl_pci_send_response() with the given vubuf.
 *	Note that the input pointer points to the correct byte-order
 *	(little endian) value of the appropriate size.
 * @mmio_read: read MMIO space at the given BAR/offset
 * @mmio_write: write MMIO space at the given BAR/offset
 * @mmio_set: memset MMIO space at the given BAR/offset
 * @mmio_read_deferred: like @mmio_read, but deferred, and the user must
 *	call usfstl_pci_send_response() with the given vubuf
 * @mmio_write_deferred: like @mmio_write, but deferred, and the user must
 *	call usfstl_pci_send_response() with the given vubuf
 * @mmio_set_deferred: like @mmio_set, but deferred, and the user must
 *	call usfstl_pci_send_response() with the given vubuf
 */
struct usfstl_pci_device_ops {
	struct usfstl_pci_device *(*connected)(void);
	void (*disconnected)(struct usfstl_pci_device *dev);

	uint64_t (*cfg_space_read)(struct usfstl_pci_device *dev,
				   int offset, int size);
	void (*cfg_space_write)(struct usfstl_pci_device *dev,
				int offset, int size, uint64_t value);

	void (*cfg_space_read_deferred)(struct usfstl_pci_device *dev,
					void *buf, int offset, int size,
					struct usfstl_vhost_user_buf *vubuf);
	void (*cfg_space_write_deferred)(struct usfstl_pci_device *dev,
					 int offset, const void *buf, int size,
					 struct usfstl_vhost_user_buf *vubuf);

	void (*mmio_read)(struct usfstl_pci_device *dev, int bar,
			  void *buf, size_t offset, size_t size);
	void (*mmio_write)(struct usfstl_pci_device *dev, int bar,
			   size_t offset, const void *buf, size_t size);
	void (*mmio_set)(struct usfstl_pci_device *dev, int bar,
			 size_t offset, uint8_t value, size_t size);

	void (*mmio_read_deferred)(struct usfstl_pci_device *dev, int bar,
				   void *buf, size_t offset, size_t size,
				   struct usfstl_vhost_user_buf *vubuf);
	void (*mmio_write_deferred)(struct usfstl_pci_device *dev, int bar,
				    size_t offset, const void *buf, size_t size,
				    struct usfstl_vhost_user_buf *vubuf);
	void (*mmio_set_deferred)(struct usfstl_pci_device *dev, int bar,
				  size_t offset, uint8_t value, size_t size,
				  struct usfstl_vhost_user_buf *vubuf);
};

extern const struct usfstl_vhost_user_ops usfstl_vhost_user_ops_pci;

#define USFSTL_VHOST_USER_SERVER_PCI(pci_device_ops)		\
	.ops = &usfstl_vhost_user_ops_pci,			\
	.max_queues = 2,					\
	.input_queues = 0x1,					\
	.deferred_handling = 1,					\
	.features = 1ULL << VHOST_USER_F_PROTOCOL_FEATURES |	\
		    1ULL << VIRTIO_F_VERSION_1,			\
	.config = NULL,						\
	.config_len = 0,					\
	.data = &pci_device_ops

/**
 * usfstl_pci_send_interrupt - send an interrupt
 * @pcidev: the device to send for
 * @number: the interrupt number to send (1-4 for INTA-INTD)
 */
void usfstl_pci_send_int(struct usfstl_pci_device *pcidev, int number);

/**
 * usfstl_pci_send_interrupt - send an MSI/MSI-X event
 * @pcidev: the device to send for
 * @addr: the (physical) address to send to
 * @msix: indicates MSI-X (32-bit write)
 * @data: the message to send (16 bits only for MSI)
 */
void usfstl_pci_send_msi(struct usfstl_pci_device *pcidev,
			 uint64_t addr, bool msix, uint32_t data);

/**
 * usfstl_pci_send_pme - signal the PME# line
 * @pcidev: the device to send for
 */
void usfstl_pci_send_pme(struct usfstl_pci_device *pcidev);

static inline void usfstl_pci_send_response(struct usfstl_pci_device *pcidev,
					    struct usfstl_vhost_user_buf *vubuf)
{
	usfstl_vhost_user_send_response(pcidev->dev, vubuf);
}

/**
 * usfstl_pci_pa_to_va - translate physical address to virtual
 * @pcidev: device to translate address for
 * @physaddr: physical address given by the host
 */
static inline void *usfstl_pci_pa_to_va(struct usfstl_pci_device *pcidev,
					uint64_t physaddr)
{
	return usfstl_vhost_user_to_va(pcidev->dev, physaddr);
}

/**
 * usfstl_pci_dma_read - DMA read from physical memory
 * @pcidev: device that does the DMA access
 * @buf: output buffer
 * @physaddr: physical address to copy from
 * @size: number of bytes to copy
 */
static inline void usfstl_pci_dma_read(struct usfstl_pci_device *pcidev,
				       void *buf, uint64_t physaddr,
				       unsigned int size)
{
	memcpy(buf, usfstl_pci_pa_to_va(pcidev, physaddr), size);
}

/**
 * usfstl_pci_dma_read - DMA write to physical memory
 * @pcidev: device that does the DMA access
 * @physaddr: physical address to copy to
 * @buf: input buffer
 * @size: number of bytes to copy
 */
static inline void usfstl_pci_dma_write(struct usfstl_pci_device *pcidev,
					uint64_t physaddr, const void *buf,
					unsigned int size)
{
	memcpy(usfstl_pci_pa_to_va(pcidev, physaddr), buf, size);
}

#endif // _USFSTL_PCI_H_
