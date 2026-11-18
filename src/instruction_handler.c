#include "instructions.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OP_LUI     0x37
#define OP_AUIPC   0x17
#define OP_JAL     0x6F
#define OP_JALR    0x67
#define OP_BRANCH  0x63
#define OP_LOAD    0x03
#define OP_STORE   0x23
#define OP_IMM     0x13
#define OP_REG     0x33
#define OP_FENCE   0x0F
#define OP_SYSTEM  0x73

#define ECALL_BITS  0x00000073u
#define EBREAK_BITS 0x00100073u

struct InstructionHandler *create_instruction_handler()
{
    struct InstructionHandler *ih = malloc(sizeof(struct InstructionHandler));
    if (!ih)
        return NULL;
    memset(ih->registers, 0, sizeof ih->registers);
    ih->decode_instruction = decode_instruction;
    ih->execute_instruction = execute_instruction;
    return ih;
}

static int32_t sign_extend(uint32_t value, unsigned width) {
    uint32_t mask = 1u << (width - 1);
    value &= (1u << width) - 1;

    return (int32_t)((value ^ mask) - mask);
}

static inline int32_t get_imm_i(uint32_t bits) {
    return sign_extend(bits >> 20, 12);
}

static inline int32_t get_imm_s(uint32_t bits) {
    uint32_t temp = ((bits >> 25) << 5) | ((bits >> 7) & 0b11111);
    return sign_extend(temp, 12);
}

static inline int32_t get_imm_b(uint32_t bits) {
    uint32_t temp = (((bits >> 31) & 0b00001) << 12) |
                    (((bits >> 7 ) & 0b00001) << 11) |
                    (((bits >> 25) & 0b11111) << 5 ) |
                    (((bits >> 8 ) & 0b01111) << 1 );
    return sign_extend(temp, 13);
}

static inline int32_t get_imm_u(uint32_t bits) {
    return (int32_t)(bits & 0b11111111111111111111000000000000);
}

static inline int32_t get_imm_j(uint32_t bits) {
    uint32_t temp = (((bits >> 31) & 0b00000001) << 20) |
                    (((bits >> 12) & 0b11111111) << 12) |
                    (((bits >> 20) & 0b00000001) << 11) |
                    (((bits >> 21) & 0b00111111) << 1 );
    return sign_extend(temp, 21);
}

instruction_err_t decode_instruction(struct InstructionHandler *ih, int bits, struct ProgramCounter* pc)
{
    ih->rd     = (bits >> 7 ) & 0b0011111;
    ih->funct3 = (bits >> 12) & 0b0000111;
    ih->rs1    = (bits >> 15) & 0b0011111;
    ih->rs2    = (bits >> 20) & 0b0011111;
    ih->funct7 = (bits >> 25) & 0b1111111;
    ih->imm    = 0;

    instruction_t instr = invalid_i;

    switch (bits & 0b1111111)
    {
    case 0b0110011: // function type R
        switch(ih->funct3) {
            case 0x0:
                if       (ih->funct7 == 0x00) { instr = add_i;  }
                else if  (ih->funct7 == 0x20) { instr = sub_i;  }
                break;
            case 0x1: if (ih->funct7 == 0x00) { instr = sll_i;  } break;
            case 0x2: if (ih->funct7 == 0x00) { instr = slt_i;  } break;
            case 0x3: if (ih->funct7 == 0x00) { instr = sltu_i; } break;
            case 0x4: if (ih->funct7 == 0x00) { instr = xor_i;  } break;
            case 0x5:
                if       (ih->funct7 == 0x00) { instr = srl_i;  }
                else if  (ih->funct7 == 0x20) { instr = sra_i;  }
                break;
            case 0x6: if (ih->funct7 == 0x00) { instr = or_i;   } break;
            case 0x7: if (ih->funct7 == 0x00) { instr = and_i;  } break;
            default:
                printf("Unknown Instruction of type R\n");
                break;
        }
        //execute_format_r(ih);
        break;

    case 0b0010011: // function type I-1 (Immediate Operations)
        ih->imm = get_imm_i(bits);
        switch (ih->funct3) {
            case 0x0: instr = addi_i;  break;
            case 0x1:
                if (ih->funct7 == 0x00) { instr = slli_i; }
                break;
            case 0x2: instr = slti_i;  break;
            case 0x3: instr = sltiu_i; break;
            case 0x4: instr = xori_i;  break;
            case 0x5: 
                if      (ih->funct7 == 0x00) { instr = srli_i; }
                else if (ih->funct7 == 0x20) { instr = srai_i; }
                break;
            case 0x6: instr = ori_i;  break;
            case 0x7: instr = andi_i;  break;
            default:
                printf("Unknown Instruction of type I1\n");
                break;
        }
        //execute_format_i1(ih);
        break;

    case 0b0000011: // function type I-2 (Load Store)
        ih->imm = get_imm_i(bits);
        switch (ih->funct3) {
            case 0x0: instr = lb_i;  break;
            case 0x1: instr = lh_i;  break;
            case 0x2: instr = lw_i;  break;
            case 0x4: instr = lbu_i; break;
            case 0x5: instr = lhu_i; break;
            default:
                printf("Unknown Instruction of type I2\n");
                break;
        }
        //execute_format_i2(ih);
        break;

    case 0b1100111: // function type I-3 (Jump and Link reg)
        ih->imm = get_imm_i(bits);
        if (ih->funct3 == 0x0) { instr = jalr_i; }
        else { 
            printf("Unknown Instruction of type I3\n");
        }
        //execute_format_i3(ih, pc);
        break;

    case 0b1110011: // function type I-4 (Enviroment Calls)
        if      (bits == 0x000073) { instr = ecall_i;  }
        else if (bits == 0x100073) { instr = ebreak_i; }
        else {
            printf("Unknown Instruction of type I4\n");
        }
        //execute_format_i4(ih);
        break;

    case 0b0100011: // function type S
        ih->imm = get_imm_s(bits);
        printf("function type S\n");
        switch (ih->funct3) {
            case 0x0: instr = sb_i; break;
            case 0x1: instr = sh_i; break;
            case 0x2: instr = sw_i; break;
            default: 
                printf("Unknown Instruction of type S\n");
                break;
        }
        break;

    case 0b1100011: // function type B
        ih->imm = get_imm_b(bits);
        // printf("function type B\n");

        switch (ih->funct3) {
            case 0x0: instr = beq_i;  break;
            case 0x1: instr = bne_i;  break;
            case 0x4: instr = blt_i;  break;
            case 0x5: instr = bge_i;  break;
            case 0x6: instr = bltu_i; break;
            case 0x7: instr = bgeu_i; break;
        }

        break;

    case 0b1101111: // function type J
        ih->imm = get_imm_j(bits);
        instr = jal_i;
        printf("function type J\n");
        break;

    case 0b0110111: // function type U (Load Upper Imm)
        ih->imm = get_imm_u(bits);
        instr = lui_i;
        //execute_format_u1(ih);
        break;
    case 0b0010111: // function type U (Add Upper Imm To PC)
        ih->imm = get_imm_u(bits);
        instr = auipc_i;
        //execute_format_u2(ih);
        break;

    default:
        printf("Function Type Not Existing: %d\n", (bits & 0b1111111));
        return UNKNOWN_INSTR;
    }

    return ih->execute_instruction(ih, instr, pc);
}

static void write_reg(struct InstructionHandler *ih, unsigned rd, uint32_t value) {
    if (rd != 0) {
        ih->registers[rd] = value;
    }
}

static int mem_read(struct InstructionHandler *ih, uint32_t addr, unsigned size, uint32_t *out) {
    if ((uint64_t)addr + size > MEM_SIZE) return 0;
    uint32_t temp = 0;
    for (unsigned i = 0; i < size; i++) {
        temp |= (uint32_t)ih->memory[addr + i] << (8 * i);
    }
    *out = temp;
    return 1;
}

static int mem_write(struct InstructionHandler *ih, uint32_t addr, unsigned size, uint32_t value) {
    if ((uint64_t)addr + size > MEM_SIZE) return 0;
    for (unsigned i = 0; i < size; i++) {
        ih->memory[addr + i] = (uint8_t)(value >> (8 * i));
    }
    return 1;
}

static uint32_t shift_right_arith(uint32_t value, unsigned shift) {
    uint32_t temp = value >> shift;
    if (value & 0x80000000u) {
        temp |= ~(0xFFFFFFFFu >> shift);
    }
    return temp;
}


instruction_err_t execute_instruction(struct InstructionHandler *ih, instruction_t instruction, struct ProgramCounter* pc)
{
    const uint32_t pc_now = (uint32_t)pc->cnt;
    uint32_t next_pc = pc_now + 4;

    uint32_t rs1 = ih->registers[ih->rs1];
    uint32_t rs2 = ih->registers[ih->rs2];
    int32_t rs1_s = (int32_t)rs1;
    int32_t rs2_s = (int32_t)rs2;
    uint32_t imm  = ih->imm;
    uint32_t addr = imm + rs1;
    unsigned rd   = ih->rd;

    unsigned shift_amount_i = imm & 0x1F;
    unsigned shift_amount_r = rs2 & 0x1F;

    uint32_t temp = 0;

    switch (instruction) {
        case lui_i:
            write_reg(ih, rd, imm);
            break;
        
        case auipc_i:
            write_reg(ih, rd, pc_now + imm);
            break;
        
        case jal_i:
            write_reg(ih, rd, pc_now + 4);
            next_pc = pc_now + imm;
            break;
        
        case jalr_i:
            write_reg(ih, rd, pc_now + 4);
            next_pc = (rs1 + imm) & ~1u;
            break;
        
        // BRANCH
        case beq_i:  if (rs1 == rs2)     next_pc = pc_now + imm; break;
        case bne_i:  if (rs1 != rs2)     next_pc = pc_now + imm; break;
        case blt_i:  if (rs1_s < rs2_s)  next_pc = pc_now + imm; break;
        case bge_i:  if (rs1_s >= rs2_s) next_pc = pc_now + imm; break;
        case bltu_i: if (rs1 < rs2)      next_pc = pc_now + imm; break;
        case bgeu_i: if (rs1 >= rs2)     next_pc = pc_now + imm; break;

        // LOADS
        case lb_i:
            if(!mem_read(ih, addr, 1, &temp)) return MEM_ERR;
            write_reg(ih, rd, (uint32_t)(int32_t)(int16_t)temp);
            break;

        case lh_i:
            if(!mem_read(ih, addr, 2, &temp)) return MEM_ERR;
            write_reg(ih, rd, (uint32_t)(int32_t)(int16_t)temp);
            break;

        case lw_i:
            if(!mem_read(ih, addr, 4, &temp)) return MEM_ERR;
            write_reg(ih, rd, temp);
            break;

        case lbu_i:
            if(!mem_read(ih, addr, 1, &temp)) return MEM_ERR;
            write_reg(ih, rd, temp);
            break;

        case lhu_i:
            if(!mem_read(ih, addr, 2, &temp)) return MEM_ERR;
            write_reg(ih, rd, temp);
            break;

        // STORE
        case sb_i:
            if (!mem_write(ih, addr, 1, rs2)) return MEM_ERR;
            break;
        
        case sh_i:
            if (!mem_write(ih, addr, 2, rs2)) return MEM_ERR;
            break;
        
        case sw_i:
            if (!mem_write(ih, addr, 4, rs2)) return MEM_ERR;
            break;

        // Immediate instrucitons
        case addi_i:  write_reg(ih, rd, rs1 + imm);                              break;
        case slti_i:  write_reg(ih, rd, rs1_s < ih->imm ? 1u : 0u);              break;
        case sltiu_i: write_reg(ih, rd, rs1 < imm ? 1u : 0u);                    break;
        case xori_i:  write_reg(ih, rd, rs1 ^ imm);                              break;
        case ori_i:   write_reg(ih, rd, rs1 | imm);                              break;
        case andi_i:  write_reg(ih, rd, rs1 & imm);                              break;
        case slli_i:  write_reg(ih, rd, rs1 << shift_amount_i);                  break;
        case srli_i:  write_reg(ih, rd, rs1 >> shift_amount_i);                  break;
        case srai_i:  write_reg(ih, rd, shift_right_arith(rs1, shift_amount_i)); break;

        // Register instructions
        case add_i:  write_reg(ih, rd, rs1 + rs2);                              break;
        case sub_i:  write_reg(ih, rd, rs1 - rs2);                              break;
        case slt_i:  write_reg(ih, rd, rs1_s < rs2_s ? 1u : 0u);                break;
        case sltu_i: write_reg(ih, rd, rs1 < rs2 ? 1u : 0u);                    break;
        case xor_i:  write_reg(ih, rd, rs1 ^ rs2);                              break;
        case or_i:   write_reg(ih, rd, rs1 | rs2);                              break;
        case and_i:  write_reg(ih, rd, rs1 & rs2);                              break;
        case sll_i:  write_reg(ih, rd, rs1 << shift_amount_r);                  break;
        case srl_i:  write_reg(ih, rd, rs1 >> shift_amount_r);                  break;
        case sra_i:  write_reg(ih, rd, shift_right_arith(rs1, shift_amount_r)); break;

        // SYSTEM
        case fence_i:
            break;
        
        case ecall_i:
        case ebreak_i:
            ih->halted = 1;
            break;
        
        case invalid_i:
        default:
            fprintf(stderr, "Illegal instruction at PC=0x%08x\n", pc_now);
            return UNKNOWN_INSTR;
    }

    pc->cnt = (int)next_pc;
    return OK;
}