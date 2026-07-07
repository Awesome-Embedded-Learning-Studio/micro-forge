#pragma once
#include <cstdint>
namespace micro_forge::loader::utils {
static constexpr uint8_t ELF_MAGIC[4] = {0x7F, 'E', 'L', 'F'};
static constexpr uint8_t ELFCLASS32 = 1;
static constexpr uint8_t ELFDATA2LSB = 1; // Little-endian
static constexpr uint8_t ET_EXEC = 2;
static constexpr uint8_t EM_ARM = 40;
static constexpr uint32_t PT_LOAD = 1;

struct Elf32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff; // Program header table offset
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;    // ELF header size = 52
    uint16_t e_phentsize; // Program header entry size = 32
    uint16_t e_phnum;     // Number of program headers
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;   // types of elf
    uint32_t p_offset; // offsets
    uint32_t p_vaddr;  // virtual, in MCU with no MMU, same with paddr
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

} // namespace micro_forge::loader::utils
