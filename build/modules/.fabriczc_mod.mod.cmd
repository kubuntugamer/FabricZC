savedcmd_fabriczc_mod.mod := printf '%s\n'   src/core/main.o src/core/block_dev.o src/core/power_mgmt.o src/fs/fs_main.o src/fs/metadata_io.o | awk '!x[$$0]++ { print("./"$$0) }' > fabriczc_mod.mod
