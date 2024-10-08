#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "payload_bin.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

FILE *romfile;
FILE *outfile;
uint32_t romsize;
uint8_t rom[0x02000000];
char signature[] = "<3 from ChisBreadRumble";

int pause_exit(int argc, char **argv)
{
    if (argc <= 2)
    {
		scanf("%*s");
        return 1;
    }
    return 1;
}
//BL xx 跳转指令的机器码，
//address：执行这个 BL xx 指令时候，处于哪个地址
//entry：跳转的目标地址，就是xx
unsigned int NE_MakeBLmachineCode2(unsigned int address, unsigned int entry)
{ 
	unsigned int offset, imm32, low, high;
	offset = ( entry - address - 4 ) & 0x007fffff;
 
	high = 0xF000 | offset >> 12;
	low = 0xF800 | (offset & 0x00000fff) >> 1;
 
	imm32 = (low << 16) | high;
 
	return imm32;
}

static uint8_t *memfind(uint8_t *haystack, size_t haystack_size, uint8_t *needle, size_t needle_size, int stride)
{
    for (size_t i = 0; i < haystack_size - needle_size; i += stride)
    {
        if (!memcmp(haystack + i, needle, needle_size))
        {
            return haystack + i;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("Wrong number of args. Try dragging and dropping your ROM onto the .exe file in the file browser.");
        return pause_exit(argc, argv);
    }
	
	memset(rom, 0x00ff, sizeof rom);
    
    size_t romfilename_len = strlen(argv[1]);
    if (romfilename_len < 4 || strcasecmp(argv[1] + romfilename_len - 4, ".gba"))
    {
        puts("File does not have .gba extension.");
        return pause_exit(argc, argv);
    }

    // Open ROM file
    if (!(romfile = fopen(argv[1], "rb")))
    {
        puts("Could not open input file");
        puts(strerror(errno));
        return pause_exit(argc, argv);
    }

    // Load ROM into memory
    fseek(romfile, 0, SEEK_END);
    romsize = ftell(romfile);

    if (romsize > sizeof rom)
    {
        puts("ROM too large - not a GBA ROM?");
        return pause_exit(argc, argv);
    }

    if (romsize & 0x3ffff)
    {
		puts("ROM has been trimmed and is misaligned. Padding to 256KB alignment");
		romsize &= ~0x3ffff;
		romsize += 0x40000;
    }

    fseek(romfile, 0, SEEK_SET);
    fread(rom, 1, romsize, romfile);

    // Check if ROM already patched.
    if (memfind(rom, romsize, signature, sizeof signature - 1, 4))
    {
        puts("Signature found. ROM already patched!");
        return pause_exit(argc, argv);
    }
    
    // puts("Is the payload THUMB? (y/n)");
    int is_thumb = 1;
    // char thumb[32];
    // scanf("%s", thumb);
    // if (thumb[0] != 'n')
    // {
    //     is_thumb = 1;
    // }
    
    puts("Enter the address of the ROM you want to patch(e.g. 0x08000000):");
    puts("Enter 'q' to finish:");
    uint32_t rom_addr_to_patch[1024] = {0}; // 0存储地址个数
    int i = 0;
    while (1)
    {
        char addr[32];
        scanf("%s", addr);
        if (addr[0] == 'q')
        {
            break;
        }
        rom_addr_to_patch[i + 1] = strtol(addr, NULL, 16);
        if (rom_addr_to_patch[i + 1] >= 0x08000000) {
            rom_addr_to_patch[i + 1] -= 0x08000000;
        }
        i++;
    }
    rom_addr_to_patch[0] = i;
    // expend payload
    unsigned char* new_payload_bin = payload_bin;
    unsigned int new_payload_bin_len = payload_bin_len;
    new_payload_bin_len = rom_addr_to_patch[0] * payload_bin_len;
    new_payload_bin = (unsigned char *)malloc(new_payload_bin_len);
    // init new_payload_bin
    for (int i = 0; i < rom_addr_to_patch[0]; i++)
    {
        memcpy(new_payload_bin + i * payload_bin_len, payload_bin, payload_bin_len);
        printf("Patching thumb address %x\n", rom_addr_to_patch[i + 1]);
        memcpy(new_payload_bin + i * payload_bin_len, rom + rom_addr_to_patch[i + 1], 4);
    }
    // Find a location to insert the payload immediately before a 0x1000 byte sector
	int payload_base, last_payload_base;
    for (payload_base = romsize - 0x1000 - new_payload_bin_len; payload_base >= 0; payload_base -= 0x1000)
    {
        int is_all_zeroes = 1;
        int is_all_ones = 1;
        for (int i = 0; i < 0x1000 + new_payload_bin_len; ++i)
        {
            if (rom[payload_base+i] != 0)
            {
                is_all_zeroes = 0;
            }
            if (rom[payload_base+i] != 0xFF)
            {
                is_all_ones = 0;
            }
        }
        if (is_all_zeroes || is_all_ones)
        {
            last_payload_base = payload_base;
		}
    }
    payload_base = last_payload_base;
	if (payload_base < 0)
	{
		puts("ROM too small to install payload.");
		if (romsize + 0x2000 > 0x2000000)
		{
			puts("ROM alraedy max size. Cannot expand. Cannot install payload");
            scanf("%*s");
			return 1;
		}
		else
		{
			puts("Expanding ROM");
			romsize += 0x2000;
			payload_base = romsize - 0x1000 - new_payload_bin_len;
		}
	}
	for (int i = 0; i < rom_addr_to_patch[0]; i++)
    {
        // patch rom
        uint32_t patch_base = payload_base + i * payload_bin_len;
        uint32_t machineCode;
        if (!is_thumb) {
            /*
            当前地址0000D11C，目的地址00021888，偏移值=（00021888-（0000D11C+8））/4=0x0051D9
            */
            uint32_t offset = (patch_base- rom_addr_to_patch[i + 1] - 8) / 4;
            machineCode = 0xEB000000 | offset;
        }
        else 
        {
            machineCode = NE_MakeBLmachineCode2(rom_addr_to_patch[i + 1]+0x08000000, patch_base+0x08000000);
        }
        printf("Patching thumb address %x to jump to %x with machine code %x\n", rom_addr_to_patch[i + 1], patch_base, machineCode);
        rom[rom_addr_to_patch[i + 1]] = machineCode & 0xFF;
        rom[rom_addr_to_patch[i + 1] + 1] = (machineCode >> 8) & 0xFF;
        rom[rom_addr_to_patch[i + 1] + 2] = (machineCode >> 16) & 0xFF;
        rom[rom_addr_to_patch[i + 1] + 3] = (machineCode >> 24) & 0xFF;
    } 
	printf("Installing payload at offset %x\n", payload_base);
	memcpy(rom + payload_base, new_payload_bin, new_payload_bin_len);
    

	// Patch the ROM entrypoint to init sram and the dummy IRQ handler, and tell the new entrypoint where the old one was.
	if (rom[3] != 0xea)
	{
		puts("Unexpected entrypoint instruction");
		scanf("%*s");
		return 1;
	}
	

	// Flush all changes to new file
    char *suffix = "_rumble.gba";
    size_t suffix_length = strlen(suffix);
    char new_filename[FILENAME_MAX];
    strncpy(new_filename, argv[1], FILENAME_MAX);
    strncpy(new_filename + romfilename_len - 4, suffix, strlen(suffix));
    
    if (!(outfile = fopen(new_filename, "wb")))
    {
        puts("Could not open output file");
        puts(strerror(errno));
        return pause_exit(argc, argv);
    }
    
    fwrite(rom, 1, romsize, outfile);
    fflush(outfile);

    printf("Patched successfully. Changes written to %s\n", new_filename);
    pause_exit(argc, argv);
	return 0;
	
}
