#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef uint32_t __u32;

#define volatile
#include "ccmu_regs.h"
#undef volatile

#if 0
#define debug printf
#else
#define debug(...)
#endif

void * mmap_io(__u32 addr, size_t len)
{
	int pagesize = sysconf(_SC_PAGESIZE);
	int fd = open("/dev/mem",O_RDWR);
	int maddr = addr & ~(pagesize-1);
	int offset = addr & (pagesize-1);
	int mlen = (len + (pagesize -1) ) & ~(pagesize -1);
	if (fd == -1) {
		perror("open:");
		exit(1);
	}
	char *map = mmap(NULL, mlen, PROT_WRITE|PROT_READ, MAP_SHARED, fd, maddr);
	if (map == MAP_FAILED) {
		perror("mmap:");
		exit(1);
	}
	return map + offset;

}

long get_pll1(__ccmu_reg_list_t * ccmu)
{
	int M, K, N, P;
	__ccmu_pll1_core_reg0000_t *pll1 = &ccmu->Pll1Ctl;
	debug("PLL1_Enable      : %d\n", pll1->PLLEn);
	M = pll1->FactorM + 1;
	debug("PLL1_FACTOR_M    : %d (%d)\n", M, pll1->FactorM);
	K = pll1->FactorK + 1;
	debug("PLL1_FACTOR_K    : %d (%d)\n", K, pll1->FactorK);
	N = pll1->FactorN;
	if (N == 0)
		N = 1;
	debug("PLL1_FACTOR_N    : %d (%d)\n", N, pll1->FactorN);
	P = 1 << pll1->PLLDivP;
	debug("PLL1_OUT_EXT_DIVP: %d (%d)\n", P, pll1->PLLDivP);
	return (24000000 * N * K) / (M * P);
}

long get_cpu(__ccmu_reg_list_t * ccmu)
{
	switch (ccmu->SysClkDiv.CPUClkSrc) {
	case 0: return 32768;
	case 1: return 24000000;
	case 2: return get_pll1(ccmu);
	case 3: return 200000000; /* PLL6 derived  somehow */
	default: return 0;
	}
}

long get_axi(__ccmu_reg_list_t * ccmu)
{
	int cpu = get_cpu(ccmu);
	debug("AXI DIV: %d (%d)\n", ccmu->SysClkDiv.AXIClkDiv + 1, ccmu->SysClkDiv.AXIClkDiv);
	return cpu / (ccmu->SysClkDiv.AXIClkDiv + 1);
}

long get_ahb(__ccmu_reg_list_t * ccmu)
{
	long src;
	switch(ccmu->SysClkDiv.AHBClkSrc) {
	case 0: src = get_axi(ccmu); break;
	default: fprintf(stderr, "Unhandled AHBClkSrc(%d)\n", ccmu->SysClkDiv.AHBClkSrc); exit(1);
/*
	case 1: src = get_pll6(ccmu) / 2; break;
	case 2: src = get_pll6(ccmu); break;
	case 4: 
*/
	}
	return src / (1 << ccmu->SysClkDiv.AHBClkDiv);
}

long get_apb0(__ccmu_reg_list_t * ccmu)
{
	int div = 1 << ccmu->SysClkDiv.APB0ClkDiv;
	if (div == 1)
		div = 2;
	return get_ahb(ccmu) / div;
}

long get_atb(__ccmu_reg_list_t * ccmu)
{
	int div = 1 << ccmu->SysClkDiv.AtbApbClkDiv;
	if (div > 4)
		div = 4;
	return get_cpu(ccmu) / div;
}

long get_apb1(__ccmu_reg_list_t * ccmu)
{
	int div = (1 << ccmu->Apb1ClkDiv.PreDiv) * (ccmu->Apb1ClkDiv.ClkDiv + 1);
	long src;
	switch(ccmu->Apb1ClkDiv.ClkSrc) {
	case 0: src = 24000000; break;
	/* case 1: src = get_pll6(ccmu); break; */
	case 2: src = 32768; break;
	default: fprintf(stderr, "Unhandled APB1ClkSrc(%d)\n", ccmu->Apb1ClkDiv.ClkSrc); exit(1);
	}
	return src / div;
}

int main(int argc, char **argv)
{
	__ccmu_reg_list_t ccmu;
	char *ccm = mmap_io(0x1c20000, sizeof(ccmu));
	memcpy(&ccmu, ccm, sizeof(ccmu));
	printf("SYSCLKDIV: %08x\n", *(__u32 *)&ccmu.SysClkDiv);
	printf("CPU : %7.2f MHz\n", get_cpu(&ccmu) / 1000000.0);
	printf("ATB : %7.2f MHz\n", get_atb(&ccmu) / 1000000.0);
	printf("AXI : %7.2f MHz\n", get_axi(&ccmu) / 1000000.0);
	printf("AHB : %7.2f MHz\n", get_ahb(&ccmu) / 1000000.0);
	printf("APB0: %7.2f MHz\n", get_apb0(&ccmu) / 1000000.0);
	printf("APB1CLKDIV: %08x\n", *(__u32 *)&ccmu.Apb1ClkDiv);
	printf("APB1: %7.2f MHz\n", get_apb1(&ccmu) / 1000000.0);
}
