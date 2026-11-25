/* SPDX-License-Identifier: Apache-2.0 */
.section .rodata.spine_c012, "a", %progbits
.balign 4
.global c012_atlas, c012_atlas_end
c012_atlas:
    .incbin "platform/assets/c012/c012_00.atlas"
c012_atlas_end:

.balign 4
.global c012_skeleton, c012_skeleton_end
c012_skeleton:
    .incbin "platform/assets/c012/c012_demo.skel"
c012_skeleton_end:

.balign 4
.global c012_texture_rle, c012_texture_rle_end
c012_texture_rle:
    .incbin "platform/assets/c012/c012_00.rle"
c012_texture_rle_end:
