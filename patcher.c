#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_bin.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

FILE *romfile;
FILE *outfile;
uint32_t romsize;
uint8_t rom[0x02000000];
char signature[] = "<3 from ChisBread";

enum payload_offsets {
    ORIGINAL_ENTRYPOINT_ADDR,
    PATCHED_ENTRYPOINT
};

static unsigned short m_d_ids[] = {0xBFD4, 0x1F3D, 0xC21C, 0x321B, 0xC209, 0x6213, 0xBF5B, 0xFFFF};
static char suffixes[8][16] = {"_BFD4.gba", "_1F3D.gba", "_C21C.gba", "_321B.gba", "_C209.gba", "_6213.gba", "_BF5B.gba", "_FFFF.gba"};
static unsigned char mov_mid[] = { 0xbf, 0x40, 0xa0, 0xe3 }; // mov r4, #0xBF
static unsigned char mov_did[] = { 0xd4, 0x40, 0xa0, 0xe3 }; // mov r4, #0xD4

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
    if (argc != 2)
    {
        puts("Wrong number of args. Try dragging and dropping your ROM onto the .exe file in the file browser.");
		scanf("%*s");
        return 1;
    }
	
	memset(rom, 0x00ff, sizeof rom);
    
    size_t romfilename_len = strlen(argv[1]);
    if (romfilename_len < 4 || strcasecmp(argv[1] + romfilename_len - 4, ".gba"))
    {
        puts("File does not have .gba extension.");
		scanf("%*s");
        return 1;
    }

    // Open ROM file
    if (!(romfile = fopen(argv[1], "rb")))
    {
        puts("Could not open input file");
        puts(strerror(errno));
		scanf("%*s");
        return 1;
    }

    // Load ROM into memory
    fseek(romfile, 0, SEEK_END);
    romsize = ftell(romfile);

    if (romsize > sizeof rom)
    {
        puts("ROM too large - not a GBA ROM?");
		scanf("%*s");
        return 1;
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
		scanf("%*s");
        return 1;
    }

    puts("Enter 1~8 to select the manufacturer and device ID of the flash chip in your cart:");
    puts("1. 0xBFD4:'SST 39VF512");
    puts("2. 0x1F3D:'Atmel AT29LV512");
    puts("3. 0xC21C:'Macronix MX29L512");
    puts("4. 0x321B:'Panasonic MN63F805MNP");
    puts("5. 0xC209:'Macronix MX29L010");
    puts("6. 0x6213:'SANYO LE26FV10N1TS");
    puts("7. 0xBF5B:'Unlicensed SST49LF080A");
    puts("8. 0xFFFF:'Unlicensed 0xFFFF");
    int manufacturer_device_id_num;
    scanf("%d", &manufacturer_device_id_num);
    if (manufacturer_device_id_num < 1 || manufacturer_device_id_num > 8)
    {
        puts("Invalid selection.");
        scanf("%*s");
        return 1;
    }
    printf("Selected manufacturer and device ID: %04X\n", m_d_ids[manufacturer_device_id_num - 1]);
    unsigned short selected_manufacturer_device_id = m_d_ids[manufacturer_device_id_num - 1];
    // find mov_mid in payload_bin
    uint8_t *mid_ptr = memfind(payload_bin, payload_bin_len, mov_mid, sizeof mov_mid, 4);
    if (!mid_ptr)
    {
        puts("Could not find manufacturer ID in payload");
        return 1;
    }
    // replace manufacturer ID
    mid_ptr[0] = (uint8_t)((selected_manufacturer_device_id >> 8) & 0xFF);
    // find mov_did in payload_bin
    uint8_t *did_ptr = memfind(payload_bin, payload_bin_len, mov_did, sizeof mov_did, 4);
    if (!did_ptr)
    {
        puts("Could not find device ID in payload");
        return 1;
    }
    // replace device ID
    did_ptr[0] = (uint8_t)(selected_manufacturer_device_id & 0xFF);
    

    // Find a location to insert the payload immediately before a 0x10000 byte sector
	int payload_base;
    for (payload_base = romsize - 0x10000 - payload_bin_len; payload_base >= 0; payload_base -= 0x10000)
    {
        int is_all_zeroes = 1;
        int is_all_ones = 1;
        for (int i = 0; i < 0x10000 + payload_bin_len; ++i)
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
           break;
		}
    }
	if (payload_base < 0)
	{
		puts("ROM too small to install payload.");
		if (romsize + 0x20000 > 0x2000000)
		{
			puts("ROM alraedy max size. Cannot expand. Cannot install payload");
            scanf("%*s");
			return 1;
		}
		else
		{
			puts("Expanding ROM");
			romsize += 0x20000;
			payload_base = romsize - 0x10000 - payload_bin_len;
		}
	}
	
	printf("Installing payload at offset %x\n", payload_base);
	memcpy(rom + payload_base, payload_bin, payload_bin_len);
    

	// Patch the ROM entrypoint to init sram and the dummy IRQ handler, and tell the new entrypoint where the old one was.
	if (rom[3] != 0xea)
	{
		puts("Unexpected entrypoint instruction");
		scanf("%*s");
		return 1;
	}
	unsigned long original_entrypoint_offset = rom[0];
	original_entrypoint_offset |= rom[1] << 8;
	original_entrypoint_offset |= rom[2] << 16;
	unsigned long original_entrypoint_address = 0x08000000 + 8 + (original_entrypoint_offset << 2);
	printf("Original offset was %lx, original entrypoint was %lx\n", original_entrypoint_offset, original_entrypoint_address);
	// little endian assumed, deal with it
    
	ORIGINAL_ENTRYPOINT_ADDR[(uint32_t*) &rom[payload_base]] = original_entrypoint_address;

	unsigned long new_entrypoint_address = 0x08000000 + payload_base + PATCHED_ENTRYPOINT[(uint32_t*) payload_bin];
	0[(uint32_t*) rom] = 0xea000000 | (new_entrypoint_address - 0x08000008) >> 2;

	// Flush all changes to new file
    char *suffix = suffixes[manufacturer_device_id_num - 1];
    size_t suffix_length = strlen(suffix);
    char new_filename[FILENAME_MAX];
    strncpy(new_filename, argv[1], FILENAME_MAX);
    strncpy(new_filename + romfilename_len - 4, suffix, strlen(suffix));
    
    if (!(outfile = fopen(new_filename, "wb")))
    {
        puts("Could not open output file");
        puts(strerror(errno));
		scanf("%*s");
        return 1;
    }
    
    fwrite(rom, 1, romsize, outfile);
    fflush(outfile);

    printf("Patched successfully. Changes written to %s\n", new_filename);
    scanf("%*s");
	return 0;
	
}
