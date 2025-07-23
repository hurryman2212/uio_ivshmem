/*
 * UIO IVShmem Driver
 *
 * (C) 2009 Cam Macdonell
 * (C) 2017 Henning Schild
 * (C) 2023 Jihong Min
 * based on Hilscher CIF card driver (C) 2007 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under GPL version 2 only.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uio_driver.h>
#include <linux/version.h>
#ifdef CONFIG_AMD_MEM_ENCRYPT
#include <asm/mem_encrypt.h>
#endif

MODULE_VERSION(__PACKAGE_VERSION__);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cam Macdonell");
MODULE_AUTHOR("Henning Schild");
MODULE_AUTHOR("Jihong Min");

#define IntrStatus 0x04
#define IntrMask 0x00

struct ivshmem_info {
  struct uio_info *uio;
  struct pci_dev *dev;
  struct dev_pagemap *devm_pgmap;
};

int intel = 0;
module_param(intel, int, 0000);
MODULE_PARM_DESC(intel, "Use optimized method for Intel processor");

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

#ifdef CONFIG_AMD_MEM_ENCRYPT
    vma->vm_page_prot.pgprot &= ~(sme_me_mask);
#endif
    vma->vm_private_data = this_ivshmem_info;
    vma->vm_ops = &uio_ivshmem_vmops;
  }

  return 0;
}

static irqreturn_t ivshmem_handler(int irq, struct uio_info *dev_info) {
  struct ivshmem_info *ivshmem_info;
  void __iomem *plx_intscr;
  u32 val;

  ivshmem_info = dev_info->priv;

  if (ivshmem_info->dev->msix_enabled)
    /* Increment UIO read value; Race will happen here! */
    return IRQ_HANDLED;

  /* Deprecated */
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

  ivshmem_info = kzalloc(sizeof(struct ivshmem_info), GFP_KERNEL);
  if (!ivshmem_info) {
    kfree(info);
    return -ENOMEM;
  }

  pgmap = kzalloc(sizeof(struct dev_pagemap), GFP_KERNEL);
  if (!pgmap) {
    kfree(info);
    kfree(ivshmem_info);
    return -ENOMEM;
  }

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
  info->mem[1].name = "shmem";

  if (intel)
    info->mem[1].memtype = UIO_MEM_PHYS;
  else {
    info->mem[1].memtype =
        UIO_MEM_IOVA; // No mark as uncached; Previously UIO_MEM_PHYS was used.

    pgmap->range.start = pci_resource_start(dev, 2);
    pgmap->range.end = pci_resource_end(dev, 2);
    pgmap->nr_range = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
    pgmap->type = MEMORY_DEVICE_DEVDAX;
#else
    pgmap->type = MEMORY_DEVICE_GENERIC;
#endif
    info->mem[1].internal_addr = devm_memremap_pages(&dev->dev, pgmap);
    if (!info->mem[1].internal_addr)
      goto out_unmap0;

    info->mmap = uio_ivshmem_mmap; // Register custom mmap() function.

    ivshmem_info->devm_pgmap = pgmap;
  }

  /* IRQ Handler */

  /* UIO allows only one IRQ to be used... (2048 is the maximum for MSI-X) */
  ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_ALL_TYPES);
  if (ret < 0)
    goto out_unmap2;
  if (ret == 0)
    goto out_vector;

  if (pci_irq_vector(dev, 0)) {
    info->irq = pci_irq_vector(dev, 0);
    info->irq_flags = IRQF_SHARED;
    info->handler = ivshmem_handler;
  } else
    dev_warn(&dev->dev, "No IRQ assigned to device: "
                        "no support for interrupts?\n");
  pci_set_master(dev);

  ivshmem_info->uio = info;
  ivshmem_info->dev = dev;
  info->priv = ivshmem_info;

  info->name = "uio_ivshmem";
  info->version = __PACKAGE_VERSION__;

  if (uio_register_device(&dev->dev, info))
    goto out_clear;

  /* Deprecated */
  if (!dev->msix_enabled)
    writel(0xffffffff, info->mem[0].internal_addr + IntrMask);

  pci_set_drvdata(dev, ivshmem_info);

  return 0;
out_clear:
  pci_clear_master(dev);
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
  kfree(pgmap);
  kfree(ivshmem_info);
  kfree(info);
  return -ENODEV;
}

static void ivshmem_pci_remove(struct pci_dev *dev) {
  struct ivshmem_info *ivshmem_info = pci_get_drvdata(dev);
  struct uio_info *info = ivshmem_info->uio;
  struct dev_pagemap *pgmap = ivshmem_info->devm_pgmap;

  pci_set_drvdata(dev, NULL);
  uio_unregister_device(info);
  pci_clear_master(dev);
  pci_free_irq_vectors(dev);
  if (!intel)
    devm_memunmap_pages(&dev->dev, pgmap);
  iounmap(info->mem[0].internal_addr);
  pci_release_regions(dev);
  pci_disable_device(dev);
  kfree(pgmap);
  kfree(ivshmem_info);
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
