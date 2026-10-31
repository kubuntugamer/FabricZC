#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

KSYMTAB_FUNC(fabriczc_get_runtime_ctx, "_gpl", "");
KSYMTAB_FUNC(fabriczc_submit_raw_bio, "_gpl", "");

SYMBOL_CRC(fabriczc_get_runtime_ctx, 0xcdbb541b, "_gpl");
SYMBOL_CRC(fabriczc_submit_raw_bio, 0x43c6629f, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x1f172f09, "d_splice_alias" },
	{ 0xfbe7861b, "memcpy" },
	{ 0xcb8b6ec6, "kfree" },
	{ 0xf792861b, "iput" },
	{ 0xc9f4eafd, "register_filesystem" },
	{ 0x11f4259a, "_raw_spin_lock_irqsave" },
	{ 0x4f257de9, "path_put" },
	{ 0xd272d446, "__fentry__" },
	{ 0x5a844b26, "__x86_indirect_thunk_rax" },
	{ 0x6980f362, "kill_block_super" },
	{ 0xf792861b, "unlock_new_inode" },
	{ 0xe8213e80, "_printk" },
	{ 0x9d9ec27a, "put_disk" },
	{ 0xbd03ed67, "__ref_stack_chk_guard" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0x6844e739, "truncate_inode_pages_final" },
	{ 0x86d206f6, "__SCT__WARN_trap" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0xbd03ed67, "page_offset_base" },
	{ 0x14980e8a, "unregister_blkdev" },
	{ 0xb04490dd, "bio_put" },
	{ 0xb04490dd, "bio_endio" },
	{ 0xe9c17e97, "__brelse" },
	{ 0xdc198d4f, "sync_dirty_buffer" },
	{ 0xd7333c3c, "set_capacity" },
	{ 0x2df8aafc, "submit_bio_noacct" },
	{ 0xbeb4268b, "bdev_fput" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0x8de0a862, "setattr_copy" },
	{ 0x44ee09bf, "from_kgid" },
	{ 0xbd03ed67, "phys_base" },
	{ 0x7125a001, "bio_add_page" },
	{ 0x9d9ec27a, "del_gendisk" },
	{ 0x402db74e, "memcmp" },
	{ 0xf10a638c, "__bread_gfp" },
	{ 0xe54e0a6b, "__fortify_panic" },
	{ 0x444885a7, "_raw_spin_unlock_irqrestore" },
	{ 0xd3ad8305, "from_kuid" },
	{ 0xe18ebead, "kern_path" },
	{ 0x5a2322a3, "param_ops_charp" },
	{ 0xb5988148, "kernel_read" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x058c185a, "jiffies" },
	{ 0xbd03ed67, "vmemmap_base" },
	{ 0x82fd7238, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xc108e881, "generic_read_dir" },
	{ 0xad7df6e1, "__register_blkdev" },
	{ 0x7f812bba, "inode_set_ctime_current" },
	{ 0x84585e08, "get_tree_bdev" },
	{ 0x4574d0c7, "__kmalloc_cache_noprof" },
	{ 0x5cb46e6d, "validate_usercopy_range" },
	{ 0xddb470cf, "_copy_from_iter" },
	{ 0xaa8c727c, "file_bdev" },
	{ 0x5a844b26, "__x86_indirect_thunk_r12" },
	{ 0xe58e05ff, "d_parent_ino" },
	{ 0x43a349ca, "strlen" },
	{ 0x73e5be28, "__mark_inode_dirty" },
	{ 0xc1cd8d94, "generic_file_llseek" },
	{ 0x40cec96c, "bio_alloc_bioset" },
	{ 0xc4fee520, "kmalloc_caches" },
	{ 0xa0a43157, "kernel_write" },
	{ 0xa0626dfe, "fs_bio_set" },
	{ 0x53751ea3, "bio_associate_blkg" },
	{ 0xb5255dab, "__blk_alloc_disk" },
	{ 0xcedc48a8, "inode_init_owner" },
	{ 0xb933cd70, "setattr_prepare" },
	{ 0x03d0f304, "iget_locked" },
	{ 0x8b6fac22, "bdev_file_open_by_path" },
	{ 0xa61fd7aa, "__check_object_size" },
	{ 0x0abd9b75, "d_instantiate" },
	{ 0x54d5968b, "_copy_to_iter" },
	{ 0xf792861b, "clear_inode" },
	{ 0xd710adbf, "__kmalloc_noprof" },
	{ 0xfbe7861b, "memmove" },
	{ 0x1d5ff095, "device_add_disk" },
	{ 0xc9f4eafd, "unregister_filesystem" },
	{ 0xe9c17e97, "mark_buffer_dirty" },
	{ 0x0d60cea9, "d_make_root" },
	{ 0xcb5d00ba, "sync_filesystem" },
	{ 0xe9196a28, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x1f172f09,
	0xfbe7861b,
	0xcb8b6ec6,
	0xf792861b,
	0xc9f4eafd,
	0x11f4259a,
	0x4f257de9,
	0xd272d446,
	0x5a844b26,
	0x6980f362,
	0xf792861b,
	0xe8213e80,
	0x9d9ec27a,
	0xbd03ed67,
	0xd272d446,
	0x6844e739,
	0x86d206f6,
	0x90a48d82,
	0xbd03ed67,
	0x14980e8a,
	0xb04490dd,
	0xb04490dd,
	0xe9c17e97,
	0xdc198d4f,
	0xd7333c3c,
	0x2df8aafc,
	0xbeb4268b,
	0xbd03ed67,
	0x8de0a862,
	0x44ee09bf,
	0xbd03ed67,
	0x7125a001,
	0x9d9ec27a,
	0x402db74e,
	0xf10a638c,
	0xe54e0a6b,
	0x444885a7,
	0xd3ad8305,
	0xe18ebead,
	0x5a2322a3,
	0xb5988148,
	0xd272d446,
	0x058c185a,
	0xbd03ed67,
	0x82fd7238,
	0xc108e881,
	0xad7df6e1,
	0x7f812bba,
	0x84585e08,
	0x4574d0c7,
	0x5cb46e6d,
	0xddb470cf,
	0xaa8c727c,
	0x5a844b26,
	0xe58e05ff,
	0x43a349ca,
	0x73e5be28,
	0xc1cd8d94,
	0x40cec96c,
	0xc4fee520,
	0xa0a43157,
	0xa0626dfe,
	0x53751ea3,
	0xb5255dab,
	0xcedc48a8,
	0xb933cd70,
	0x03d0f304,
	0x8b6fac22,
	0xa61fd7aa,
	0x0abd9b75,
	0x54d5968b,
	0xf792861b,
	0xd710adbf,
	0xfbe7861b,
	0x1d5ff095,
	0xc9f4eafd,
	0xe9c17e97,
	0x0d60cea9,
	0xcb5d00ba,
	0xe9196a28,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"d_splice_alias\0"
	"memcpy\0"
	"kfree\0"
	"iput\0"
	"register_filesystem\0"
	"_raw_spin_lock_irqsave\0"
	"path_put\0"
	"__fentry__\0"
	"__x86_indirect_thunk_rax\0"
	"kill_block_super\0"
	"unlock_new_inode\0"
	"_printk\0"
	"put_disk\0"
	"__ref_stack_chk_guard\0"
	"__stack_chk_fail\0"
	"truncate_inode_pages_final\0"
	"__SCT__WARN_trap\0"
	"__ubsan_handle_out_of_bounds\0"
	"page_offset_base\0"
	"unregister_blkdev\0"
	"bio_put\0"
	"bio_endio\0"
	"__brelse\0"
	"sync_dirty_buffer\0"
	"set_capacity\0"
	"submit_bio_noacct\0"
	"bdev_fput\0"
	"random_kmalloc_seed\0"
	"setattr_copy\0"
	"from_kgid\0"
	"phys_base\0"
	"bio_add_page\0"
	"del_gendisk\0"
	"memcmp\0"
	"__bread_gfp\0"
	"__fortify_panic\0"
	"_raw_spin_unlock_irqrestore\0"
	"from_kuid\0"
	"kern_path\0"
	"param_ops_charp\0"
	"kernel_read\0"
	"__x86_return_thunk\0"
	"jiffies\0"
	"vmemmap_base\0"
	"__ubsan_handle_shift_out_of_bounds\0"
	"generic_read_dir\0"
	"__register_blkdev\0"
	"inode_set_ctime_current\0"
	"get_tree_bdev\0"
	"__kmalloc_cache_noprof\0"
	"validate_usercopy_range\0"
	"_copy_from_iter\0"
	"file_bdev\0"
	"__x86_indirect_thunk_r12\0"
	"d_parent_ino\0"
	"strlen\0"
	"__mark_inode_dirty\0"
	"generic_file_llseek\0"
	"bio_alloc_bioset\0"
	"kmalloc_caches\0"
	"kernel_write\0"
	"fs_bio_set\0"
	"bio_associate_blkg\0"
	"__blk_alloc_disk\0"
	"inode_init_owner\0"
	"setattr_prepare\0"
	"iget_locked\0"
	"bdev_file_open_by_path\0"
	"__check_object_size\0"
	"d_instantiate\0"
	"_copy_to_iter\0"
	"clear_inode\0"
	"__kmalloc_noprof\0"
	"memmove\0"
	"device_add_disk\0"
	"unregister_filesystem\0"
	"mark_buffer_dirty\0"
	"d_make_root\0"
	"sync_filesystem\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "41378E6B5DF7138EFFBC066");
