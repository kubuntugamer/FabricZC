savedcmd_fabriczc_mod.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o fabriczc_mod.o @fabriczc_mod.mod  ; /usr/src/linux-headers-7.0.0-34-generic/tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --retpoline --rethunk --sls --stackval --static-call --uaccess --prefix=16  --link  --module fabriczc_mod.o

fabriczc_mod.o: $(wildcard /usr/src/linux-headers-7.0.0-34-generic/tools/objtool/objtool)
