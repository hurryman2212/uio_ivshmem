# Notes

For those who have slow read (& write) performance issue(s) when accessing IVSHMEM region on the guest domain (Windows guest is not confirmed), there is a bug? on AMD-based systems including 5900X & other Zen3-based processors (possibly EPYC too) but not limited to.

Until now, all I had known is that on Linux guest, this issue can be solved by using kernel module remapping the IVSHMEM BAR region via `devm_memremap_pages()`, not `remap_*()` or `io_remap_*()`.
(`*remap_*wc()` only solves write performance issue). But yesterday, I discussed this with the graduate student at UT Austin, Yibo Huang, and he suggested this issue is related to vMTRR which marks the device memory types of I/O devices.

He said in the email that currently (with the versions above certain kernel/QEMU/etc.) the vMTRR emulated by QEMU/KVM set by underlying BIOS/EFI-OVMF is set to mark this region as uncachable (i.e. non-prefetching/non-temporal/etc.). You can check this behavior with cat /proc/mrtt. The reason why it only affects AMD ones is that he said, the vMTRR does not have a real effect on the guest memory access and suggests either marking this region as cachable (= W/B) or simply removing its notation in vMTRR as a viable solution.

But I also recently found that with Intel i9-12900K, there are opposite symptoms compared to the AMD ones, showing degraded performance when using `devm_memremap_pages()` instead of `*remap_*()` (WC does not affect the write performance) starting from certain kernel/QEMU/etc. version.

He suggested that on Intel, if EPT (nested paging) is disabled, vMTRR will take effect but he and I don't know whether this is related to the issue.

BTW, the performance difference when getting degraded access performance is huge so you will probably notice right away and I'm not sure about the use case(s) on Windows.
