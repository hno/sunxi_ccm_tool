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

enum PLL5_GET_MODE {
	PLL5_GET_MODE_DRAM,
	PLL5_GET_MODE_OTHER
};

long get_pll5_inner(__ccmu_reg_list_t * ccmu, enum PLL5_GET_MODE mode)
{
	int M, K, N, P;
	__ccmu_pll5_ddr_reg0020_t *pll = &ccmu->Pll5Ctl;
	M = pll->FactorM + 1;
	K = pll->FactorK + 1;
	N = pll->FactorN;
	P = 1 << pll->PLLDivP;
	switch(mode) {
	case PLL5_GET_MODE_DRAM:
		return (24000000 * N * K) / M;
	case PLL5_GET_MODE_OTHER:
		return (24000000 * N * K) / P;
	default: fprintf(stderr, "INTERNAL ERROR!\n"); exit(1);
	}
}

long get_dram(__ccmu_reg_list_t * ccmu)
{
	return get_pll5_inner(ccmu, PLL5_GET_MODE_DRAM);
}

long get_pll5(__ccmu_reg_list_t * ccmu)
{
	return get_pll5_inner(ccmu, PLL5_GET_MODE_OTHER);
}

enum PLL6_GET_MODE {
	PLL6_GET_MODE_SATA,
	PLL6_GET_MODE_PLL6, /* PLL6 */
	PLL6_GET_MODE_PLL62, /* PLL6*2 */
	PLL6_GET_MODE_200,
};

long get_pll6_inner(__ccmu_reg_list_t * ccmu, enum PLL6_GET_MODE mode)
{
	int M, K, N;
	__ccmu_pll6_sata_reg0028_t *pll = &ccmu->Pll6Ctl;
	M = pll->FactorM + 1;
	K = pll->FactorK + 1;
	N = pll->FactorN;
	switch(mode) {
	case PLL6_GET_MODE_SATA:
		return (24000000 * N * K) / M / 6;
	case PLL6_GET_MODE_PLL6:
		return (24000000 * N * K) / 2;
	case PLL6_GET_MODE_PLL62:
		return (24000000 * N * K);
	case PLL6_GET_MODE_200:
		return (200000000); /* This should be derived somehow */
	default: fprintf(stderr, "INTERNAL ERROR!\n"); exit(1);
	}
}

long get_sata(__ccmu_reg_list_t * ccmu)
{
	return get_pll6_inner(ccmu, PLL6_GET_MODE_SATA);
}

long get_pll6(__ccmu_reg_list_t * ccmu)
{
	return get_pll6_inner(ccmu, PLL6_GET_MODE_PLL6);
}

long get_pll62(__ccmu_reg_list_t * ccmu)
{
	return get_pll6_inner(ccmu, PLL6_GET_MODE_PLL62);
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

long get_mbus(__ccmu_reg_list_t * ccmu)
{
	__ccmu_mbus_clk_reg015c_t *mbus = &ccmu->MBusClk;
	long src;
	switch ( mbus->ClkSrc ) {
	case 0: src = 24000000; break;
	case 1: src = get_pll62(ccmu); break;
	case 2: src = get_pll5(ccmu); break;
	default: fprintf(stderr, "Unhandled MBUSClkSrc(%d)\n", mbus->ClkSrc); exit(1);
	}
	int N = 1 << mbus->ClkDivN;
	int M = mbus->ClkDivM + 1;
	return src / N / M;
}

int main(int argc, char **argv)
{
	__ccmu_reg_list_t ccmu;
	char *ccm = mmap_io(0x1c20000, sizeof(ccmu));
	memcpy(&ccmu, ccm, sizeof(ccmu));
	printf("PLL1CTL   : %08x\n", *(__u32 *)&ccmu.Pll1Ctl);
	printf("CPU       : %7.2f MHz\n", get_cpu(&ccmu) / 1000000.0);
	printf("SYSCLKDIV : %08x\n", *(__u32 *)&ccmu.SysClkDiv);
	printf("ATB       : %7.2f MHz\n", get_atb(&ccmu) / 1000000.0);
	printf("AXI       : %7.2f MHz\n", get_axi(&ccmu) / 1000000.0);
	printf("AHB       : %7.2f MHz\n", get_ahb(&ccmu) / 1000000.0);
	printf("APB0      : %7.2f MHz\n", get_apb0(&ccmu) / 1000000.0);
	printf("APB1CLKDIV: %08x\n", *(__u32 *)&ccmu.Apb1ClkDiv);
	printf("APB1      : %7.2f MHz\n", get_apb1(&ccmu) / 1000000.0);
	printf("DRAM      : %7.2f MHz\n", get_dram(&ccmu) / 1000000.0);
	printf("PLL5CTL   : %08x\n", *(__u32 *)&ccmu.Pll5Ctl);
	printf("PLL5      : %7.2f MHz\n", get_pll5(&ccmu) / 1000000.0);
	printf("SATA      : %7.2f MHz\n", get_sata(&ccmu) / 1000000.0);
	printf("PLL6      : %7.2f MHz\n", get_pll6(&ccmu) / 1000000.0);
	printf("PLL62     : %7.2f MHz\n", get_pll62(&ccmu) / 1000000.0);
	printf("MBUSCLK   : %08x\n", *(__u32 *)&ccmu.MBusClk);
	printf("MBUS      : %7.2f MHz\n", get_mbus(&ccmu) / 1000000.0);
	return 0;
}
