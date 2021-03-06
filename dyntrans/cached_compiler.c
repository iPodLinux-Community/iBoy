#include <string.h>
#include <stdlib.h>


#include <stdio.h>
#include "../defs.h"
#include "../mem.h"
#include "../cop.h"
#include "../fastmem.h"

#include "cached_compiler.h"
#include "compiler.h"
#include "compiler_defs.h"


#define HASH_SIZE 0x4000
#define ROM_BANKS 256		// for rombank 0 // FIXME: Wieviele wirklich?


block_exec_fp table[HASH_SIZE];

block_exec_fp *rbn_table[ROM_BANKS] = {0};

unsigned int bin_field[CASH_SIZE];
block_exec_fp bin_free = 0; // this is the first free block whithin bin_blocks

struct stored_successor patch_dummy;

struct hiram_block hiram_block[128];
unsigned int hiram_compl_mask[4];
//unsigned int hiram_compliled[4];
//unsigned int hiram_dirty_mask[4];

#if USE_TEMP_HASHING == 1
	struct temp_block temp_block[TEMP_HASH_SIZE];
#else
	unsigned int temp_field[512]; //FIXME
#endif


void ccmpl_hash_alloc_rbn(){

	if(rbn_table[mbc.rombank] != 0) {
	#ifdef USE_DEBUG
		err_msg("ALLOC USED ROMBANK", 1500);
	#endif
		return;
	}

	//FIXME is eh klasr
	if(!(rbn_table[mbc.rombank] = malloc(sizeof(block_exec_fp)*HASH_SIZE))) {
		cmpl_bailout("rbn_table out of mem\n");
	}
	memset(rbn_table[mbc.rombank], 0, sizeof(block_exec_fp)*HASH_SIZE);
}

int ccmpl_no_space()
{
	return ((unsigned int*)bin_free-bin_field) > (CASH_SIZE-MAX_BASIC_BLOCK_CYC*MAX_ARM_PER_CYC);
}


//$ arm-uclinux-elf-gcc -funit-at-a-time *.c */*.c asm/arm7tdmi/cpu.s -mcpu=arm7tdmi -mtune=arm7tdmi -O3 -fomit-frame-pointer -mlittle-endian -elf2flt -DUSE_ASM -DUSE_COP -DHAVE_CONFIG_H -DIS_LITTLE_ENDIAN -DIS_LINUX -DHAVE_USLEEP -DMEASUREMENT -mstructure-size-boundary=32 -Wall -o iboy


//$ arm-uclinux-elf-gcc -funit-at-a-time *.c */*.c asm/arm7tdmi/cpu.s -mcpu=arm7tdmi -mtune=arm7tdmi
//-O3 -fomit-frame-pointer -mlittle-endian -elf2flt -DUSE_ASM -DUSE_COP -DHAVE_CONFIG_H -DIS_LITTLE_E
//NDIAN -DIS_LINUX -DHAVE_USLEEP -DMEASUREMENT -mstructure-size-boundary=32 -Wall -o iboy


#if USE_TEMP_HASHING == 1

inline unsigned int compute_crc(int pc, unsigned int len) {
	unsigned int ret=0, temp;

	while(len--) {
		temp = readb(pc++);

		//ret ^= temp; // use X-OR + Rotate to get 32 Bit Checksum
		asm ("eor %0,%1,%0, ror #8": "+r" (ret) : "r" (temp));

	}
	return ret;
}

void check_crc(int dummy, int pc) ;

void update_temp_block(int pc) {
	int temp;
	struct temp_block *cur_tmp;

	cur_tmp = &temp_block[pc&TEMP_HASH_MASK];
	cur_tmp->from_pc = pc;

	//field[0] doesnt change after init
	//cur_tmp->field[0] =  PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&check_crc)-(int)(&cur_tmp->field[0]))/4-2 );

	temp = compile(pc, (block_exec_fp) &cur_tmp->field[1] , USE_TEMP_PATCH );

#ifdef USE_DEBUG
	if(temp >= TEMP_HASH_BLOCKSIZE){
		cop_end();
		err_msg("tempblock to fat",2000);
		cop_begin();
	}

#endif

	cur_tmp->bytes = cmpl_get_z80bytes();
	cur_tmp->crc = compute_crc(pc,cur_tmp->bytes);
}


void check_crc(int dummy, int pc) {
	unsigned int *pointer;

	//asm (pointer = LinkRegister - 12);
	asm ("sub %0, lr, #16": "=r" (pointer));

	if(pointer[0] == pc && pointer[2] == compute_crc(pointer[0], pointer[1]))
		return;

	/*cop_end();
	err_msg("%04X, %04X",50, pc, pointer[0]);
	cop_begin();*/

	update_temp_block(pc); // update the native code

	//FIXME: Flush Cache fuer linki !!

	return; // return to the new compiled code (behind the call to this function)
}
#endif

//HIRAM:
void check_hiram();
void update_hiram_block(int pc) {
	int temp;
	struct hiram_block *cur;

	cur = &hiram_block[pc&0x7f];

	cur->field[0] = PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&check_hiram)-(int)(&cur->field[0]))/4-2 );
	temp = compile(pc, (block_exec_fp) &cur->field[1] , USE_HIRAM_PATCH );

#ifdef USE_DEBUG
	if(temp >= TEMP_HASH_BLOCKSIZE){
		cop_end();
		err_msg("tempblock to fat",2000);
		cop_begin();
	}

#endif
	//cur->bytes = cmpl_get_z80bytes();

	//hiram_compliled[(pc>>5)&0x03] |= (1 << (pc & 0x1F));
	// FIXME can be done much faster
	for(temp = pc + cmpl_get_z80bytes(); pc < temp; pc++){
		hiram_compl_mask[(pc>>5)&0x03] |= (1 << (pc & 0x1F));
		hiram_dirty_mask[(pc>>5)&0x03] &= ~(1 << (pc & 0x1F));
	}
}

void check_hiram() {
	int pc;

	//asm (pointer = LinkRegister - 12);


		if((hiram_compl_mask[0] & hiram_dirty_mask[0]) ||
			(hiram_compl_mask[1] & hiram_dirty_mask[1]) ||
			(hiram_compl_mask[2] & hiram_dirty_mask[2]) ||
			(hiram_compl_mask[3] & hiram_dirty_mask[3])) {

			asm ("ldr %0, [lr, #-8]": "=r" (pc));
			ccmpl_hiram_reset();
			update_hiram_block(pc);
		}

	return; // return to the new compiled code (behind the call to this function)
}

void init_hiram() {
	int pc;

	asm ("ldr %0, [lr, #-8]": "=r" (pc));
	update_hiram_block(pc);

	return; // return to the new compiled code (behind the call to this function)
}




/*
*	look up hashtable for given PC - compiles blocks if needed
*/
block_exec_fp pc_to_native(int pc)
{
	#if USE_TEMP_HASHING == 1
	struct temp_block *cur_tmp;
	#endif

	block_exec_fp ret;
	//int temp;

	#ifdef USE_DEBUG
		if(pc<0 || pc > 0xffff) {
			cop_end();
			err_msg("largePClookup", 1200);
			err_msg("cut and continue!", 1200);
			cop_begin();
			//cmpl_bailout("Bad Parameters pc:%0x, dest:%0x", from_pc, dest);
			pc &= 0xffff;
		}
	#endif



	if(pc < 0x4000) {				// ROMBANK 0

		if((ret = table[pc]))
			return ret;

		if(ccmpl_no_space()) {
			#ifdef USE_DEBUG
			cop_end();
			err_msg("Cmpl Cache Flush", 500);
			cop_begin();
			#endif
			ccmpl_reset();
		}

		ret = table[pc] = bin_free;
		bin_free += compile(pc, bin_free, 1);
		return ret;
	} else if (pc < 0x8000) {				// ROMBANK 1 FIXME
		if(rbn_table[mbc.rombank]) {
			if((ret = rbn_table[mbc.rombank][pc-0x4000]))
				return ret;
		} else {
			ccmpl_hash_alloc_rbn();
		}

		if(ccmpl_no_space()) {
			#ifdef USE_DEBUG
			cop_end();
			err_msg("Cmpl Cache Flush", 500);
			cop_begin();
			#endif
			ccmpl_reset();
			ccmpl_hash_alloc_rbn();
		}

		ret = rbn_table[mbc.rombank][pc-0x4000] = bin_free;
		bin_free += compile(pc, bin_free, 1);
		return ret;
	}

	#if USE_HIRAM_CACHE == 1
	if(pc >= 0xff80) {				// HI-RAM

		/*if((hiram_compl_mask[0] & hiram_dirty_mask[0]) ||
			(hiram_compl_mask[1] & hiram_dirty_mask[1]) ||
			(hiram_compl_mask[2] & hiram_dirty_mask[2]) ||
			(hiram_compl_mask[3] & hiram_dirty_mask[3])) {
			ccmpl_hiram_reset();
			update_hiram_block(pc);
		}*/

		return (block_exec_fp) &hiram_block[pc&0x7F].field[0];
	}
	#endif

	// all other code

#if USE_TEMP_HASHING == 1
	cur_tmp = &(temp_block[pc&TEMP_HASH_MASK]);
	if(cur_tmp->from_pc != pc || cur_tmp->crc != compute_crc(cur_tmp->from_pc, cur_tmp->bytes))
		update_temp_block(pc);

	return (block_exec_fp) &cur_tmp->field[1];

#else

	compile(pc, (block_exec_fp) temp_field, 0);
	return (block_exec_fp) temp_field;

#endif


}


void mymess(int r0, int r1, int r2) {
	cop_end();
	err_msg("%04X %04X %04X", 1000, r0, r1, r2);
	cop_begin();

}


struct stored_successor *ccmpl_patch(unsigned int *paddr, int pc, int cascade)
{
	block_exec_fp next_native = 0;
	int must_reset=0;
	int is_spec = cascade & SPEC_JUMP;

	#if USE_SPEC_JUMP == 1
	int spec_rom1 = cascade & SPEC_JUMP_ROM1;
	int back = cascade & SPEC_PATCH_BACK;
	#endif 

	unsigned int *paddr_in=paddr;

	cascade &= 0x0f; // upper bits used for spec jump

	#ifdef USE_DEBUG
	if(pc >= 0x8000 && pc < 0xFF80) cmpl_bailout("pc out of range");
	#endif


	#if USE_SPEC_JUMP == 1
	//avoid to patch a speculative jump rom0 to rom1
	if(is_spec) {
		if((back)||(!spec_rom1 && pc > 0x4000 && pc < 0x8000)){ //FIXME
			// patch back to a normal unknown jump!
			*paddr++ = M_ADD_IMM(15,11,IMM_ROTL( (((unsigned char)(&point_of_return_lu-&Anchor))), 1 ));

			patch_dummy.pc = pc;
			patch_dummy.native_successor = 0;//FIXME not used
			return &patch_dummy;

		}
	}
	#endif

	if(pc >= 0x8000) {
		next_native = pc_to_native(pc);
	} else {
		if(pc < 0x4000) next_native = table[pc];
		else {
			if(!rbn_table[mbc.rombank]){
				ccmpl_hash_alloc_rbn();
				#ifdef USE_DEBUG
					cop_end();
					err_msg("romalloc patch",2000);
					cop_begin();
				#endif
			}
			next_native = rbn_table[mbc.rombank][pc-0x4000]; // rbn_table exists here
		}

		if(!next_native) {
			must_reset = ccmpl_no_space();
			next_native = pc_to_native(pc);
		}

		if(must_reset){ // do not patch into flushed cache
			patch_dummy.pc = pc;
			patch_dummy.native_successor = 0; //FIXME not used
			return &patch_dummy;
		}
	}

	if(is_spec) {
		//check 16Bit PC (goes to zero on match)
		*paddr++ = PM_EOR_IMM(COND_AL,1,0,1, pc & 0xff );
		*paddr++ = PM_EOR_IMM(COND_AL,1,0,0, IMM_ROTL((pc>>8) & 0xff, 4) );
		*paddr++ = PM_BRA(COND_NE,0, 3);
		//*paddr++ = PM_ADD_IMM(COND_NE,0,15,11,IMM_ROTL( (((unsigned char)(&point_of_return_lu-&Anchor))), 1 ));
	}
	if(cascade) {
		if(is_spec) { // the flag of cycles is lost at this time so a second compare is needed!?
			*paddr++ = PM_CMP_IMM(COND_AL,10, 0);
		}

		*paddr =
		PM_BRA(COND_GT,0, cmpl_check_boffset((unsigned int)(next_native)-(unsigned int)(paddr))/4-2);
		paddr++;   // fixme: better than *paddr++  ??
	}

	*paddr = PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&point_of_return)-(int)(paddr))/4-2 );
	paddr++;

	*paddr++ = pc;		// here will come pc FIXME: additional data?
	*paddr++ = (unsigned int) next_native;

	if(is_spec) { // trailer to patch back in a second patching-step
		*paddr++= PM_MOV_IMM(COND_AL,0,2, 1 | SPEC_JUMP | SPEC_PATCH_BACK);
		*paddr= PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&patch_spec_point_of_return)-(int)(paddr))/4-2 );
		paddr++;
		*paddr++= pc;
		*paddr++= (unsigned int) paddr_in;

		patch_dummy.pc = pc;
		patch_dummy.native_successor = 0;//FIXME not used
		return &patch_dummy;
	}

	#ifdef PORTABLE
	flush_cache(paddr-24, paddr);		//FIXME: this should flush the cache
	#endif


	return (struct stored_successor *)(paddr-2);
}


block_exec_fp ccmpl_cache_interrupts(int addr)
{
	int i;
	block_exec_fp ret = pc_to_native(addr);

	switch(addr) {
		case 0x40:
			for(i=1;i<32;i+=2)
				int_vec_table[i] = ret;
			break;
		case 0x48:
			for(i=2;i<32;i+=4)
				int_vec_table[i] = ret;
			break;
		case 0x50:
			for(i=4;i<32;i+=8)
				int_vec_table[i] = ret;
			break;
		case 0x58:
				int_vec_table[8] = int_vec_table[24] = ret;
			break;
		case 0x60:
				int_vec_table[16] = ret;
			break;
		default:
			//FIXME
			cmpl_bailout("invalid interrupt - never");
			break;
	}
	return ret;
}


void ccmpl_init_interrupts()
{
	int i;

	for(i=1;i<32;i+=2)
		int_vec_table[i] = int_0x40;
	for(i=2;i<32;i+=4)
		int_vec_table[i] = int_0x48;
	for(i=4;i<32;i+=8)
		int_vec_table[i] = int_0x50;

	int_vec_table[8] = int_vec_table[24] = int_0x58;
	int_vec_table[16] = int_0x60;
}

void ccmpl_hiram_reset()
{
	int i;

	#ifdef USE_DEBUG
	cop_end();
	err_msg("HIRAM flush", 250);
	cop_begin();
	#endif

	for(i = 0; i < 128; i++) {
		hiram_block[i].pc = 0xFF80+i;
		hiram_block[i].field[0] = PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&init_hiram)-(int)(&hiram_block[i].field[0]))/4-2 );
	}

	for(i = 0; i < 4; i++)
		hiram_compl_mask[i] = hiram_dirty_mask[i] = 0;

}

void ccmpl_reset()
{
	int i=0;

	//reset cached interrupthanders
	ccmpl_init_interrupts();

	ccmpl_hiram_reset();

#if USE_TEMP_HASHING == 1
	for(i = 0; i < TEMP_HASH_SIZE; i++) {
		temp_block[i].from_pc = -100;
		temp_block[i].field[0] = PM_BRA(COND_AL,1, cmpl_check_boffset((int)(&check_crc)-(int)(&temp_block[i].field[0]))/4-2 );
	}
#endif

	for(i = 0; i < HASH_SIZE; i++) {//set all to unknown
		table[i] = 0;
	}

	for(i = 0; i < ROM_BANKS; i++) { //set all to unknown
		if(rbn_table[i]) free(rbn_table[i]);
		rbn_table[i] = 0;
	}

	bin_free = (block_exec_fp) bin_field;

	//mmap(100000, 100000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	//if(!bin_blocks) die("compiler memunderrun");
}
