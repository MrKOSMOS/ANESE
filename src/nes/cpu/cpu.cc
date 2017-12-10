#include "cpu.h"

#include <cstdlib>
#include <cstdio>
#include <cassert>

/*-----------------------------  Public Methods  -----------------------------*/

CPU::~CPU() {}

CPU::CPU(Memory& mem, InterruptLines& interrupt)
: interrupt(interrupt),
  mem(mem)
{
  this->power_cycle();
}

// https://wiki.nesdev.com/w/index.php/CPU_power_up_state
void CPU::power_cycle() {
  this->cycles = 0;

  this->reg.p.raw = 0x34; // 0b00110100, Interrupt = 1, Break = 1, Unused = 1

  this->reg.a = 0x00;
  this->reg.x = 0x00;
  this->reg.y = 0x00;

  this->reg.s = 0xFD;

  this->state = CPU::State::Running;
}

// https://wiki.nesdev.com/w/index.php/CPU_power_up_state
void CPU::reset() {
  this->reg.s -= 3; // the stack pointer is decremented by 3 (weird...)
  this->reg.p.i = 1;

  this->state = CPU::State::Running;
}

CPU::State CPU::getState() const { return this->state; }

/*----------------------------  Private Methods  -----------------------------*/

void CPU::service_interrupt(Interrupts::Type interrupt, bool brk /* = false */) {
  using namespace Interrupts;

  assert(interrupt != NONE);

#ifdef NESTEST
  // custom reset for headless nestest
  if (interrupt == RESET) {
    this->reg.pc = 0xC000;
    this->interrupt.service(interrupt);
    return;
  }
#endif

  this->reg.p.i = true; // don't want interrupts being interrupted

  // Push stack pointer and processor status onto stack for safekeeping
  if (interrupt != RESET) {
    this->s_push16(this->reg.pc);
    this->s_push(this->reg.p.raw);
  }

  // Interrupts take 7 cycles to execute
  this->cycles += 7;

  switch (interrupt) {
  case IRQ: if (brk || !this->reg.p.i)
              this->reg.pc = this->mem.read16(0xFFFE); break;
  case RESET: this->reg.pc = this->mem.read16(0xFFFC); break;
  case NMI:   this->reg.pc = this->mem.read16(0xFFFA); break;
  default: break;
  }

  // Clear interrupt
  this->interrupt.service(interrupt);
}

u16 CPU::get_operand_addr(const Instructions::Opcode& opcode) {
  using namespace Instructions::AddrM;

  u16 addr = 0xBAD; // this is what we are trying to find...

  // To keep switch-statement code clean, define some temporary macros that
  // read 8 and 1 bit arguments (respectively)
  #define arg8  (this->mem[this->reg.pc++])
  #define arg16 (this->mem.read16((this->reg.pc += 2) - 2))

  switch(opcode.addrm) {
    case abs_: addr = arg16;                                             break;
    case absX: addr = arg16 + this->reg.x;                               break;
    case absY: addr = arg16 + this->reg.y;                               break;
    case ind_: addr = this->mem.read16_zpg(arg16);                       break;
    case indY: addr = this->mem.read16_zpg(arg8) + this->reg.y;          break;
    case Xind: addr = this->mem.read16_zpg((arg8 + this->reg.x) & 0xFF); break;
    case zpg_: addr = arg8;                                              break;
    case zpgX: addr = (arg8 + this->reg.x) & 0xFF;                       break;
    case zpgY: addr = (arg8 + this->reg.y) & 0xFF;                       break;
    case rel : addr = this->reg.pc++;                                    break;
    case imm : addr = this->reg.pc++;                                    break;
    case acc : addr = this->reg.a;                                       break;
    case impl: addr = u8(0xFACA11); /* no args, so return fack all :D */ break;
    case INVALID:
      fprintf(stderr, "[CPU] Invalid Addressing Mode! Double check table!\n");
      // don't fail *just* yet, let it fail at the CPU instr decode switch
      return 0xBAD;
  }

  // Undefine temporary switch statement macros
  #undef arg16
  #undef arg8

  // Check to see if we need to add extra cycles due to crossing pages
  if (opcode.check_pg_cross == true) {
    // I checked, the only instructions affected by this are those with the
    // following 3 addressing modes. No need to handle any of the other modes
    #define did_pg_cross(a,b) (((a) & 0xFF00) != ((b) & 0xFF00))
    switch (opcode.addrm) {
    case absX: this->cycles += did_pg_cross(addr - this->reg.x, addr); break;
    case absY: this->cycles += did_pg_cross(addr - this->reg.y, addr); break;
    case indY: this->cycles += did_pg_cross(addr - this->reg.y, addr); break;
    default: break;
    }
    #undef is_pg_cross
  }

  return addr;
}

uint CPU::step() {
  uint old_cycles = this->cycles;

  // Service pending interrupts
  if (Interrupts::Type interrupt = this->interrupt.get()) {
    this->service_interrupt(interrupt);
    return this->cycles - old_cycles;
  }

  // Fetch current opcode
  u8 op = this->mem[this->reg.pc++];

  // Lookup info about opcode
  Instructions::Opcode opcode = Instructions::Opcodes[op];

#ifdef NESTEST
  this->nestest(opcode); // print NESTEST debug info
#endif

  // Depending on what addrm this instruction uses, this will either be a u8
  // or a u16. Thus, we use a u16 to get the value from the fn, and let
  // individual instructions cast it to u8 when they need to.
  u16 addr = this->get_operand_addr(opcode);

  using namespace Instructions::Instr;

  // Define some macros used across multiple instructions

  // Set Zero and Negative flags
  #define set_zn(val) \
    this->reg.p.z = val == 0; \
    this->reg.p.n = nth_bit(val, 7);

  // Branch if condition is satisfied
  #define branch(cond)                                                 \
    if (!cond) break;                                                  \
    i8 offset = i8(this->mem[addr]);                                   \
    /* Extra cycle on succesful branch */                              \
    this->cycles += 1;                                                 \
    /* Check if extra cycles due to jumping across pages */            \
    if ((this->reg.pc & 0xFF00) != ((this->reg.pc + offset) & 0xFF00)) \
      this->cycles += 2;                                               \
    this->reg.pc += offset;                                            \

  switch (opcode.instr) {
    case JMP: { this->reg.pc = addr;
              } break;
    case LDX: { this->reg.x = this->mem[addr];
                set_zn(this->reg.x);
              } break;
    case STX: { this->mem[addr] = this->reg.x;
              } break;
    case JSR: { this->s_push16(this->reg.pc - 1);
                this->reg.pc = addr;
              } break;
    case NOP: { // me_irl
              } break;
    case SEC: { this->reg.p.c = 1;
              } break;
    case CLC: { this->reg.p.c = 0;
              } break;
    case CLI: { this->reg.p.i = 0;
              } break;
    case BCS: { branch(this->reg.p.c);
              } break;
    case BCC: { branch(!this->reg.p.c);
              } break;
    case BEQ: { branch(this->reg.p.z);
              } break;
    case BNE: { branch(!this->reg.p.z);
              } break;
    case LDA: { this->reg.a = this->mem[addr];
                set_zn(this->reg.a);
              } break;
    case STA: { this->mem[addr] = this->reg.a;
              } break;
    case BIT: { u8 mem = this->mem[addr];
                this->reg.p.z = (this->reg.a & mem) == 0;
                this->reg.p.v = nth_bit(mem, 6);
                this->reg.p.n = nth_bit(mem, 7);
              } break;
    case BVS: { branch(this->reg.p.v);
              } break;
    case BVC: { branch(!this->reg.p.v);
              } break;
    case BPL: { branch(!this->reg.p.n);
              } break;
    case RTS: { this->reg.pc = this->s_pull16() + 1;
              } break;
    case AND: { this->reg.a &= this->mem[addr];
                set_zn(this->reg.a);
              } break;
    case SEI: { this->reg.p.i = 1;
              } break;
    case SED: { this->reg.p.d = 1;
              } break;
    case PHP: { this->s_push(this->reg.p.raw);
              } break;
    case PLA: { this->reg.a = this->s_pull();
                set_zn(this->reg.a);
              } break;
    case CMP: { u8 val = this->mem[addr];
                this->reg.p.c = this->reg.a >= val;
                set_zn(this->reg.a - val);
              } break;
    case CLD: { this->reg.p.d = 0;
              } break;
    case PHA: { this->s_push(this->reg.a);
              } break;
    case PLP: { this->reg.p.raw = this->s_pull() | 0x20; // NESTEST
              } break;
    case BMI: { branch(this->reg.p.n);
              } break;
    case ORA: { this->reg.a |= this->mem[addr];
                set_zn(this->reg.a);
              } break;
    case CLV: { this->reg.p.v = 0;
              } break;
    case EOR: { this->reg.a ^= this->mem[addr];
                set_zn(this->reg.a);
              } break;
    // Fuck ADC
    case ADC: { u8  val = this->mem[addr];
                u16 sum = this->reg.a + val + this->reg.p.c;
                this->reg.p.c = sum > 0xFF;
                this->reg.p.z = u8(sum) == 0;
                // http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html
                this->reg.p.v = ~(this->reg.a ^ val)
                              &  (this->reg.a ^ sum)
                              & 0x80;
                this->reg.p.n = nth_bit(u8(sum), 7);
                this->reg.a = u8(sum);
              } break;
    // Fuck SBC
    case SBC: { u8  val = this->mem[addr];
                u16 sum = this->reg.a + ~val + this->reg.p.c;
                this->reg.p.c = !(sum > 0xFF);
                this->reg.p.z = u8(sum) == 0;
                // http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html
                this->reg.p.v = ~(this->reg.a ^ ~val)
                              &  (this->reg.a ^ sum)
                              & 0x80;
                this->reg.p.n = nth_bit(u8(sum), 7);
                this->reg.a = u8(sum);
              } break;
    case LDY: { this->reg.y = this->mem[addr];
                set_zn(this->reg.y);
              } break;
    case CPY: { u8 val = this->mem[addr];
                this->reg.p.c = this->reg.y >= val;
                set_zn(this->reg.y - val);
              } break;
    case CPX: { u8 val = this->mem[addr];
                this->reg.p.c = this->reg.x >= val;
                set_zn(this->reg.x - val);
              } break;
    case INY: { this->reg.y++;
                set_zn(this->reg.y);
              } break;
    case INX: { this->reg.x++;
                set_zn(this->reg.x);
              } break;
    case DEY: { this->reg.y--;
                set_zn(this->reg.y);
              } break;
    case DEX: { this->reg.x--;
                set_zn(this->reg.x);
              } break;
    case STY: { this->mem[addr] = this->reg.y;
              } break;
    case TAY: { this->reg.y = this->reg.a;
                set_zn(this->reg.y);
              } break;
    case TAX: { this->reg.x = this->reg.a;
                set_zn(this->reg.x);
              } break;
    case TYA: { this->reg.a = this->reg.y;
                set_zn(this->reg.a);
              } break;
    case TXA: { this->reg.a = this->reg.x;
                set_zn(this->reg.a);
              } break;
    case TSX: { this->reg.x = this->reg.s;
                set_zn(this->reg.x);
              } break;
    case TXS: { this->reg.s = this->reg.x;
              } break;
    case RTI: { this->reg.p.raw = this->s_pull() | 0x20; // NESTEST
                this->reg.pc = this->s_pull16();
              } break;
    case BRK: { // ignores interrupt disable bit, and forces an interrupt
                this->service_interrupt(Interrupts::Type::IRQ, true);
              } break;
    case LSR: { if (opcode.addrm == Instructions::AddrM::acc) {
                  this->reg.p.c = nth_bit(this->reg.a, 0);
                  this->reg.a >>= 1;
                  set_zn(this->reg.a);
                } else {
                  u8 val = this->mem[addr];
                  this->reg.p.c = nth_bit(val, 0);
                  val >>= 1;
                  set_zn(val);
                  this->mem[addr] = val;
                }
              } break;
    case ASL: { if (opcode.addrm == Instructions::AddrM::acc) {
                  this->reg.p.c = nth_bit(this->reg.a, 7);
                  this->reg.a <<= 1;
                  set_zn(this->reg.a);
                } else {
                  u8 val = this->mem[addr];
                  this->reg.p.c = nth_bit(val, 7);
                  val <<= 1;
                  set_zn(val);
                  this->mem[addr] = val;
                }
              } break;
    case ROR: { if (opcode.addrm == Instructions::AddrM::acc) {
                  bool old_bit_0 = nth_bit(this->reg.a, 0);
                  this->reg.a = (this->reg.a >> 1) | (this->reg.p.c << 7);
                  this->reg.p.c = old_bit_0;
                  set_zn(this->reg.a);
                } else {
                  u8 val = this->mem[addr];
                  bool old_bit_0 = nth_bit(val, 0);
                  val = (val >> 1) | (this->reg.p.c << 7);
                  this->reg.p.c = old_bit_0;
                  set_zn(val);
                  this->mem[addr] = val;
                }
              } break;
    case ROL: { if (opcode.addrm == Instructions::AddrM::acc) {
                  bool old_bit_0 = nth_bit(this->reg.a, 7);
                  this->reg.a = (this->reg.a << 1) | this->reg.p.c;
                  this->reg.p.c = old_bit_0;
                  set_zn(this->reg.a);
                } else {
                  u8 val = this->mem[addr];
                  bool old_bit_0 = nth_bit(val, 7);
                  val = (val << 1) | this->reg.p.c;
                  this->reg.p.c = old_bit_0;
                  set_zn(val);
                  this->mem[addr] = val;
                }
              } break;
    case INC: { u8 val = this->mem[addr];
                val++;
                set_zn(val);
                this->mem[addr] = val;
              } break;
    case DEC: { u8 val = this->mem[addr];
                val--;
                set_zn(val);
                this->mem[addr] = val;
              } break;
    default:
      fprintf(stderr, "[CPU] Unimplemented Instruction! 0x%02X\n", opcode.raw);
      this->state = CPU::State::Halted;
      break;
  }

  this->cycles += opcode.cycles;
  return this->cycles - old_cycles;
}

/*----------  Helpers  ----------*/

u8   CPU::s_pull()       { return this->mem[0x0100 + ++this->reg.s]; }
void CPU::s_push(u8 val) { this->mem[0x0100 + this->reg.s--] = val;  }

u16 CPU::s_pull16() {
  u16 lo = this->s_pull();
  u16 hi = this->s_pull();
  return (hi << 8) | lo;
}
void CPU::s_push16(u16 val) {
  this->s_push(val >> 8);   // push hi
  this->s_push(val & 0xFF); // push lo
}
