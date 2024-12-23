obj-$(CONFIG_VKERNEL_DRIVER) += vkernel.o

ccflags-y := -I$(srctree)/drivers/vkernel/include

vkernel-y := vkernel_main.o syscall.o
vkernel-y += fs/acl.o
vkernel-y += security/capability.o
vkernel-y += sysctl/fs.o sysctl/kernel.o sysctl/net.o sysctl/vm.o sysctl/raw.o
vkernel-y += utils/kallsyms.o
