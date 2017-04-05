/*
 * Copyright 2017 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <elf.h>
#include <link.h>
#include <stdio.h>

int main()
{
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t /*size*/, void* data) -> int {
        printf("phdrs for: %s | %zx\n", info->dlpi_name, info->dlpi_addr);
        for (int i = 0; i < info->dlpi_phnum; i++) {
            const auto& phdr = info->dlpi_phdr[i];
            printf("\tphdr: type=%d, vaddr=%zx, memsz=%zx, filesz=%zx, offset=%zx, flags=%d\n", phdr.p_type, phdr.p_vaddr, phdr.p_memsz, phdr.p_filesz, phdr.p_offset, phdr.p_flags);
            const auto fileOffset = phdr.p_vaddr + info->dlpi_addr;
            if (phdr.p_type == PT_LOAD) {
                printf("\t\tPT_LOAD\n");
                if (phdr.p_offset == 0) {
                    const auto* ehdr = reinterpret_cast<ElfW(Ehdr)*>(fileOffset);
                    printf("\t\tehdr: type=%u, machine=%d, version=%u, entry=%zx, phoff=%zx, phnum=%x, phentsize=%u, shoff=%zx, shnum=%x, shentsize=%u, shstrndx=%x\n",
                           ehdr->e_type, ehdr->e_machine, ehdr->e_version, ehdr->e_entry,
                           ehdr->e_phoff, ehdr->e_phnum, ehdr->e_phentsize,
                           ehdr->e_shoff, ehdr->e_shnum, ehdr->e_shentsize, ehdr->e_shstrndx);
                }
            } else if (phdr.p_type == PT_NOTE) {
                printf("\t\tPT_NOTE\n");
                auto sectionAddr = fileOffset;
                const auto segmentEnd = sectionAddr + phdr.p_memsz;
                const ElfW(Nhdr)* nhdr = nullptr;
                while (sectionAddr < segmentEnd) {
                    nhdr = reinterpret_cast<ElfW(Nhdr)*>(sectionAddr);
                    printf("\t\tnhdr: type=%x, namesz=%x, descsz=%x\n", nhdr->n_type, nhdr->n_namesz, nhdr->n_descsz);
                    if (nhdr->n_type == NT_GNU_BUILD_ID) {
                        const auto buildIdAddr = sectionAddr + sizeof(ElfW(Nhdr)) + nhdr->n_namesz;
                        if (buildIdAddr + nhdr->n_descsz <= segmentEnd) {
                            const auto* buildId = reinterpret_cast<const unsigned char*>(buildIdAddr);
                            printf("\t\tBuild id: ");
                            for (uint j = 0; j < nhdr->n_descsz; ++j) {
                                printf("%02x", buildId[j]);
                            }
                            printf("\n");

                        }
                    }
                    sectionAddr += sizeof(ElfW(Nhdr)) + nhdr->n_namesz + nhdr->n_descsz;
                }
            }
        }

        return 0;
    }, nullptr);
}
