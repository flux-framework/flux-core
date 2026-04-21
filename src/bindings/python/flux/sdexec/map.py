###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Resource ID to systemd unit property mapping for sdexec.

This module maps Flux resource IDs (logical core and GPU indices) to systemd
transient unit properties such as ``AllowedCPUs``, ``AllowedMemoryNodes``, and
``AllowedDevices``.

Overview
--------

:class:`ResourceMapper` is the base class.  Its :meth:`~ResourceMapper.map`
method dispatches on resource type: for each key in the input dict it calls
``self.map_<type>(value)`` and merges the results.  Supported resource types
are defined by the methods present on the mapper class.

:class:`HwlocMapper` is the built-in implementation.  It uses hwloc topology
data (passed as XML) to translate logical core IDs to CPU and NUMA-node sets,
and logical GPU IDs to PCI addresses which are then resolved to ``/dev``
paths via sysfs.

GPU Device Discovery
--------------------

:class:`HwlocMapper` discovers GPU device nodes via sysfs and includes all
device paths needed for compute workloads:

**NVIDIA GPUs:**
    - ``/dev/nvidia<N>`` — Main GPU device (N matches hwloc GPU ID)
    - ``/dev/nvidiactl`` — Control device (shared across all GPUs)
    - ``/dev/nvidia-uvm`` — Unified Virtual Memory (required for CUDA)
    - ``/dev/nvidia-uvm-tools`` — UVM tools (optional)
    - ``/dev/dri/renderD<M>`` — DRM render node (alternative interface)

**AMD GPUs:**
    - ``/dev/dri/renderD<M>`` — DRM render node (primary ROCm interface)
    - ``/dev/dri/card<N>`` — DRM card device
    - ``/dev/kfd`` — Kernel Fusion Driver (required for ROCm/HIP, shared)

Discovery is opportunistic: if a device path doesn't exist, it's skipped.
This makes the mapper work across different drivers and configurations.
Shared devices like ``/dev/kfd`` and ``/dev/nvidiactl`` are automatically
deduplicated when multiple GPUs are allocated to a job.

Extension
---------

**Override the GPU mapping only** — the most common need.  Subclass
:class:`HwlocMapper` and override :meth:`~HwlocMapper.map_gpus`.  The core
and NUMA-node mapping is inherited unchanged::

    class MyGpuMapper(HwlocMapper):
        def map_gpus(self, gpus):
            # custom discovery, e.g. vendor-specific sysfs
            ...
            return {"AllowedDevices": [...]}

**Add a new resource type** — subclass :class:`HwlocMapper` (or
:class:`ResourceMapper`) and add a ``map_<type>`` method.  The dispatcher
picks it up automatically when that key appears in the resources dict::

    class FpgaMapper(HwlocMapper):
        def map_fpgas(self, fpgas):
            ...
            return {"AllowedDevices": [...]}

**Replace the implementation entirely** — subclass :class:`ResourceMapper`
directly and implement all required ``map_<type>`` methods.  Point the sdexec
broker module at your class via the ``[sdexec] mapper`` config key using the
fully-qualified class name (``"mypackage.mymodule.MyMapper"``).

Configuration
-------------

The sdexec broker module loads the mapper class named by the
``[sdexec] mapper`` TOML config key.  The value must be a fully-qualified
Python class name.  When omitted, :class:`HwlocMapper` is used.
"""

import errno
from pathlib import Path

from flux.idset import IDset
from flux.resource import ResourceSet

# Sysfs and device path constants
SYSFS_PCI_DEVICES = "/sys/bus/pci/devices"
DEV_DRI = "/dev/dri"
DEV_PREFIX = "/dev"

# Driver names
NVIDIA_DRIVER = "nvidia"
AMD_DRIVER = "amdgpu"

# NVIDIA shared devices required for compute
NVIDIA_SHARED_DEVICES = ["nvidiactl", "nvidia-uvm", "nvidia-uvm-tools"]

# Device name prefixes
DRM_CARD_PREFIX = "card"


class ResourceMapper:
    """Base class for resource ID to systemd unit property mappers.

    Subclasses implement ``map_<type>`` methods for each resource type they
    support.  :meth:`map` extracts the local rank's resources from R and
    dispatches each resource type to the corresponding method.

    Args:
        rank: Local broker rank used to extract per-node resources from R.
            Defaults to 0.
    """

    def __init__(self, rank=0):
        self._rank = rank

    def map(self, R):
        """Map local resources from R to systemd unit properties.

        Extracts the local rank's resources from *R*, then dispatches each
        resource type to ``map_<type>(idset_string)``, merging the results.
        Resource types with no corresponding method are silently skipped.

        Args:
            R: R JSON string, dict, or :class:`~flux.resource.ResourceSet`
                describing the job's allocated resources.

        Returns:
            Dict of systemd unit property names to values.
        """
        if not isinstance(R, ResourceSet):
            R = ResourceSet(R)
        local = R.copy_ranks(self._rank).impl._ranks.get(self._rank, {})
        result = {}
        for rtype, ids in local.items():
            method = getattr(self, f"map_{rtype}", None)
            if method is not None:
                value = str(IDset(",".join(str(i) for i in sorted(ids)))) if ids else ""
                result.update(method(value))
        return self.finalize_properties(result, R)

    def finalize_properties(self, properties, R):
        """Hook to add or modify systemd unit properties.

        Called after all resource-specific mapping is complete. Subclasses
        can override this to add arbitrary systemd unit properties not tied
        to specific resource types, such as resource accounting, limits,
        security settings, or conditional properties based on job
        characteristics.

        The default implementation sets ``DevicePolicy=closed`` when
        properties are present (non-empty dict), ensuring device containment
        by allowing access to standard pseudo devices (/dev/null, /dev/zero,
        etc.) while blocking physical devices unless explicitly allowed via
        ``DeviceAllow`` (set by resource mappers like
        :meth:`~HwlocMapper.map_gpus`).

        An empty properties dict is left empty, preserving the ability for
        custom mappers to return ``{}`` to indicate unconstrained execution
        (no systemd unit properties applied). In practice, normal jobs will
        always have cores allocated and thus non-empty properties.

        Args:
            properties: Dict of properties generated by map_<type>() methods.
            R: The original :class:`~flux.resource.ResourceSet` object.

        Returns:
            Dict of systemd unit property names to values.

        Example:
            Override to add resource accounting::

                class AccountingMapper(HwlocMapper):
                    def finalize_properties(self, properties, R):
                        properties.update({
                            "CPUAccounting": "true",
                            "MemoryAccounting": "true",
                        })
                        return super().finalize_properties(properties, R)
        """
        if properties:
            properties["DevicePolicy"] = "closed"
        return properties


class HwlocMapper(ResourceMapper):
    """hwloc-based resource mapper.

    Uses hwloc topology XML to map logical core IDs to ``AllowedCPUs`` and
    ``AllowedMemoryNodes``, and logical GPU IDs to ``AllowedDevices`` entries
    resolved via sysfs.

    Args:
        xml: hwloc topology XML string for the local node.
        rank: Local broker rank.  Defaults to 0.
    """

    def __init__(self, xml, rank=0):
        super().__init__(rank)
        from _flux._rhwloc_map import ffi, lib

        self._ffi = ffi
        self._lib = lib
        self._map = lib.rhwloc_map_create(xml.encode())
        if self._map == ffi.NULL:
            raise OSError(errno.EINVAL, "rhwloc_map_create failed")

    def __del__(self):
        if self._map and self._map != self._ffi.NULL:
            self._lib.rhwloc_map_destroy(self._map)
            self._map = None

    def map_cores(self, cores):
        """Map logical core IDs to AllowedCPUs and AllowedMemoryNodes.

        Args:
            cores: Idset string of logical core IDs, e.g. ``"0-3"``.

        Returns:
            Dict with ``AllowedCPUs`` and ``AllowedMemoryNodes`` keys.

        Raises:
            OSError: If the mapping fails.
        """
        ffi = self._ffi
        lib = self._lib
        cpus_out = ffi.new("char **")
        mems_out = ffi.new("char **")
        if lib.rhwloc_map_cores(self._map, cores.encode(), cpus_out, mems_out) < 0:
            raise OSError(ffi.errno, "rhwloc_map_cores failed")
        cpus = ffi.string(cpus_out[0]).decode()
        mems = ffi.string(mems_out[0]).decode()
        lib.free(cpus_out[0])
        lib.free(mems_out[0])
        return {"AllowedCPUs": cpus, "AllowedMemoryNodes": mems}

    def _get_driver_name(self, pci_path):
        """Get the driver name for a PCI device.

        Args:
            pci_path: Path object to PCI device directory.

        Returns:
            Driver name string (lowercase), or None if unavailable.
        """
        driver_link = pci_path / "driver"
        if not driver_link.exists():
            return None
        try:
            return driver_link.resolve().name.lower()
        except OSError:
            return None

    def _discover_drm_devices(self, pci_path):
        """Discover DRM devices for a PCI device.

        Args:
            pci_path: Path object to PCI device directory.

        Returns:
            List of DeviceAllow strings, e.g. ``["/dev/dri/renderD128 rw"]``.
        """
        drm_path = pci_path / "drm"
        if not drm_path.exists():
            return []
        devices = []
        for entry in sorted(drm_path.iterdir()):
            dev = Path(DEV_DRI) / entry.name
            if dev.exists():
                devices.append(f"{dev} rw")
        return devices

    def _discover_nvidia_devices(self, pci_path):
        """Discover NVIDIA-specific device nodes for a GPU.

        Args:
            pci_path: Path object to PCI device directory.

        Returns:
            List of DeviceAllow strings for NVIDIA devices.
        """
        devices = []
        drm_path = pci_path / "drm"

        gpu_index = None
        if drm_path.exists():
            for entry in drm_path.iterdir():
                if entry.name.startswith(DRM_CARD_PREFIX):
                    suffix = entry.name[len(DRM_CARD_PREFIX) :]
                    if suffix.isdigit():
                        gpu_index = int(suffix)
                        break

        if gpu_index is not None:
            nvidia_dev = Path(f"{DEV_PREFIX}/nvidia{gpu_index}")
            if nvidia_dev.exists():
                devices.append(f"{nvidia_dev} rw")

        for shared_dev in NVIDIA_SHARED_DEVICES:
            dev_path = Path(f"{DEV_PREFIX}/{shared_dev}")
            if dev_path.exists():
                devices.append(f"{dev_path} rw")

        return devices

    def _discover_amd_devices(self, pci_path):
        """Discover AMD-specific device nodes for a GPU.

        Args:
            pci_path: Path object to PCI device directory.

        Returns:
            List of DeviceAllow strings for AMD devices.
        """
        devices = []
        kfd_dev = Path(f"{DEV_PREFIX}/kfd")
        if kfd_dev.exists():
            devices.append(f"{kfd_dev} rw")

        return devices

    def _discover_gpu_devices(self, pci_addr):
        """Discover all device nodes for a GPU at the given PCI address.

        Args:
            pci_addr: PCI address string, e.g. ``"0000:01:00.0"``.

        Returns:
            List of DeviceAllow strings, e.g.
            ``["/dev/nvidia0 rw", "/dev/nvidiactl rw",
              "/dev/dri/renderD128 rw"]``.
        """
        pci_path = Path(f"{SYSFS_PCI_DEVICES}/{pci_addr}")
        devices = []

        devices.extend(self._discover_drm_devices(pci_path))

        driver = self._get_driver_name(pci_path)
        if driver:
            if NVIDIA_DRIVER in driver:
                devices.extend(self._discover_nvidia_devices(pci_path))
            elif AMD_DRIVER in driver:
                devices.extend(self._discover_amd_devices(pci_path))

        return devices

    def map_gpus(self, gpus):
        """Map logical GPU IDs to a DeviceAllow property.

        An empty *gpus* string (no GPUs allocated) returns an empty dict.
        Device policy enforcement is handled by :meth:`finalize_properties`.

        Args:
            gpus: Idset string of logical GPU IDs, e.g. ``"0-1"``,
                or ``""`` when no GPUs are allocated.

        Returns:
            Dict with ``DeviceAllow`` key containing a comma-separated
            string of ``"<path> rw"`` entries, or empty dict when no
            GPUs are allocated.

        Raises:
            OSError: If the hwloc GPU PCI address lookup fails.
        """
        if not gpus:
            return {}
        ffi = self._ffi
        lib = self._lib
        addrs = lib.rhwloc_map_gpu_pci_addrs(self._map, gpus.encode())
        if addrs == ffi.NULL:
            raise OSError(ffi.errno, "rhwloc_map_gpu_pci_addrs failed")
        try:
            pci_addrs = []
            i = 0
            while addrs[i] != ffi.NULL:
                pci_addrs.append(ffi.string(addrs[i]).decode())
                i += 1
        finally:
            lib.rhwloc_map_strv_free(addrs)

        devices = []
        for addr in pci_addrs:
            devices.extend(self._discover_gpu_devices(addr))

        unique_devices = list(dict.fromkeys(devices))
        return {"DeviceAllow": ",".join(unique_devices)}
