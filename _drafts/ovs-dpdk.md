ovs-dpdk
=======

1. vfio - virtual function I/O

    let VM access physical devices directly, through IOMMU.
    needs both hardward (vt-d) and linux kernel (>3.6) support.

    VFIO includes, but not limited to SR-IOV.

    A more accurate abbre: Versatile Framework for Userspace I/O.

    Userspace I/O framework.

    provides access to a device within a secure and programmable IOMMU context.

1. IOMMU roles

    * translation
      - I/O Virtual Address (IOVA) space
      - Previously the main purpose of an IOMMU
    * isolation
      - per device translation
      - Invalid access blocked

    both requires secure userspace access

1. QEMU

    Quick EMUlator.

    creating fake devices, since 2003.

    Device programming:
    how does VM programmed I/O reach a device?

    * trapped by hypervisor (KVM/QEMU)
    * MemoryRegion lookup performed
    * MemoryRegion.{read,write} accessors called
    * read/write to vfio region offsets

1. MMIO, PMIO

    Memory-Mapped I/O

    Port-Mapped I/O
