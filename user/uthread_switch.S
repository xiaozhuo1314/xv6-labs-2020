	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	/* 由于其他函数在调用thread_switch的时候已经保存了caller需要保存的寄存器
	 * 所以这里只需要保存callee需要保存的寄存器即可
	 * 而且传递过来两个参数,一个是新的线程的所保存的寄存器地址,一个是旧的线程的所保存的寄存器地址
	 * 现保存旧的,再加载新的
	 */
	sd ra, 0(a1)
	sd sp, 8(a1)
	sd s0, 16(a1)
	sd s1, 24(a1)
	sd s2, 32(a1)
	sd s3, 40(a1)
	sd s4, 48(a1)
	sd s5, 56(a1)
	sd s6, 64(a1)
	sd s7, 72(a1)
	sd s8, 80(a1)
	sd s9, 88(a1)
	sd s10, 96(a1)
	sd s11, 104(a1)

	ld ra, 0(a0)
	ld sp, 8(a0)
	ld s0, 16(a0)
	ld s1, 24(a0)
	ld s2, 32(a0)
	ld s3, 40(a0)
	ld s4, 48(a0)
	ld s5, 56(a0)
	ld s6, 64(a0)
	ld s7, 72(a0)
	ld s8, 80(a0)
	ld s9, 88(a0)
	ld s10, 96(a0)
	ld s11, 104(a0)
	ret    /* return to ra */
