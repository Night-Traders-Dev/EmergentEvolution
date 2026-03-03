# Physics Engine

| Property | Detail |
|---|---|
| Particle count | Up to **100,000** simultaneous particles |
| GPU forces | O(n&#178;) pairwise per-frame on Vulkan compute shader |
| CPU physics | O(n) via **spatial acceleration grid** (30px cells, 342&times;193 = 65,906 cells) |
| Parallelism | **OpenMP** across all cores for grid builds, entropy, statistics, measurement, B-field visualization |
| GPU sync | Single `vkQueueWaitIdle` per frame (batched dirty flag) |
| GPU post-processing | **Half-resolution bloom** (extract + H/V Gaussian blur + composite), zoom-adaptive particle sizing |
| World | 10,240 &times; 5,760 px toroidal space |
| Buffers | Double-buffered ping-pong (position, velocity, angle, angular velocity, energy, genome) |
| Genome | 4 floats per particle: charge, spin, color charge / orbital L, decay rate |
| Push Constants | 128 bytes &mdash; full simulation parameters per frame |
| Subsystem dispatch | **Fisher-Yates shuffled** each tick (6 core + 10 medium subsystems) |
| Particle iteration | **Randomized start index** per subsystem per frame via `random_start()` hash |
