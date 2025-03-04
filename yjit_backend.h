#ifndef YJIT_BACKEND_H
#define YJIT_BACKEND_H 1

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "darray.h"
#include "internal/static_assert.h"
#include "internal.h"

// Operand to an IR instruction
enum yjit_ir_opnd_type
{
    EIR_VOID = 0,   // For insns with no output
    //EIR_STACK,    // Value on the temp stack (idx)
    //EIR_LOCAL,    // Local variable (idx, do we need depth too?)
    EIR_VALUE,      // Immediate Ruby value, may be GC'd, movable
    EIR_INSN_OUT,   // Output of a preceding instruction in this block
    EIR_CODE_PTR,   // Pointer to a piece of code (e.g. side-exit)
    EIR_LABEL_NAME, // A label without an index in the output
    EIR_LABEL_IDX,  // A label that has been indexed

    // Low-level operands, for lowering
    EIR_MEM,        // Memory location (num_bits, base_ptr, const_offset)
    EIR_IMM,        // Raw, non GC'd immediate (num_bits, val)
    EIR_REG         // Machine register (num_bits, idx)
};

// Register value used by IR operands
typedef struct yjit_reg_t
{
    // Register index
    uint8_t idx: 5;

    // Special register flag EC/CFP/SP/SELF
    bool special: 1;

} ir_reg_t;
STATIC_ASSERT(ir_reg_size, sizeof(ir_reg_t) <= 8);

// Operand to an IR instruction
typedef struct yjit_ir_opnd_t
{
    // Payload
    union
    {
        // Memory location
        struct {
            uint32_t disp;

            // Base register
            ir_reg_t base;
        } mem;

        // Register operand
        ir_reg_t reg;

        // Value immediates
        VALUE value;

        // Raw immediates
        int64_t imm;
        uint64_t u_imm;

        // For local/stack/insn
        uint32_t idx;

        // For branch targets
        uint8_t* code_ptr;

        // For strings (names, comments, etc.)
        char* str;
    } as;

    // Size in bits (8, 16, 32, 64)
    uint8_t num_bits;

    // Kind of IR operand
    uint8_t kind: 4;

} ir_opnd_t;
STATIC_ASSERT(ir_opnd_size, sizeof(ir_opnd_t) <= 16);

// TODO: we can later rename these to OPND_SELF, OPND_EC, etc.
//       But currently those names would clash with yjit_core.h
#define IR_VOID ( (ir_opnd_t){ .kind = EIR_VOID } )
#define IR_EC   ( (ir_opnd_t){ .num_bits = 64, .kind = EIR_REG, .as.reg.idx = 0, .as.reg.special = 1 } )
#define IR_CFP  ( (ir_opnd_t){ .num_bits = 64, .kind = EIR_REG, .as.reg.idx = 1, .as.reg.special = 1 } )
#define IR_SP   ( (ir_opnd_t){ .num_bits = 64, .kind = EIR_REG, .as.reg.idx = 2, .as.reg.special = 1 } )
#define IR_SELF ( (ir_opnd_t){ .num_bits = 64, .kind = EIR_REG, .as.reg.idx = 3, .as.reg.special = 1 } )

// Low-level register operand
#define IR_REG(x86reg) ( (ir_opnd_t){ .num_bits = 64, .kind = EIR_REG, .as.reg.idx = x86reg.as.reg.reg_no } )

ir_opnd_t ir_code_ptr(uint8_t* code_ptr);
ir_opnd_t ir_const_ptr(void *ptr);
ir_opnd_t ir_imm(int64_t val);
ir_opnd_t ir_mem(uint8_t num_bits, ir_opnd_t base, int32_t disp);

// Instruction opcodes
enum yjit_ir_op
{
    // Add a comment into the IR at the point that this instruction is added. It
    // won't have any impact on that actual compiled code, but it will impact
    // the output of ir_print_insns. Accepts as its only operand an EIR_IMM
    // operand (typically generated by ir_str_ptr).
    OP_COMMENT,

    // Add a label into the IR at the point that this instruction is added. It
    // will eventually be translated into an offset when generating code such
    // that EIR_LABEL_IDX operands know where to jump to. Accepts as its only
    // operand an EIR_LABEL_NAME operand (typically generated by ir_label_opnd).
    OP_LABEL,

    // Add two operands together, and return the result as a new operand. This
    // operand can then be used as the operand on another instruction. It
    // accepts two operands, which can be of any type
    //
    // Under the hood when allocating registers, the IR will determine the most
    // efficient way to get these values into memory. For example, if both
    // operands are immediates, then it will load the first one into a register
    // first with a mov instruction and then add them together. If one of them
    // is a register, however, it will just perform a single add instruction.
    OP_ADD,

    // This is the same as the OP_ADD instruction, except for subtraction.
    OP_SUB,

    // This is the same as the OP_ADD instruction, except that it performs the
    // binary AND operation.
    OP_AND,

    // Perform the NOT operation on an individual operand, and return the result
    // as a new operand. This operand can then be used as the operand on another
    // instruction.
    OP_NOT,

    // The following are conditional jump instructions. They all accept as their
    // first operand an EIR_LABEL_NAME, which is used as the target of the jump.
    //
    // The OP_JUMP_EQ instruction accepts two additional operands, to be
    // compared for equality. If they're equal, then the generated code jumps to
    // the target label. If they're not, then it continues on to the next
    // instruction.
    OP_JUMP_EQ,

    // The OP_JUMP_NE instruction is very similar to the OP_JUMP_EQ instruction,
    // except it compares for inequality instead.
    OP_JUMP_NE,

    // Checks the overflow flag and conditionally jumps to the target if it is
    // currently set.
    OP_JUMP_OVF,

    // A low-level call instruction for calling a function by a pointer. It
    // accepts one operand of type EIR_IMM that should be a pointer to the
    // function. Usually this is done by first casting the function to a void*,
    // as in: ir_const_ptr((void *)&my_function)).
    OP_CALL,

    // Calls a function by a pointer and returns an operand that contains the
    // result of the function. Accepts as its operands a pointer to a function
    // of type EIR_IMM (usually generated from ir_const_ptr) and a variable
    // number of arguments to the function being called.
    //
    // This is the higher-level instruction that should be used when you want to
    // call a function with arguments, as opposed to OP_CALL which is
    // lower-level and just calls a function without moving arguments into
    // registers for you.
    OP_CCALL,

    // Returns from the function being generated immediately. This is different
    // from OP_RETVAL in that it does nothing with the return value register
    // (whatever is in there is what will get returned). Accepts no operands.
    OP_RET,

    // First, moves a value into the return value register. Then, returns from
    // the generated function. Accepts as its only operand the value that should
    // be returned from the generated function.
    OP_RETVAL,

    // A low-level mov instruction. It accepts two operands. The first must
    // either be an EIR_REG or a EIR_INSN_OUT that resolves to a register. The
    // second can be anything else. Most of the time this instruction shouldn't
    // be used by the developer since other instructions break down to this
    // one.
    OP_MOV,

    // A low-level cmp instruction. It accepts two operands. The first it
    // expects to be a register. The second can be anything. Most of the time
    // this instruction shouldn't be used by the developer since other
    // instructions break down to this one.
    OP_CMP,

    // A conditional move instruction that should be preceeded at some point by
    // an OP_CMP instruction that would have set the requisite comparison flags.
    // Accepts 2 operands, both of which are expected to be of the EIR_REG type.
    //
    // If the comparison indicates the left compared value is greater than or
    // equal to the right compared value, then the conditional move is executed,
    // otherwise we just continue on to the next instruction.
    //
    // This is considered a low-level instruction, and the OP_SELECT_* variants
    // should be preferred if possible.
    OP_CMOV_GE,

    // The same as OP_CMOV_GE, except the comparison is greater than.
    OP_CMOV_GT,

    // The same as OP_CMOV_GE, except the comparison is less than or equal.
    OP_CMOV_LE,

    // The same as OP_CMOV_GE, except the comparison is less than.
    OP_CMOV_LT,

    // Selects between two different values based on a comparison of two other
    // values. Accepts 4 operands. The first two are the basis of the
    // comparison. The second two are the "then" case and the "else" case. You
    // can effectively think of this instruction as a ternary operation, where
    // the first two values are being compared.
    //
    // OP_SELECT_GE performs the described ternary using a greater than or equal
    // comparison, that is if the first operand is greater than or equal to the
    // second operand.
    OP_SELECT_GE,

    // The same as OP_SELECT_GE, except the comparison is greater than.
    OP_SELECT_GT,

    // The same as OP_SELECT_GE, except the comparison is less than or equal.
    OP_SELECT_LE,

    // The same as OP_SELECT_GE, except the comparison is less than.
    OP_SELECT_LT,

    // For later:
    // These encode Ruby true/false semantics
    // Can be used to enable op fusion of Ruby compare + branch.
    // OP_JUMP_TRUE, // (opnd, target)
    // OP_JUMP_FALSE, // (opnd, target)

    // For later:
    // OP_GUARD_HEAP, // (opnd, target)
    // OP_GUARD_IMM, // (opnd, target)
    // OP_GUARD_FIXNUM, // (opnd, target)

    // For later:
    // OP_COUNTER_INC, (counter_name)

    // For later:
    // OP_LEA,
    // OP_TEST,

    // Upper bound for opcodes. Not used for actual instructions.
    OP_MAX
};

// Array of operands
typedef rb_darray(ir_opnd_t) opnd_array_t;

// IR instruction
typedef struct yjit_ir_insn_t
{
    // TODO: do may need a union here to store a branch target?
    // How do we want to encore branch targets?
    opnd_array_t opnds;

    // Position in the generated machine code
    // Useful for comments and for patching jumps
    uint32_t pos;

    // Opcode for the instruction
    uint8_t op;

    // TODO: should we store 2-4 operands by default?
    // Some insns, like calls, will need to allow for a list of N operands
    // Most other instructions will have just 2-3
    // Many calls will also have just 1-3
    //bool many_args: 1;

} ir_insn_t;
STATIC_ASSERT(ir_insn_size, sizeof(ir_insn_t) <= 64);

// Array of instruction
typedef rb_darray(ir_insn_t) insn_array_t;

// Test code
void test_backend();

#endif // #ifndef YJIT_BACKEND_H
