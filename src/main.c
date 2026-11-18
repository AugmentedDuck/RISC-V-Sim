#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_handler.h"
#include "instructions.h"
#include "program_counter.h"

static void dump_registers(const struct InstructionHandler *ih)
{
    printf("\nRegisters (non-zero):\n");
    for (int i = 0; i < 32; i++) {
        if (ih->registers[i] != 0)
            printf("  x%-2d = 0x%08x (%d)\n", i, ih->registers[i], (int32_t)ih->registers[i]);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <program.bin>\n", argv[0]);
        return 1;
    }

    struct FileHandler *fh = create_filehandler();
    struct ProgramCounter *pc = create_counter();
    struct InstructionHandler *ih = create_instruction_handler();
    if (!fh || !pc || !ih) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int size = 0;
    uint8_t *program = fh->read_file(fh, argv[1], &size);
    if (!program)
        return 1;

    if (size > MEM_SIZE) {
        fprintf(stderr, "program is %d bytes, but memory is only %d bytes\n", size, MEM_SIZE);
        free(program);
        return 1;
    }

    memcpy(ih->memory, program, size); /* the program is loaded at address 0 */
    free(program);

    printf("loaded %d bytes (%d instructions)\n", size, size / 4);

    pc->cnt = 0;
    while (!ih->halted) {
        uint32_t addr = (uint32_t)pc->cnt;
        if ((addr & 0x3) || addr > MEM_SIZE - 4) {
            fprintf(stderr, "bad PC 0x%08x\n", addr);
            break;
        }

        uint32_t bits = (uint32_t)ih->memory[addr]
                      | ((uint32_t)ih->memory[addr + 1] << 8)
                      | ((uint32_t)ih->memory[addr + 2] << 16)
                      | ((uint32_t)ih->memory[addr + 3] << 24);

        printf("PC=0x%08x  instr=0x%08x\n", addr, bits);
        if (ih->decode_instruction(ih, bits, pc) != OK)
            break;
    }

    dump_registers(ih);

    free(ih);
    free(pc);
    free(fh);
    return 0;
}