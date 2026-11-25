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

KSYMTAB_FUNC(fabriczc_get_runtime_ctx, "");
SYMBOL_FLAGS(fabriczc_get_runtime_ctx, 0x01);
KSYMTAB_FUNC(fabriczc_submit_raw_bio, "");
SYMBOL_FLAGS(fabriczc_submit_raw_bio, 0x01);

SYMBOL_CRC(fabriczc_get_runtime_ctx, 0x8bb581d7);
SYMBOL_CRC(fabriczc_submit_raw_bio, 0x203cf112);

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xfc4f1ddf, "register_filesystem" },
	{ 0x93e870e4, "folio_clear_dirty_for_io" },
	{ 0x11f4259a, "_raw_spin_lock_irqsave" },
	{ 0x1040e01d, "path_put" },
	{ 0xd272d446, "__fentry__" },
	{ 0x5a844b26, "__x86_indirect_thunk_rax" },
	{ 0x73e9edb5, "kill_block_super" },
	{ 0x1256a406, "unlock_new_inode" },
	{ 0xe8213e80, "_printk" },
	{ 0x889a967f, "put_disk" },
	{ 0xbd03ed67, "__ref_stack_chk_guard" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0xb2c8c48c, "make_kuid" },
	{ 0x448ccf2b, "truncate_inode_pages_final" },
	{ 0x137b50b7, "sync_blockdev" },
	{ 0x0e9e438f, "__filemap_set_wb_err" },
	{ 0xd630cb0d, "generic_file_mmap" },
	{ 0x86d206f6, "__SCT__WARN_trap" },
	{ 0xdaaa00b0, "blk_finish_plug" },
	{ 0xfb20c26f, "bdev_getblk" },
	{ 0x414016b2, "submit_bio" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0xbd03ed67, "page_offset_base" },
	{ 0x14980e8a, "unregister_blkdev" },
	{ 0xeb3f3328, "bio_put" },
	{ 0xeb3f3328, "bio_endio" },
	{ 0x9f699604, "__brelse" },
	{ 0x1a1640d1, "sync_dirty_buffer" },
	{ 0xc6818741, "__bh_read_batch" },
	{ 0x6e4a1a94, "set_capacity" },
	{ 0x4aeb5613, "xa_load" },
	{ 0x414016b2, "submit_bio_noacct" },
	{ 0xe5b3ff66, "bdev_fput" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0x03b9c702, "setattr_copy" },
	{ 0xd7a59a65, "vmalloc_noprof" },
	{ 0x9f699604, "__wait_on_buffer" },
	{ 0x37c947d5, "mutex_lock" },
	{ 0xb50dac9e, "from_kgid" },
	{ 0x404510a7, "const_current_task" },
	{ 0x62cbec20, "wait_for_completion_io" },
	{ 0xbd03ed67, "phys_base" },
	{ 0x390415f5, "bio_add_page" },
	{ 0x889a967f, "del_gendisk" },
	{ 0x402db74e, "memcmp" },
	{ 0xfb20c26f, "__bread_gfp" },
	{ 0xe54e0a6b, "__fortify_panic" },
	{ 0x444885a7, "_raw_spin_unlock_irqrestore" },
	{ 0x77908aea, "__folio_start_writeback" },
	{ 0x09db26ba, "folio_end_writeback" },
	{ 0x224e2624, "from_kuid" },
	{ 0x0e9cab28, "memset" },
	{ 0xfa8db613, "kern_path" },
	{ 0x739ece98, "param_ops_charp" },
	{ 0x742c42d0, "kernel_read" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x87615c71, "make_kgid" },
	{ 0x6dbafe4d, "truncate_setsize" },
	{ 0x058c185a, "jiffies" },
	{ 0xbd03ed67, "vmemmap_base" },
	{ 0x82fd7238, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xf1de9e85, "vfree" },
	{ 0xddf171d1, "generic_read_dir" },
	{ 0x37c947d5, "mutex_unlock" },
	{ 0xad7df6e1, "__register_blkdev" },
	{ 0x633c0bf3, "init_user_ns" },
	{ 0xd4e708d6, "xa_find" },
	{ 0xe29aba61, "inode_set_ctime_current" },
	{ 0x1256a406, "inc_nlink" },
	{ 0x58b7115b, "__folio_put" },
	{ 0xb5ac7216, "get_tree_bdev" },
	{ 0xd880f319, "__kmalloc_cache_noprof" },
	{ 0x5cb46e6d, "validate_usercopy_range" },
	{ 0x10f800d3, "_copy_from_iter" },
	{ 0x11ddfdb1, "filemap_dirty_folio" },
	{ 0xce8bbb2a, "file_bdev" },
	{ 0xe6a2a545, "errseq_set" },
	{ 0xd4e708d6, "xa_find_after" },
	{ 0xf5449e95, "d_parent_ino" },
	{ 0xc240e2b3, "filemap_write_and_wait_range" },
	{ 0xe4de56b4, "__ubsan_handle_load_invalid_value" },
	{ 0x43a349ca, "strlen" },
	{ 0xf1de9e85, "kvfree" },
	{ 0x472a2372, "__mark_inode_dirty" },
	{ 0x338d4033, "generic_file_llseek" },
	{ 0x50e67a8a, "bio_alloc_bioset" },
	{ 0x35aa6a66, "blk_status_to_errno" },
	{ 0xd1110127, "__kvmalloc_node_noprof" },
	{ 0xe6e9248d, "submit_bio_wait" },
	{ 0x1256a406, "drop_nlink" },
	{ 0x7851be11, "__SCT__might_resched" },
	{ 0x8d534214, "kmalloc_caches" },
	{ 0xa39aca7c, "fs_bio_set" },
	{ 0x37c947d5, "mutex_init_generic" },
	{ 0x1a8cfd07, "bio_associate_blkg" },
	{ 0x918c6270, "__blk_alloc_disk" },
	{ 0xe0644d85, "inode_init_owner" },
	{ 0xc45d298e, "is_vmalloc_addr" },
	{ 0x8c08aaee, "setattr_prepare" },
	{ 0x0a70c0fb, "iget_locked" },
	{ 0x89c673a7, "bdev_file_open_by_path" },
	{ 0x1256a406, "clear_nlink" },
	{ 0xa61fd7aa, "__check_object_size" },
	{ 0x0b31ec42, "d_instantiate" },
	{ 0x76609d76, "_copy_to_iter" },
	{ 0xdaaa00b0, "blk_start_plug" },
	{ 0x1256a406, "clear_inode" },
	{ 0x71356ef9, "__kmalloc_noprof" },
	{ 0x09db26ba, "folio_unlock" },
	{ 0x9da14a2c, "vmalloc_to_page" },
	{ 0xfbe7861b, "memmove" },
	{ 0x38e96d4e, "device_add_disk" },
	{ 0x62cbec20, "complete" },
	{ 0xfc4f1ddf, "unregister_filesystem" },
	{ 0x9f699604, "mark_buffer_dirty" },
	{ 0xc2ccdd1e, "__init_swait_queue_head" },
	{ 0xbc7801bc, "d_make_root" },
	{ 0x5a844b26, "__x86_indirect_thunk_r15" },
	{ 0x26bddab9, "write_dirty_buffer" },
	{ 0x6e135c63, "d_splice_alias" },
	{ 0xfbe7861b, "memcpy" },
	{ 0x1256a406, "ihold" },
	{ 0xcb8b6ec6, "kfree" },
	{ 0xbfff7bd0, "invalidate_inode_pages2_range" },
	{ 0x1256a406, "iput" },
	{ 0x324ae2bc, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xfc4f1ddf,
	0x93e870e4,
	0x11f4259a,
	0x1040e01d,
	0xd272d446,
	0x5a844b26,
	0x73e9edb5,
	0x1256a406,
	0xe8213e80,
	0x889a967f,
	0xbd03ed67,
	0xd272d446,
	0xb2c8c48c,
	0x448ccf2b,
	0x137b50b7,
	0x0e9e438f,
	0xd630cb0d,
	0x86d206f6,
	0xdaaa00b0,
	0xfb20c26f,
	0x414016b2,
	0x90a48d82,
	0xbd03ed67,
	0x14980e8a,
	0xeb3f3328,
	0xeb3f3328,
	0x9f699604,
	0x1a1640d1,
	0xc6818741,
	0x6e4a1a94,
	0x4aeb5613,
	0x414016b2,
	0xe5b3ff66,
	0xbd03ed67,
	0x03b9c702,
	0xd7a59a65,
	0x9f699604,
	0x37c947d5,
	0xb50dac9e,
	0x404510a7,
	0x62cbec20,
	0xbd03ed67,
	0x390415f5,
	0x889a967f,
	0x402db74e,
	0xfb20c26f,
	0xe54e0a6b,
	0x444885a7,
	0x77908aea,
	0x09db26ba,
	0x224e2624,
	0x0e9cab28,
	0xfa8db613,
	0x739ece98,
	0x742c42d0,
	0xd272d446,
	0x87615c71,
	0x6dbafe4d,
	0x058c185a,
	0xbd03ed67,
	0x82fd7238,
	0xf1de9e85,
	0xddf171d1,
	0x37c947d5,
	0xad7df6e1,
	0x633c0bf3,
	0xd4e708d6,
	0xe29aba61,
	0x1256a406,
	0x58b7115b,
	0xb5ac7216,
	0xd880f319,
	0x5cb46e6d,
	0x10f800d3,
	0x11ddfdb1,
	0xce8bbb2a,
	0xe6a2a545,
	0xd4e708d6,
	0xf5449e95,
	0xc240e2b3,
	0xe4de56b4,
	0x43a349ca,
	0xf1de9e85,
	0x472a2372,
	0x338d4033,
	0x50e67a8a,
	0x35aa6a66,
	0xd1110127,
	0xe6e9248d,
	0x1256a406,
	0x7851be11,
	0x8d534214,
	0xa39aca7c,
	0x37c947d5,
	0x1a8cfd07,
	0x918c6270,
	0xe0644d85,
	0xc45d298e,
	0x8c08aaee,
	0x0a70c0fb,
	0x89c673a7,
	0x1256a406,
	0xa61fd7aa,
	0x0b31ec42,
	0x76609d76,
	0xdaaa00b0,
	0x1256a406,
	0x71356ef9,
	0x09db26ba,
	0x9da14a2c,
	0xfbe7861b,
	0x38e96d4e,
	0x62cbec20,
	0xfc4f1ddf,
	0x9f699604,
	0xc2ccdd1e,
	0xbc7801bc,
	0x5a844b26,
	0x26bddab9,
	0x6e135c63,
	0xfbe7861b,
	0x1256a406,
	0xcb8b6ec6,
	0xbfff7bd0,
	0x1256a406,
	0x324ae2bc,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"register_filesystem\0"
	"folio_clear_dirty_for_io\0"
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
	"make_kuid\0"
	"truncate_inode_pages_final\0"
	"sync_blockdev\0"
	"__filemap_set_wb_err\0"
	"generic_file_mmap\0"
	"__SCT__WARN_trap\0"
	"blk_finish_plug\0"
	"bdev_getblk\0"
	"submit_bio\0"
	"__ubsan_handle_out_of_bounds\0"
	"page_offset_base\0"
	"unregister_blkdev\0"
	"bio_put\0"
	"bio_endio\0"
	"__brelse\0"
	"sync_dirty_buffer\0"
	"__bh_read_batch\0"
	"set_capacity\0"
	"xa_load\0"
	"submit_bio_noacct\0"
	"bdev_fput\0"
	"random_kmalloc_seed\0"
	"setattr_copy\0"
	"vmalloc_noprof\0"
	"__wait_on_buffer\0"
	"mutex_lock\0"
	"from_kgid\0"
	"const_current_task\0"
	"wait_for_completion_io\0"
	"phys_base\0"
	"bio_add_page\0"
	"del_gendisk\0"
	"memcmp\0"
	"__bread_gfp\0"
	"__fortify_panic\0"
	"_raw_spin_unlock_irqrestore\0"
	"__folio_start_writeback\0"
	"folio_end_writeback\0"
	"from_kuid\0"
	"memset\0"
	"kern_path\0"
	"param_ops_charp\0"
	"kernel_read\0"
	"__x86_return_thunk\0"
	"make_kgid\0"
	"truncate_setsize\0"
	"jiffies\0"
	"vmemmap_base\0"
	"__ubsan_handle_shift_out_of_bounds\0"
	"vfree\0"
	"generic_read_dir\0"
	"mutex_unlock\0"
	"__register_blkdev\0"
	"init_user_ns\0"
	"xa_find\0"
	"inode_set_ctime_current\0"
	"inc_nlink\0"
	"__folio_put\0"
	"get_tree_bdev\0"
	"__kmalloc_cache_noprof\0"
	"validate_usercopy_range\0"
	"_copy_from_iter\0"
	"filemap_dirty_folio\0"
	"file_bdev\0"
	"errseq_set\0"
	"xa_find_after\0"
	"d_parent_ino\0"
	"filemap_write_and_wait_range\0"
	"__ubsan_handle_load_invalid_value\0"
	"strlen\0"
	"kvfree\0"
	"__mark_inode_dirty\0"
	"generic_file_llseek\0"
	"bio_alloc_bioset\0"
	"blk_status_to_errno\0"
	"__kvmalloc_node_noprof\0"
	"submit_bio_wait\0"
	"drop_nlink\0"
	"__SCT__might_resched\0"
	"kmalloc_caches\0"
	"fs_bio_set\0"
	"mutex_init_generic\0"
	"bio_associate_blkg\0"
	"__blk_alloc_disk\0"
	"inode_init_owner\0"
	"is_vmalloc_addr\0"
	"setattr_prepare\0"
	"iget_locked\0"
	"bdev_file_open_by_path\0"
	"clear_nlink\0"
	"__check_object_size\0"
	"d_instantiate\0"
	"_copy_to_iter\0"
	"blk_start_plug\0"
	"clear_inode\0"
	"__kmalloc_noprof\0"
	"folio_unlock\0"
	"vmalloc_to_page\0"
	"memmove\0"
	"device_add_disk\0"
	"complete\0"
	"unregister_filesystem\0"
	"mark_buffer_dirty\0"
	"__init_swait_queue_head\0"
	"d_make_root\0"
	"__x86_indirect_thunk_r15\0"
	"write_dirty_buffer\0"
	"d_splice_alias\0"
	"memcpy\0"
	"ihold\0"
	"kfree\0"
	"invalidate_inode_pages2_range\0"
	"iput\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "DA471FFADF6E6552F781A84");
