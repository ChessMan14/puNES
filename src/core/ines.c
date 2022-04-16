/*
 *  Copyright (C) 2010-2022 Fabio Cavallo (aka FHorse)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ines.h"
#include "rom_mem.h"
#include "fds.h"
#include "mem_map.h"
#include "mappers.h"
#include "emu.h"
#include "conf.h"
#include "clock.h"
#include "cheat.h"
#include "info.h"
#include "vs_system.h"
#include "patcher.h"
#include "sha1.h"
#include "database.h"

void search_in_database(void);
BYTE ines10_search_in_database(void *rom_mem);
void nes20_submapper(void);
void nes20_prg_chr_size(WORD *reg1, WORD *reg2, float divider);
BYTE nes20_ram_size(BYTE mode);

_ines ines;

BYTE ines_load_rom(void) {
	_rom_mem rom;
	BYTE tmp;

	{
		static const uTCHAR rom_ext[2][10] = { uL(".nes\0"), uL(".NES\0") };
		BYTE i, found = TRUE;
		FILE *fp;

		fp = ufopen(info.rom.file, uL("rb"));

		if (!fp) {
			found = FALSE;

			for (i = 0; i < LENGTH(rom_ext); i++) {
				uTCHAR rom_file[LENGTH_FILE_NAME_LONG];

				umemset(rom_file, 0x00, usizeof(rom_file));
				umemcpy(rom_file, info.rom.file, usizeof(rom_file) - 10 - 1);
				ustrcat(rom_file, rom_ext[i]);

				fp = ufopen(rom_file, uL("rb"));

				if (fp) {
					ustrncpy(info.rom.file, rom_file, usizeof(info.rom.file));
					found = TRUE;
					break;
				}
			}
		}

		if (!found) {
			return (EXIT_ERROR);
		}

		fseek(fp, 0L, SEEK_END);
		rom.size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);

		if ((rom.data = (BYTE *)malloc(rom.size)) == NULL) {
			fclose(fp);
			return (EXIT_ERROR);
		}

		if (fread(rom.data, 1, rom.size, fp) != rom.size) {
			fclose(fp);
			free(rom.data);
			return (EXIT_ERROR);
		}

		fclose(fp);
	}

	if (cfg->cheat_mode == GAMEGENIE_MODE) {
		gamegenie_load_rom(&rom);
	}

	patcher_apply(&rom);

	rom.position = 0;

	if ((rom.data[rom.position++] == 'N') &&
		(rom.data[rom.position++] == 'E') &&
		(rom.data[rom.position++] == 'S') &&
		(rom.data[rom.position++] == '\32')) {
		info.prg.rom[0].banks_16k = rom.data[rom.position++];
		info.chr.rom[0].banks_8k = rom.data[rom.position++];

		if (rom_mem_ctrl_memcpy(&ines.flags[0], &rom, TOTAL_FL) == EXIT_ERROR) {
			free(rom.data);
			return (EXIT_ERROR);
		}

		if ((ines.flags[FL7] & 0x0C) == 0x08) {
			// NES 2.0
			info.format = NES_2_0;

			// visto che con il NES_2_0 non eseguo la ricerca nel
			// database inizializzo queste variabili.
			info.mirroring_db = info.id = DEFAULT;
			info.extra_from_db = 0;

			info.mapper.id = ((ines.flags[FL8] & 0x0F) << 8) | (ines.flags[FL7] & 0xF0) | (ines.flags[FL6] >> 4);
			info.mapper.submapper = (ines.flags[FL8] & 0xF0) >> 4;

			// Submapper number. Mappers not using submappers set this to zero.
			if (info.mapper.submapper == 0) {
				info.mapper.submapper = DEFAULT;
			}

			info.prg.rom[0].banks_16k |= ((ines.flags[FL9] & 0x0F) << 8);
			nes20_prg_chr_size(&info.prg.rom[0].banks_16k, &info.prg.rom[0].banks_8k, 0x2000);

			info.chr.rom[0].banks_8k |= ((ines.flags[FL9] & 0xF0) << 4);
			nes20_prg_chr_size(&info.chr.rom[0].banks_8k, &info.chr.rom[0].banks_4k, 0x1000);

			info.prg.ram.banks_8k_plus = nes20_ram_size(ines.flags[FL10] & 0x0F);
			info.prg.ram.bat.banks = nes20_ram_size(ines.flags[FL10] >> 4);

			if (info.prg.ram.bat.banks && !info.prg.ram.banks_8k_plus) {
				info.prg.ram.banks_8k_plus = info.prg.ram.bat.banks;
			}

			tmp = ines.flags[FL12] & 0x01;

			vs_system.ppu = ines.flags[FL13] & 0x0F;
			vs_system.special_mode.type = (ines.flags[FL13] >> 4) & 0x0F;
		} else {
			// iNES 1.0
			info.format = iNES_1_0;

			// Older versions of the iNES emulator ignored bytes 7-15, and several ROM management tools
			// wrote messages in there. Commonly, these will be filled with "DiskDude!", which results
			// in 64 being added to the mapper number. A general rule of thumb: if the last 4 bytes are
			// not all zero, and the header is not marked for NES 2.0 format, an emulator should either
			// mask off the upper 4 bits of the mapper number or simply refuse to load the ROM.
			if (ines.flags[FL12] | ines.flags[FL13] | ines.flags[FL14] | ines.flags[FL15]) {
				info.mapper.id = ines.flags[FL6] >> 4;
				tmp = 0;
			} else {
				info.mapper.id = (ines.flags[FL7] & 0xF0) | (ines.flags[FL6] >> 4);
				tmp = ines.flags[FL9] & 0x01;
			}

			info.prg.ram.bat.banks = (ines.flags[FL6] & 0x02) >> 1;

			if (info.prg.ram.bat.banks) {
				info.prg.ram.banks_8k_plus = 1;
			}
		}

		switch (tmp) {
			case 0:
				info.machine[HEADER] = NTSC;
				break;
			case 1:
				info.machine[HEADER] = PAL;
				break;
		}

		info.trainer = ines.flags[FL6] & 0x04;

		if (ines.flags[FL6] & 0x08) {
			mirroring_FSCR();
		} else {
			if (ines.flags[FL6] & 0x01) {
				mirroring_V();
			} else {
				mirroring_H();
			}
		}

		// inizializzo qui il writeVRAM per la mapper 96 perche'
		// e' l'unica mapper che utilizza 32k di CHR Ram e che
		// si permette anche il lusso di swappare. Quindi imposto
		// a FALSE qui in modo da poter cambiare impostazione nel
		// ines10_search_in_database().
		mapper.write_vram = FALSE;

		if ((info.format != NES_2_0) && ines10_search_in_database(&rom)) {
			free(rom.data);
			return (EXIT_ERROR);
		}

		// gestione Vs. System
		if ((info.mapper.id != 99) && !vs_system.ppu && !vs_system.special_mode.type) {
			vs_system.enabled = FALSE;
			vs_system.special_mode.r5e0x = NULL;
			vs_system.special_mode.index = 0;
			vs_system.rc2c05.enabled = FALSE;
		} else {
			vs_system.enabled = TRUE;

			switch (vs_system.ppu) {
				case RP2C03B:
				case RP2C03G:
				case RP2C04:
				case RP2C04_0002:
				case RP2C04_0003:
				case RP2C04_0004:
				case RC2C03B:
				case RC2C03C:
				default:
					vs_system.rc2c05.enabled = FALSE;
					vs_system.rc2c05.r2002 = 0x00;
					break;
				case RC2C05_01:
					vs_system.rc2c05.enabled = TRUE;
					vs_system.rc2c05.r2002 = 0x1B;
					break;
				case RC2C05_02:
					vs_system.rc2c05.enabled = TRUE;
					vs_system.rc2c05.r2002 = 0x3D;
					break;
				case RC2C05_03:
					vs_system.rc2c05.enabled = TRUE;
					vs_system.rc2c05.r2002 = 0x1C;
					break;
				case RC2C05_04:
					vs_system.rc2c05.enabled = TRUE;
					vs_system.rc2c05.r2002 = 0x1B;
					break;
				case RC2C05_05:
					vs_system.rc2c05.enabled = TRUE;
					vs_system.rc2c05.r2002 = 0x00;
					break;
			}

			switch (vs_system.special_mode.type) {
				case VS_SM_Normal:
				default:
					vs_system.special_mode.r5e0x = NULL;
					break;
				case VS_SM_RBI_Baseball:
					vs_system.special_mode.r5e0x = (BYTE *)&vs_protection_data[1][0];
					break;
				case VS_SM_TKO_Boxing:
					vs_system.special_mode.r5e0x = (BYTE *)&vs_protection_data[0][0];
					break;
				case VS_SM_Super_Xevious:
					vs_system.special_mode.r5e0x = NULL;
					break;
			}
			vs_system.special_mode.index = 0;
		}

		if (info.trainer) {
			if (rom_mem_ctrl_memcpy(&trainer.data, &rom, sizeof(trainer.data)) == EXIT_ERROR) { 
				free(rom.data);
				return (EXIT_ERROR);
			}
		} else {
			memset(&trainer.data, 0x00, sizeof(trainer.data));
		}

		if (!info.chr.rom[0].banks_8k) {
			mapper.write_vram = TRUE;
			if (info.format == NES_2_0) {
				info.chr.rom[0].banks_8k = nes20_ram_size(ines.flags[FL11] & 0x0F);
			}
		}

		info.prg.rom[0].banks_8k = !info.prg.rom[0].banks_16k ? 1 : info.prg.rom[0].banks_16k * 2;
		info.prg.chips = info.chr.chips = 0;
		map_set_banks_max_prg(0);

		// alloco la PRG Ram
		if (map_prg_ram_malloc(0x2000) != EXIT_OK) {
			free(rom.data);
			return (EXIT_ERROR);
		}

		// alloco e carico la PRG Rom
		if (map_prg_chip_malloc(0, info.prg.rom[0].banks_8k * 0x2000, 0x00) == EXIT_ERROR) {
			free(rom.data);
			return (EXIT_ERROR);
		}

		if (rom_mem_ctrl_memcpy(prg_chip(0), &rom, info.prg.rom[0].banks_8k * 0x2000) == EXIT_ERROR) {
			free(rom.data);
			return (EXIT_ERROR);
		}

		if (info.format == NES_2_0) {
			sha1_csum(prg_chip(0), info.prg.rom[0].banks_8k * 0x2000, info.sha1sum.prg.value, info.sha1sum.prg.string, LOWER);
		}

		// se e' settato mapper.write_vram, vuol dire
		// che la rom non ha CHR Rom e che quindi la CHR Ram
		// la trattero' nell'inizializzazione della mapper
		// (perche' alcune mapper ne hanno 16k, altre 8k).
		if (mapper.write_vram == FALSE) {
			// alloco la CHR Rom
			if (map_chr_chip_malloc(0, info.chr.rom[0].banks_8k * 0x2000, 0x00) == EXIT_ERROR) {
				free(rom.data);
				return (EXIT_ERROR);
			}

			if (rom_mem_ctrl_memcpy(chr_chip(0), &rom, info.chr.rom[0].banks_8k * 0x2000) == EXIT_ERROR) { 
				free(rom.data);
				return (EXIT_ERROR);
			}

			map_chr_bank_1k_reset();

			if (info.format == NES_2_0) {
				sha1_csum(chr_chip(0), info.chr.rom[0].banks_8k * 0x2000, info.sha1sum.chr.value, info.sha1sum.chr.string, LOWER);
			}
		}

		info.prg.max_chips = info.prg.chips - 1;
		if (info.chr.chips > 0) {
			info.chr.max_chips = info.chr.chips - 1;
		}

		// la CHR ram extra
		memset(&chr.extra, 0x00, sizeof(chr.extra));

		if (info.format == NES_2_0) {
			nes20_submapper();
		}

		if (info.format == NES_2_0) {
			fprintf(stderr, "format : Nes 2.0\n");
		} else {
			fprintf(stderr, "format : iNES 1.0\n");
		}
		fprintf(stderr, "mapper : %u\n", info.mapper.id);
		if (info.mapper.id == 4098) {
			fprintf(stderr, "internal unif mapper : %u\n", unif.internal_mapper);
		}
		fprintf(stderr, "PRG chip 0 : 8k rom = %u\n", info.prg.rom[0].banks_8k);
		fprintf(stderr, "CHR chip 0 : 4k vrom = %u\n", info.chr.rom[0].banks_4k);
		fprintf(stderr, "sha1prg : %40s\n", info.sha1sum.prg.string);
		fprintf(stderr, "sha1chr : %40s\n", info.sha1sum.chr.string);

		free(rom.data);
		return (EXIT_OK);
	}

	free(rom.data);
	return (EXIT_ERROR);
}

void search_in_database(void) {
	WORD i;

	// cerco nel database
	for (i = 0; i < LENGTH(dblist); i++) {
		if (!(memcmp(dblist[i].sha1sum, info.sha1sum.prg.string, 40))) {
			info.mapper.id = dblist[i].mapper;
			info.mapper.submapper = dblist[i].submapper;
			info.id = dblist[i].id;
			info.machine[DATABASE] = dblist[i].machine;
			info.mirroring_db = dblist[i].mirroring;
			vs_system.ppu = dblist[i].vs_ppu;
			vs_system.special_mode.type = dblist[i].vs_sm;
			info.default_dipswitches = dblist[i].dipswitches;
			info.extra_from_db = dblist[i].extra;
			switch (info.mapper.id) {
				case 1:
					// Fix per Famicom Wars (J) [!] che ha l'header INES errato
					if (info.id == BAD_YOSHI_U) {
						info.chr.rom[0].banks_8k = 4;
					} else if (info.id == MOWPC10) {
						info.chr.rom[0].banks_8k = 0;
					}
					break;
				case 2:
					// Fix per "Best of the Best - Championship Karate (E) [!].nes"
					// che ha l'header INES non corretto.
					if (info.id == BAD_INES_BOTBE) {
						info.prg.rom[0].banks_16k = 16;
						info.chr.rom[0].banks_8k = 0;
					}
					break;
				case 3:
					// Fix per "Tetris (Bulletproof) (Japan).nes"
					// che ha l'header INES non corretto.
					if (info.id == BAD_INES_TETRIS_BPS) {
						info.prg.rom[0].banks_16k = 2;
						info.chr.rom[0].banks_8k = 2;
					}
					break;
				case 7:
					// Fix per "WWF Wrestlemania (E) [!].nes"
					// che ha l'header INES non corretto.
					if (info.id == BAD_INES_WWFWE) {
						info.prg.rom[0].banks_16k = 8;
						info.chr.rom[0].banks_8k = 0;
					} else if (info.id == CSPC10) {
						info.chr.rom[0].banks_8k = 0;
					}
					break;
				case 10:
					// Fix per Famicom Wars (J) [!] che ha l'header INES errato
					if (info.id == BAD_INES_FWJ) {
						info.chr.rom[0].banks_8k = 8;
					}
					break;
				case 11:
					// Fix per King Neptune's Adventure (Color Dreams) [!]
					// che ha l'header INES errato
					if (info.id == BAD_KING_NEPT) {
						info.prg.rom[0].banks_16k = 4;
						info.chr.rom[0].banks_8k = 4;
					}
					break;
				case 33:
					if (info.id == BAD_INES_FLINJ) {
						info.chr.rom[0].banks_8k = 32;
					}
					break;
				case 113:
					if (info.id == BAD_INES_SWAUS) {
						info.prg.rom[0].banks_16k = 1;
						info.chr.rom[0].banks_8k = 2;
					}
					break;
				case 191:
					if (info.id == BAD_SUGOROQUEST) {
						info.chr.rom[0].banks_8k = 16;
					}
					break;
				case 235:
					if (!info.prg.rom[0].banks_16k) {
						info.prg.rom[0].banks_16k = 256;
					}
					break;
				case UNIF_MAPPER:
					unif.internal_mapper = info.mapper.submapper;
					break;

			}
			if (info.mirroring_db == UNK_VERTICAL) {
				mirroring_V();
			}
			if (info.mirroring_db == UNK_HORIZONTAL) {
				mirroring_H();
			}
			break;
		}
	}
}
BYTE ines10_search_in_database(void *rom_mem) {
	_rom_mem *rom = (_rom_mem *)rom_mem;
	size_t position;

	// setto i default prima della ricerca
	info.machine[DATABASE] = info.mapper.submapper = info.mirroring_db = info.id = DEFAULT;
	info.extra_from_db = 0;
	vs_system.ppu = vs_system.special_mode.type = DEFAULT;

	// punto oltre l'header
	if (info.trainer) {
		position = (0x10 + sizeof(trainer.data));
	} else {
		position = 0x10;
	}

	// mapper 235
	// 260-in-1 [p1][b1].nes ha un numero di prg_rom_16k_count
	// pari a 256 (0x100) ed essendo un BYTE (0xFF) quello che l'INES
	// utilizza per indicare in numero di 16k, nell'INES header sara'
	// presente 0.
	// 150-in-1 [a1][p1][!].nes ha lo stesso chsum del 260-in-1 [p1][b1].nes
	// ma ha un numero di prg_rom_16k_count di 127.
	if (!info.prg.rom[0].banks_16k && (info.mapper.id == 235)) {
		info.prg.rom[0].banks_16k = 256;
	}

	{
		size_t len = !info.prg.rom[0].banks_16k ? 0x2000 : info.prg.rom[0].banks_16k * 0x4000;

		if ((position + len) > rom->size) {
			info.prg.rom[0].banks_16k = (rom->size - position) / 0x4000;
			fprintf(stderr, "truncated PRG ROM\n");
		}

		// calcolo l'sha1 della PRG Rom
		sha1_csum(rom->data + position, len, info.sha1sum.prg.value, info.sha1sum.prg.string, LOWER);
		position += len;
	}

	if (info.chr.rom[0].banks_8k) {
		if ((position + (info.chr.rom[0].banks_8k * 0x2000)) > rom->size) {
			info.chr.rom[0].banks_8k = (rom->size - position) / 0x2000;
			fprintf(stderr, "truncated CHR ROM\n");
		}
		// calcolo anche l'sha1 della CHR rom
		sha1_csum(rom->data + position, info.chr.rom[0].banks_8k * 0x2000, info.sha1sum.chr.value, info.sha1sum.chr.string, LOWER);
		position += (info.chr.rom[0].banks_8k * 0x2000);
	}

	// cerco nel database
	search_in_database();

	if ((vs_system.ppu == DEFAULT) && (vs_system.special_mode.type == DEFAULT)) {
		vs_system.ppu = vs_system.special_mode.type = 0;
	}

	return (EXIT_OK);
}
void nes20_submapper(void) {
	switch (info.mapper.id) {
		case 2:
			switch (info.mapper.submapper) {
				case 0:
				case 1:
					info.mapper.submapper = UXROMNBC;
					break;
				case 2:
					info.mapper.submapper = UXROM;
					break;
			}
			break;
		case 3:
			switch (info.mapper.submapper) {
				case 0:
				case 1:
					info.mapper.submapper = DEFAULT;
					break;
				case 2:
					info.mapper.submapper = CNROM_CNFL;
					break;
			}
			break;
		case 7:
			switch (info.mapper.submapper) {
				case 0:
				case 1:
					info.mapper.submapper = DEFAULT;
					break;
				case 2:
					info.mapper.submapper = AMROM;
					break;
			}
			break;
		case 21:
			switch (info.mapper.submapper) {
				case 1:
					info.mapper.submapper = VRC4A;
					break;
				case 2:
					info.mapper.submapper = VRC4C;
					break;
			}
			break;
		case 23:
			switch (info.mapper.submapper) {
				case 0:
				case 3:
					info.mapper.submapper = VRC2B;
					break;
				case 1:
					info.mapper.submapper = VRC4UNL;
					break;
				case 2:
					info.mapper.submapper = VRC4E;
					break;
			}
			break;
		case 71:
			switch (info.mapper.submapper) {
				case 1:
					info.mapper.submapper = BF9097;
					break;
			}
			break;
		case 78:
			switch (info.mapper.submapper) {
				case 3:
					info.mapper.submapper = HOLYDIVER;
					break;
			}
			break;
		case 85:
			switch (info.mapper.submapper) {
				case 1:
					info.mapper.submapper = VRC7B;
					break;
				case 2:
					info.mapper.submapper = VRC7A;
					break;
			}
			break;
		case 210:
			search_in_database();
			break;
		case 268:
			switch (info.mapper.submapper) {
				case 0:
					info.mapper.submapper = COOLBOY;
					break;
				case 1:
					info.mapper.submapper = MINDKIDS;
					break;
			}
			break;
	}
}
void nes20_prg_chr_size(WORD *reg1, WORD *reg2, float divider) {
	if ((*reg1) & 0x0F00) {
		int exponent = ((*reg1) & 0x00FC) >> 2;
		float len = (size_t)pow(2, exponent) * ((((*reg1) & 0x0003) * 2) + 1);

		(*reg2) = (int)ceil(len / divider);
		(*reg1) = (*reg2) / 2;
	}
}
BYTE nes20_ram_size(BYTE mode) {
	switch (mode) {
		case 0:
			return (0);
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			return (1);
		case 8:
			return (2);
		case 9:
			return (4);
		case 10:
			return (8);
		case 11:
			return (16);
		case 12:
			return (32);
		case 13:
			return (64);
		case 14:
			return (128);
		case 15:
			return (0);
	}
	return (0);
}
