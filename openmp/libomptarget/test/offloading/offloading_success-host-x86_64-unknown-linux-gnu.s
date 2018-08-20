	.text
	.file	"offloading_success.cpp"
	.globl	main                    # -- Begin function main
	.p2align	4, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:
	subq	$40, %rsp
	.cfi_def_cfa_offset 48
	movl	$0, 20(%rsp)
	leaq	20(%rsp), %rax
	movq	%rax, 32(%rsp)
	movq	%rax, 24(%rsp)
	movq	$.L.offload_maptypes, (%rsp)
	leaq	32(%rsp), %rcx
	leaq	24(%rsp), %r8
	movq	$-1, %rdi
	movl	$.L.omp_offload.region_id, %esi
	movl	$1, %edx
	movl	$.L.offload_sizes, %r9d
	callq	__tgt_target
	testl	%eax, %eax
	je	.LBB0_2
# %bb.1:                                # %.thread1
	movl	$1, 20(%rsp)
	movl	$.L.str.2, %esi
	jmp	.LBB0_5
.LBB0_2:
	movl	20(%rsp), %esi
	testl	%esi, %esi
	jns	.LBB0_4
# %bb.3:
	movl	$.L.str, %edi
	xorl	%eax, %eax
	callq	printf
	movl	20(%rsp), %esi
.LBB0_4:
	testl	%esi, %esi
	movl	$.L.str.3, %eax
	movl	$.L.str.2, %esi
	cmoveq	%rax, %rsi
.LBB0_5:
	movl	$.L.str.1, %edi
	xorl	%eax, %eax
	callq	printf
	movl	20(%rsp), %eax
	addq	$40, %rsp
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.section	.text.startup,"axG",@progbits,.omp_offloading.descriptor_reg,comdat
	.p2align	4, 0x90         # -- Begin function .omp_offloading.descriptor_unreg
	.type	.omp_offloading.descriptor_unreg,@function
.omp_offloading.descriptor_unreg:       # @.omp_offloading.descriptor_unreg
	.cfi_startproc
# %bb.0:
	movl	$.omp_offloading.descriptor, %edi
	jmp	__tgt_unregister_lib    # TAILCALL
.Lfunc_end1:
	.size	.omp_offloading.descriptor_unreg, .Lfunc_end1-.omp_offloading.descriptor_unreg
	.cfi_endproc
                                        # -- End function
	.hidden	.omp_offloading.descriptor_reg # -- Begin function .omp_offloading.descriptor_reg
	.weak	.omp_offloading.descriptor_reg
	.p2align	4, 0x90
	.type	.omp_offloading.descriptor_reg,@function
.omp_offloading.descriptor_reg:         # @.omp_offloading.descriptor_reg
	.cfi_startproc
# %bb.0:
	pushq	%rax
	.cfi_def_cfa_offset 16
	movl	$.omp_offloading.descriptor, %edi
	callq	__tgt_register_lib
	movl	$.omp_offloading.descriptor_unreg, %edi
	movl	$.omp_offloading.descriptor, %esi
	movl	$__dso_handle, %edx
	popq	%rax
	jmp	__cxa_atexit            # TAILCALL
.Lfunc_end2:
	.size	.omp_offloading.descriptor_reg, .Lfunc_end2-.omp_offloading.descriptor_reg
	.cfi_endproc
                                        # -- End function
	.type	.L.omp_offload.region_id,@object # @.omp_offload.region_id
	.section	.rodata,"a",@progbits
.L.omp_offload.region_id:
	.byte	0                       # 0x0
	.size	.L.omp_offload.region_id, 1

	.type	.L.offload_sizes,@object # @.offload_sizes
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3
.L.offload_sizes:
	.quad	4                       # 0x4
	.size	.L.offload_sizes, 8

	.type	.L.offload_maptypes,@object # @.offload_maptypes
	.p2align	3
.L.offload_maptypes:
	.quad	34                      # 0x22
	.size	.L.offload_maptypes, 8

	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"Runtime error, isHost=%d\n"
	.size	.L.str, 26

	.type	.L.str.1,@object        # @.str.1
.L.str.1:
	.asciz	"Target region executed on the %s\n"
	.size	.L.str.1, 34

	.type	.L.str.2,@object        # @.str.2
.L.str.2:
	.asciz	"host"
	.size	.L.str.2, 5

	.type	.L.str.3,@object        # @.str.3
.L.str.3:
	.asciz	"device"
	.size	.L.str.3, 7

	.type	.omp_offloading.entry_name,@object # @.omp_offloading.entry_name
	.section	.rodata.str1.16,"aMS",@progbits,1
	.p2align	4
.omp_offloading.entry_name:
	.asciz	"__omp_offloading_803_2d614ec_main_l12"
	.size	.omp_offloading.entry_name, 38

	.type	.omp_offloading.entry,@object # @.omp_offloading.entry
	.section	.omp_offloading.entries,"a",@progbits
	.globl	.omp_offloading.entry
.omp_offloading.entry:
	.quad	.L.omp_offload.region_id
	.quad	.omp_offloading.entry_name
	.quad	0                       # 0x0
	.long	0                       # 0x0
	.long	0                       # 0x0
	.size	.omp_offloading.entry, 32

	.type	.omp_offloading.device_images,@object # @.omp_offloading.device_images
	.section	.rodata..omp_offloading.device_images,"aG",@progbits,.omp_offloading.descriptor_reg,comdat
	.p2align	3
.omp_offloading.device_images:
	.quad	".omp_offloading.img_start.nvptx64-nvidia-cuda"
	.quad	".omp_offloading.img_end.nvptx64-nvidia-cuda"
	.quad	.omp_offloading.entries_begin
	.quad	.omp_offloading.entries_end
	.size	.omp_offloading.device_images, 32

	.type	.omp_offloading.descriptor,@object # @.omp_offloading.descriptor
	.section	.rodata..omp_offloading.descriptor,"aG",@progbits,.omp_offloading.descriptor_reg,comdat
	.p2align	3
.omp_offloading.descriptor:
	.long	1                       # 0x1
	.zero	4
	.quad	.omp_offloading.device_images
	.quad	.omp_offloading.entries_begin
	.quad	.omp_offloading.entries_end
	.size	.omp_offloading.descriptor, 32

	.hidden	__dso_handle
	.section	.init_array.0,"aGw",@init_array,.omp_offloading.descriptor_reg,comdat
	.p2align	3
	.quad	.omp_offloading.descriptor_reg

	.ident	"clang version 6.0.1 (http://llvm.org/git/clang.git 0e746072ed897a85b4f533ab050b9f506941a097) (http://llvm.org/git/llvm.git f1b37feef3d5f09dadf6a46fdb11fa7e4218cf6c)"
	.section	".note.GNU-stack","",@progbits
