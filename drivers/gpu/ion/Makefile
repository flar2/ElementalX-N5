ccflags-y := -O3 -ffast-math -fgcse-lm -fgcse-sm -fsched-spec-load -fforce-addr -fsingle-precision-constant -mcpu=cortex-a15 -mtune=cortex-a15 -marm -mfpu=neon-vfpv4
obj-$(CONFIG_ION) +=	ion.o ion_heap.o ion_page_pool.o ion_system_heap.o \
			ion_carveout_heap.o ion_chunk_heap.o
obj-$(CONFIG_CMA) += ion_cma_heap.o ion_cma_secure_heap.o
obj-$(CONFIG_ION_TEGRA) += tegra/
obj-$(CONFIG_ION_MSM) += ion_iommu_heap.o ion_cp_heap.o ion_removed_heap.o msm/
