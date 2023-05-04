/*
 * UIO driver for IVSHMEM-Doorbell virtual device using devm_memremap_pages()
 *
 * (C) 2009 Cam Macdonell
 * (C) 2017 Henning Schild
 * (C) 2023 Jihong Min
 * based on Hilscher CIF card driver (C) 2007 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under GPL version 2 only.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uio_driver.h>

#define IntrStatus 0x04
#define IntrMask 0x00

struct ivshmem_info {
  struct uio_info *uio;
  struct pci_dev *dev;
  struct dev_pagemap *devm_pgmap;
};

static vm_fault_t uio_ivshmem_vmfault(struct vm_fault *vmf) {
  struct ivshmem_info *this_ivshmem_info = vmf->vma->vm_private_data;

  vmf->page = virt_to_page(this_ivshmem_info->uio->mem[1].internal_addr +
                           ((vmf->pgoff - 1) << PAGE_SHIFT));
  get_page(vmf->page);

  return 0;
}
static const struct vm_operations_struct uio_ivshmem_vmops = {
    .fault = uio_ivshmem_vmfault};
static int uio_ivshmem_mmap(struct uio_info *info, struct vm_area_struct *vma) {
  struct ivshmem_info *this_ivshmem_info = info->priv;
  unsigned long size = vma->vm_end - vma->vm_start;
  int ret;

  if (!vma->vm_pgoff) {
    if (size > PAGE_SIZE)
      return -EINVAL;

    if ((ret = io_remap_pfn_range(
             vma, vma->vm_start,
             pci_resource_start(this_ivshmem_info->dev, 0) >> PAGE_SHIFT, size,
             vma->vm_page_prot)) < 0)
      return ret;
  }

  else {
    if ((((vma->vm_pgoff - 1) << PAGE_SHIFT) + size) >
        pci_resource_len(this_ivshmem_info->dev, 2))
      return -EINVAL;

    vma->vm_ops = &uio_ivshmem_vmops;
    vma->vm_private_data = this_ivshmem_info;
  }

  return 0;
}

static irqreturn_t ivshmem_handler(int irq, struct uio_info *dev_info) {
  struct ivshmem_info *ivshmem_info = dev_info->priv;
  void __iomem *plx_intscr;
  u32 val;

  if (ivshmem_info->dev->msix_enabled)
    return IRQ_HANDLED;

  plx_intscr = dev_info->mem[0].internal_addr + IntrStatus;
  val = readl(plx_intscr);
  if (val == 0)
    return IRQ_NONE;

  return IRQ_HANDLED;
}

static int ivshmem_pci_probe(struct pci_dev *dev,
                             const struct pci_device_id *id) {
  struct uio_info *info;
  struct dev_pagemap *pgmap;
  struct ivshmem_info *ivshmem_info;
  int ret;

  info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
  if (!info)
    return -ENOMEM;

  pgmap = kzalloc(sizeof(struct dev_pagemap), GFP_KERNEL);
  if (!pgmap)
    return -ENOMEM;

  ivshmem_info = kzalloc(sizeof(struct ivshmem_info), GFP_KERNEL);
  if (!ivshmem_info) {
    kfree(info);
    return -ENOMEM;
  }
  info->priv = ivshmem_info;

  if (pci_enable_device(dev))
    goto out_free;

  if (pci_request_regions(dev, "ivshmem"))
    goto out_disable;

  /* Registers */

  info->mem[0].addr = pci_resource_start(dev, 0);
  if (!info->mem[0].addr)
    goto out_release;

  info->mem[0].size = (pci_resource_len(dev, 0) + PAGE_SIZE - 1) & PAGE_MASK;
  info->mem[0].internal_addr = pci_ioremap_bar(dev, 0);
  if (!info->mem[0].internal_addr)
    goto out_release;

  info->mem[0].memtype = UIO_MEM_PHYS;
  info->mem[0].name = "registers";

  /* Shared memory */

  info->mem[1].addr = pci_resource_start(dev, 2);
  if (!info->mem[1].addr)
    goto out_unmap0;

  info->mem[1].size = pci_resource_len(dev, 2);
  pgmap->range.start = pci_resource_start(dev, 2);
  pgmap->range.end = pci_resource_end(dev, 2);
  pgmap->nr_range = 1;
  pgmap->type = MEMORY_DEVICE_GENERIC;
  info->mem[1].internal_addr = devm_memremap_pages(&dev->dev, pgmap);
  if (!info->mem[1].internal_addr)
    goto out_unmap0;

  info->mem[1].memtype = UIO_MEM_PHYS;
  info->mem[1].name = "shmem";

  info->mmap = uio_ivshmem_mmap;

  ivshmem_info->uio = info;
  ivshmem_info->dev = dev;
  ivshmem_info->devm_pgmap = pgmap;

  ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSIX);
  if (ret < 0)
    goto out_unmap2;
  if (ret == 0)
    goto out_vector;
  if (pci_irq_vector(dev, 0)) {
    info->irq = pci_irq_vector(dev, 0);
    info->irq_flags = 0;
    info->handler = ivshmem_handler;
  } else
    dev_warn(&dev->dev, "No IRQ assigned to device: "
                        "no support for interrupts?\n");
  pci_set_master(dev);

  info->name = "uio_ivshmem";
  info->version = "0.0.1";

  if (uio_register_device(&dev->dev, info))
    goto out_vector;

  if (!dev->msix_enabled)
    writel(0xffffffff, info->mem[0].internal_addr + IntrMask);

  pci_set_drvdata(dev, ivshmem_info);

  return 0;
out_vector:
  pci_free_irq_vectors(dev);
out_unmap2:
  devm_memunmap_pages(&dev->dev, pgmap);
out_unmap0:
  iounmap(info->mem[0].internal_addr);
out_release:
  pci_release_regions(dev);
out_disable:
  pci_disable_device(dev);
out_free:
  kfree(ivshmem_info);
  kfree(pgmap);
  kfree(info);
  return -ENODEV;
}

static void ivshmem_pci_remove(struct pci_dev *dev) {
  struct ivshmem_info *ivshmem_info = pci_get_drvdata(dev);
  struct dev_pagemap *pgmap = ivshmem_info->devm_pgmap;
  struct uio_info *info = ivshmem_info->uio;

  pci_set_drvdata(dev, NULL);
  uio_unregister_device(info);
  pci_free_irq_vectors(dev);
  devm_memunmap_pages(&dev->dev, pgmap);
  iounmap(info->mem[0].internal_addr);
  pci_release_regions(dev);
  pci_disable_device(dev);
  kfree(ivshmem_info);
  kfree(pgmap);
  kfree(info);
}

static struct pci_device_id ivshmem_pci_ids[] = {{
                                                     .vendor = 0x1af4,
                                                     .device = 0x1110,
                                                     .subvendor = PCI_ANY_ID,
                                                     .subdevice = PCI_ANY_ID,
                                                 },
                                                 {
                                                     0,
                                                 }};

static struct pci_driver ivshmem_pci_driver = {
    .name = "uio_ivshmem",
    .id_table = ivshmem_pci_ids,
    .probe = ivshmem_pci_probe,
    .remove = ivshmem_pci_remove,
};

module_pci_driver(ivshmem_pci_driver);
MODULE_DEVICE_TABLE(pci, ivshmem_pci_ids);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cam Macdonell");
MODULE_AUTHOR("Henning Schild");
MODULE_AUTHOR("Jihong Min");
