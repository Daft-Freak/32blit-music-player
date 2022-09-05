// stub for libvgm's CPConv
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libvgm/stdtype.h"
#include "libvgm/utils/StrUtils.h"

struct _codepage_conversion
{
};

UINT8 CPConv_Init(CPCONV** retCPC, const char* cpFrom, const char* cpTo)
{
    if(strcmp(cpFrom, "UTF-16LE") == 0 && strcmp(cpTo, "UTF-8") == 0)
    {
    	CPCONV* cpc = (CPCONV*)calloc(1, sizeof(CPCONV));
	    if (cpc == NULL)
		    return 0xFF;

	    *retCPC = cpc;
	    return 0x00;
    }

    return 0x80;
}

void CPConv_Deinit(CPCONV* cpc)
{
    free(cpc);
}

UINT8 CPConv_StrConvert(CPCONV* cpc, size_t* outSize, char** outStr, size_t inSize, const char* inStr)
{
    if(!inSize || *outStr)
        return 1;

    // replace non-ascii with ?

    *outStr = malloc(inSize / 2);
    *outSize = inSize / 2;

    char *outP = *outStr;
    uint16_t *inP = (uint16_t *)inStr;

    for(size_t i = 0; i < inSize / 2; i++)
    {
        int v = *inP++;
        *outP++ = v >= 0x7F ? '?' : v;
    }

    return 0;
}
