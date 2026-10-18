/* Switch statements: dense (jump tables), sparse, nested, with fallthrough. */
#include "corpus.h"

EXPORT i32 dense(i32 op, i32 a, i32 b) {
    switch (op) {
    case 0: return a + b;   case 1: return a - b;   case 2: return a * b;   case 3: return a & b;
    case 4: return a | b;   case 5: return a ^ b;   case 6: return a << (b & 31); case 7: return a >> (b & 31);
    case 8: return -a;      case 9: return ~b;      case 10: return a < b;  case 11: return a == b;
    case 12: return a + 2 * b; case 13: return a - 3 * b; case 14: return (a + b) / 2; case 15: return a % 7;
    default: return 0x7fffffff;
    }
}
EXPORT i32 dense_offset(i32 k) {
    switch (k) {
    case 100: return 1; case 101: return 4; case 102: return 9; case 103: return 16; case 104: return 25;
    case 105: return 36; case 106: return 49; case 107: return 64; case 108: return 81; case 110: return 121;
    case 111: return 144; case 112: return 169;
    }
    return -1;
}
EXPORT i32 sparse(u32 k) {
    switch (k) {
    case 1: return 10; case 17: return 20; case 300: return 30; case 4096: return 40;
    case 70000: return 50; case 0x10000000u: return 60; case 0xFFFFFFFFu: return 70;
    }
    return 0;
}
EXPORT i32 fallthrough(i32 n, i32 acc) {
    switch (n & 7) {
    case 7: acc += 7; case 6: acc *= 3; case 5: acc -= 5; case 4: acc ^= 0x44;
    case 3: acc += 3; case 2: acc <<= 1; case 1: acc |= 1; case 0: break;
    }
    return acc;
}
EXPORT i32 nested(i32 a, i32 b) {
    switch (a & 3) {
    case 0: switch (b & 7) { case 0: return 1; case 1: return 2; case 2: return 3; case 3: return 5;
                             case 4: return 8; case 5: return 13; case 6: return 21; default: return 34; }
    case 1: return b + 100;
    case 2: switch (b & 3) { case 0: return -1; case 1: return -2; default: return -3; }
    default: return a * b;
    }
}
EXPORT u8 byte_table(u8 c) {
    switch (c) {
    case 'a': case 'e': case 'i': case 'o': case 'u': return 1;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': return 2;
    case ' ': case '\t': case '\n': case '\r': return 3;
    case '+': case '-': case '*': case '/': return 4;
    default: return 0;
    }
}
/* A small bytecode interpreter: a loop around a dense switch. */
EXPORT i64 interp(const u8 *code, u32 len, i64 x) {
    i64 st[8] = {0}; u32 sp = 0, pc = 0, steps = 0;
    while (pc < len && steps++ < 256) {
        u8 op = code[pc++];
        switch (op & 15) {
        case 0: st[sp & 7] = x; sp++; break;
        case 1: st[sp & 7] = (i8)code[pc % len]; pc++; sp++; break;
        case 2: sp--; st[(sp - 1) & 7] += st[sp & 7]; break;
        case 3: sp--; st[(sp - 1) & 7] -= st[sp & 7]; break;
        case 4: sp--; st[(sp - 1) & 7] *= st[sp & 7]; break;
        case 5: st[(sp - 1) & 7] = -st[(sp - 1) & 7]; break;
        case 6: st[sp & 7] = st[(sp - 1) & 7]; sp++; break;
        case 7: sp--; break;
        case 8: if (st[(sp - 1) & 7] & 1) pc += 2; break;
        case 9: st[(sp - 1) & 7] ^= x; break;
        case 10: st[(sp - 1) & 7] >>= 1; break;
        case 11: st[(sp - 1) & 7] <<= 1; break;
        default: return st[(sp - 1) & 7];
        }
    }
    return st[(sp - 1) & 7];
}
