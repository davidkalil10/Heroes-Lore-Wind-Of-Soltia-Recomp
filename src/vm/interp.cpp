// interp.cpp — interpretador de bytecode JVM (subconjunto CLDC: int/long/ref; sem float/jsr/wide)
#include "vm.h"
#include <cstdio>
#include <climits>

namespace hl {

namespace {
struct SpGuard { ThreadCtx* t; size_t base; ~SpGuard() { t->sp = base; } };

[[noreturn]] void aioobe(VM& vm, Method* m, int pc, int idx, int len) {
  fprintf(stderr, "[AIOOBE] in %s.%s%s pc=%d: index=%d, length=%d\n",
          m->owner->name.c_str(), m->name.c_str(), m->desc.c_str(), pc, idx, len);
  vm.throwNew("java/lang/ArrayIndexOutOfBoundsException", std::to_string(idx));
}

Object* mkMulti(VM& vm, const std::string& d, size_t lvl, const int* cnt, int ndims) {
  char et = d[lvl + 1];
  if (ndims > 1) {
    Array* a = vm.newRefArray(cnt[0]);
    for (int i = 0; i < cnt[0]; i++) a->refs()[i] = mkMulti(vm, d, lvl + 1, cnt + 1, ndims - 1);
    return a;
  }
  if (et == '[' || et == 'L') return vm.newRefArray(cnt[0]);
  return vm.newArray(et, cnt[0]);
}

ClassInfo* resolveClassCP(VM& vm, ClassInfo* c, int idx) {
  CPEntry& e = c->cp[idx];
  if (!e.cls) e.cls = vm.mustClass(c->cp[e.a].s);
  return e.cls;
}

void resolveField(VM& vm, ClassInfo* c, int idx) {
  CPEntry& e = c->cp[idx];
  if (e.resolved) return;
  ClassInfo* k = vm.mustClass(c->cp[c->cp[e.a].a].s);
  CPEntry& nt = c->cp[e.b];
  std::string fkey = c->cp[nt.a].s + ":" + c->cp[nt.b].s;
  FieldInfo* f = vm.findField(k, fkey);
  if (!f) vm.fatal("campo não encontrado: " + k->name + "." + fkey);
  e.fld = f; e.resolved = true;
}

void resolveMethod(VM& vm, ClassInfo* c, int idx) {
  CPEntry& e = c->cp[idx];
  if (e.resolved) return;
  e.cls = vm.mustClass(c->cp[c->cp[e.a].a].s);
  CPEntry& nt = c->cp[e.b];
  const std::string& name = c->cp[nt.a].s; const std::string& desc = c->cp[nt.b].s;
  e.mkey = name + ":" + desc;
  e.retSlots = slotsOfDesc(desc, e.nargs);
  e.resolved = true;
}

void exec(VM& vm, Method* m, Value* L, Value* ret) {
  const uint8_t* code = m->code.data();
  Value* S = L + m->maxLocals;
  ClassInfo* cls = m->owner;
  int sp = 0, pc = 0, curpc = 0;
  auto u2 = [&](int p) { return (int)((code[p] << 8) | code[p + 1]); };
  auto s2 = [&](int p) { return (int)(int16_t)((code[p] << 8) | code[p + 1]); };
  auto s4 = [&](int p) { return (int32_t)(((uint32_t)code[p] << 24) | (code[p + 1] << 16) | (code[p + 2] << 8) | code[p + 3]); };
  for (;;) {
    try {
      for (;;) {
        curpc = pc; uint8_t op = code[pc++];
        switch (op) {
          case 0: break;
          case 1: S[sp++].o = nullptr; break;
          case 2: case 3: case 4: case 5: case 6: case 7: case 8: S[sp++].i = (int)op - 3; break;
          case 9: case 10: S[sp].l = op - 9; sp += 2; break;
          case 16: S[sp++].i = (int8_t)code[pc++]; break;
          case 17: S[sp++].i = s2(pc); pc += 2; break;
          case 18: case 19: {
            int idx = op == 18 ? code[pc++] : (pc += 2, u2(pc - 2));
            CPEntry& e = cls->cp[idx];
            if (e.tag == 3 || e.tag == 4) S[sp++].i = e.i;
            else if (e.tag == 8) { if (!e.str) e.str = vm.internStr(fromUtf8(cls->cp[e.a].s)); S[sp++].o = e.str; }
            else if (e.tag == 7) S[sp++].o = vm.classObjOf(resolveClassCP(vm, cls, idx));
            else vm.fatal("ldc tag " + std::to_string(e.tag));
            break;
          }
          case 20: { int idx = u2(pc); pc += 2; S[sp].l = cls->cp[idx].l; sp += 2; break; }
          case 21: case 25: S[sp++] = L[code[pc++]]; break;
          case 22: S[sp].l = L[code[pc++]].l; sp += 2; break;
          case 26: case 27: case 28: case 29: S[sp++] = L[op - 26]; break;
          case 30: case 31: case 32: case 33: S[sp].l = L[op - 30].l; sp += 2; break;
          case 42: case 43: case 44: case 45: S[sp++] = L[op - 42]; break;
          case 46: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].i = a->as<int32_t>()[i]; break; }
          case 47: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].l = a->as<int64_t>()[i]; sp++; break; }
          case 50: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].o = a->refs()[i]; break; }
          case 51: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].i = a->as<int8_t>()[i]; break; }
          case 52: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].i = a->as<uint16_t>()[i]; break; }
          case 53: { int i = S[--sp].i; Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); S[sp - 1].i = a->as<int16_t>()[i]; break; }
          case 54: case 58: L[code[pc++]] = S[--sp]; break;
          case 55: sp -= 2; L[code[pc++]].l = S[sp].l; break;
          case 59: case 60: case 61: case 62: L[op - 59] = S[--sp]; break;
          case 63: case 64: case 65: case 66: sp -= 2; L[op - 63].l = S[sp].l; break;
          case 75: case 76: case 77: case 78: L[op - 75] = S[--sp]; break;
          case 79: { int v = S[--sp].i; int i = S[--sp].i; Array* a = (Array*)S[--sp].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); a->as<int32_t>()[i] = v; break; }
          case 80: { sp -= 2; int64_t v = S[sp].l; int i = S[--sp].i; Array* a = (Array*)S[--sp].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); a->as<int64_t>()[i] = v; break; }
          case 83: { Object* v = S[--sp].o; int i = S[--sp].i; Array* a = (Array*)S[--sp].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); a->refs()[i] = v; break; }
          case 84: { int v = S[--sp].i; int i = S[--sp].i; Array* a = (Array*)S[--sp].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); a->as<int8_t>()[i] = (int8_t)v; break; }
          case 85: case 86: { int v = S[--sp].i; int i = S[--sp].i; Array* a = (Array*)S[--sp].o; if (!a) vm.npe(); if ((uint32_t)i >= (uint32_t)a->len) aioobe(vm, m, curpc, i, a->len); a->as<uint16_t>()[i] = (uint16_t)v; break; }
          case 87: sp--; break;
          case 88: sp -= 2; break;
          case 89: S[sp] = S[sp - 1]; sp++; break;
          case 90: { Value a = S[sp - 1], b = S[sp - 2]; S[sp - 2] = a; S[sp - 1] = b; S[sp++] = a; break; }
          case 91: { Value a = S[sp - 1], b = S[sp - 2], c = S[sp - 3]; S[sp - 3] = a; S[sp - 2] = c; S[sp - 1] = b; S[sp++] = a; break; }
          case 92: S[sp] = S[sp - 2]; S[sp + 1] = S[sp - 1]; sp += 2; break;
          case 93: { Value a = S[sp - 1], b = S[sp - 2], c = S[sp - 3]; S[sp - 3] = b; S[sp - 2] = a; S[sp - 1] = c; S[sp] = b; S[sp + 1] = a; sp += 2; break; }
          case 94: { Value a = S[sp - 1], b = S[sp - 2], c = S[sp - 3], d = S[sp - 4]; S[sp - 4] = b; S[sp - 3] = a; S[sp - 2] = d; S[sp - 1] = c; S[sp] = b; S[sp + 1] = a; sp += 2; break; }
          case 95: { Value a = S[sp - 1]; S[sp - 1] = S[sp - 2]; S[sp - 2] = a; break; }
          case 96: sp--; S[sp - 1].i = (int32_t)((uint32_t)S[sp - 1].i + (uint32_t)S[sp].i); break;
          case 97: sp -= 2; S[sp - 2].l = (int64_t)((uint64_t)S[sp - 2].l + (uint64_t)S[sp].l); break;
          case 100: sp--; S[sp - 1].i = (int32_t)((uint32_t)S[sp - 1].i - (uint32_t)S[sp].i); break;
          case 101: sp -= 2; S[sp - 2].l = (int64_t)((uint64_t)S[sp - 2].l - (uint64_t)S[sp].l); break;
          case 104: sp--; S[sp - 1].i = (int32_t)((uint32_t)S[sp - 1].i * (uint32_t)S[sp].i); break;
          case 105: sp -= 2; S[sp - 2].l = (int64_t)((uint64_t)S[sp - 2].l * (uint64_t)S[sp].l); break;
          case 108: { sp--; int b = S[sp].i, a = S[sp - 1].i; if (!b) vm.throwNew("java/lang/ArithmeticException", "/ by zero"); S[sp - 1].i = (a == INT_MIN && b == -1) ? INT_MIN : a / b; break; }
          case 109: { sp -= 2; int64_t b = S[sp].l, a = S[sp - 2].l; if (!b) vm.throwNew("java/lang/ArithmeticException", "/ by zero"); S[sp - 2].l = (a == INT64_MIN && b == -1) ? INT64_MIN : a / b; break; }
          case 112: { sp--; int b = S[sp].i, a = S[sp - 1].i; if (!b) vm.throwNew("java/lang/ArithmeticException", "/ by zero"); S[sp - 1].i = (b == -1) ? 0 : a % b; break; }
          case 113: { sp -= 2; int64_t b = S[sp].l, a = S[sp - 2].l; if (!b) vm.throwNew("java/lang/ArithmeticException", "/ by zero"); S[sp - 2].l = (b == -1) ? 0 : a % b; break; }
          case 116: S[sp - 1].i = (int32_t)(0u - (uint32_t)S[sp - 1].i); break;
          case 117: S[sp - 2].l = (int64_t)(0ull - (uint64_t)S[sp - 2].l); break;
          case 120: sp--; S[sp - 1].i = (int32_t)((uint32_t)S[sp - 1].i << (S[sp].i & 31)); break;
          case 121: sp--; S[sp - 2].l = (int64_t)((uint64_t)S[sp - 2].l << (S[sp].i & 63)); break;
          case 122: sp--; S[sp - 1].i = S[sp - 1].i >> (S[sp].i & 31); break;
          case 123: sp--; S[sp - 2].l = S[sp - 2].l >> (S[sp].i & 63); break;
          case 124: sp--; S[sp - 1].i = (int32_t)((uint32_t)S[sp - 1].i >> (S[sp].i & 31)); break;
          case 125: sp--; S[sp - 2].l = (int64_t)((uint64_t)S[sp - 2].l >> (S[sp].i & 63)); break;
          case 126: sp--; S[sp - 1].i &= S[sp].i; break;
          case 127: sp -= 2; S[sp - 2].l &= S[sp].l; break;
          case 128: sp--; S[sp - 1].i |= S[sp].i; break;
          case 129: sp -= 2; S[sp - 2].l |= S[sp].l; break;
          case 130: sp--; S[sp - 1].i ^= S[sp].i; break;
          case 131: sp -= 2; S[sp - 2].l ^= S[sp].l; break;
          case 132: { int idx = code[pc++]; int c = (int8_t)code[pc++]; L[idx].i = (int32_t)((uint32_t)L[idx].i + (uint32_t)c); break; }
          case 133: S[sp - 1].l = (int64_t)S[sp - 1].i; sp++; break;
          case 136: S[sp - 2].i = (int32_t)S[sp - 2].l; sp--; break;
          case 145: S[sp - 1].i = (int8_t)S[sp - 1].i; break;
          case 146: S[sp - 1].i = (uint16_t)S[sp - 1].i; break;
          case 147: S[sp - 1].i = (int16_t)S[sp - 1].i; break;
          case 148: { sp -= 4; int64_t a = S[sp].l, b = S[sp + 2].l; S[sp++].i = a < b ? -1 : (a > b ? 1 : 0); break; }
#define IF1(OPC, COND) case OPC: { int v = S[--sp].i; if (COND) pc = curpc + s2(pc); else pc += 2; break; }
          IF1(153, v == 0) IF1(154, v != 0) IF1(155, v < 0) IF1(156, v >= 0) IF1(157, v > 0) IF1(158, v <= 0)
#define IF2(OPC, COND) case OPC: { int b = S[--sp].i; int a = S[--sp].i; if (COND) pc = curpc + s2(pc); else pc += 2; break; }
          IF2(159, a == b) IF2(160, a != b) IF2(161, a < b) IF2(162, a >= b) IF2(163, a > b) IF2(164, a <= b)
          case 165: { Object* b = S[--sp].o; Object* a = S[--sp].o; if (a == b) pc = curpc + s2(pc); else pc += 2; break; }
          case 166: { Object* b = S[--sp].o; Object* a = S[--sp].o; if (a != b) pc = curpc + s2(pc); else pc += 2; break; }
          case 167: pc = curpc + s2(pc); break;
          case 170: {
            int p = (curpc + 4) & ~3; int def = s4(p), lo = s4(p + 4), hi = s4(p + 8); int v = S[--sp].i;
            if (v < lo || v > hi) pc = curpc + def; else pc = curpc + s4(p + 12 + (v - lo) * 4);
            break;
          }
          case 171: {
            int p = (curpc + 4) & ~3; int def = s4(p), n = s4(p + 4); int v = S[--sp].i; int tgt = def;
            for (int i = 0; i < n; i++) if (s4(p + 8 + i * 8) == v) { tgt = s4(p + 12 + i * 8); break; }
            pc = curpc + tgt; break;
          }
          case 172: case 176: ret[0] = S[sp - 1]; return;
          case 173: ret[0].l = S[sp - 2].l; return;
          case 177: return;
          case 178: case 179: {
            int idx = u2(pc); pc += 2; resolveField(vm, cls, idx); FieldInfo* f = cls->cp[idx].fld;
            if (f->owner->initState != 2) vm.initClass(f->owner);
            Value& v = f->owner->statics[f->index]; bool wide = f->desc[0] == 'J' || f->desc[0] == 'D';
            if (op == 178) { if (wide) { S[sp].l = v.l; sp += 2; } else S[sp++] = v; }
            else { if (wide) { sp -= 2; v.l = S[sp].l; } else v = S[--sp]; }
            break;
          }
          case 180: {
            int idx = u2(pc); pc += 2; resolveField(vm, cls, idx); FieldInfo* f = cls->cp[idx].fld;
            Object* o = S[sp - 1].o; if (!o) vm.npe();
            Value& v = static_cast<Instance*>(o)->f[f->index];
            if (f->desc[0] == 'J' || f->desc[0] == 'D') { S[sp - 1].l = v.l; sp++; } else S[sp - 1] = v;
            break;
          }
          case 181: {
            int idx = u2(pc); pc += 2; resolveField(vm, cls, idx); FieldInfo* f = cls->cp[idx].fld;
            bool wide = f->desc[0] == 'J' || f->desc[0] == 'D';
            Value val; if (wide) { sp -= 2; val.l = S[sp].l; } else val = S[--sp];
            Object* o = S[--sp].o; if (!o) vm.npe();
            static_cast<Instance*>(o)->f[f->index] = val;
            break;
          }
          case 182: case 183: case 184: case 185: {
            int idx = u2(pc); pc += (op == 185) ? 4 : 2;
            resolveMethod(vm, cls, idx); CPEntry& e = cls->cp[idx];
            int na = e.nargs; Method* mt;
            if (op == 184) {
              if (!e.meth) { e.meth = vm.mustMethod(e.cls, e.mkey); }
              mt = e.meth; if (mt->owner->initState != 2) vm.initClass(mt->owner);
              Value rv[2]; vm.invoke(mt, &S[sp - na], rv); sp -= na;
              if (e.retSlots == 1) S[sp++] = rv[0]; else if (e.retSlots == 2) { S[sp].l = rv[0].l; sp += 2; }
              break;
            }
            Object* self = S[sp - na - 1].o; if (!self) vm.npe();
            if (op == 183) {
              if (!e.meth) e.meth = vm.mustMethod(e.cls, e.mkey);
              mt = e.meth;
            } else if (self->kind == K_ARRAY && e.mkey == "clone:()Ljava/lang/Object;") {
              Array* a = static_cast<Array*>(self); Array* c = vm.newArray(a->type, a->len); c->data = a->data;
              S[sp - 1].o = c; break;
            } else {
              if (self->cls == e.icCls) mt = e.icM;
              else { mt = vm.findMethod(self->cls, e.mkey); if (!mt) vm.fatal("método virtual não encontrado: " + self->cls->name + "." + e.mkey); e.icCls = self->cls; e.icM = mt; }
            }
            Value rv[2]; vm.invoke(mt, &S[sp - na - 1], rv); sp -= na + 1;
            if (e.retSlots == 1) S[sp++] = rv[0]; else if (e.retSlots == 2) { S[sp].l = rv[0].l; sp += 2; }
            break;
          }
          case 187: {
            int idx = u2(pc); pc += 2; ClassInfo* c = resolveClassCP(vm, cls, idx);
            vm.maybeGC(); S[sp++].o = vm.newObject(c); break;
          }
          case 188: {
            int t = code[pc++]; int n = S[sp - 1].i; vm.maybeGC();
            static const char tm[] = {0, 0, 0, 0, 'Z', 'C', 'F', 'D', 'B', 'S', 'I', 'J'};
            S[sp - 1].o = vm.newArray(tm[t], n); break;
          }
          case 189: { pc += 2; int n = S[sp - 1].i; vm.maybeGC(); S[sp - 1].o = vm.newRefArray(n); break; }
          case 190: { Array* a = (Array*)S[sp - 1].o; if (!a) vm.npe(); S[sp - 1].i = a->len; break; }
          case 191: { Object* o = S[--sp].o; if (!o) vm.npe(); throw JavaThrow{o}; }
          case 192: case 193: {
            int idx = u2(pc); pc += 2; Object* o = S[sp - 1].o; const std::string& nm = cls->cp[cls->cp[idx].a].s;
            bool ok;
            if (!o) ok = (op == 192);
            else if (nm[0] == '[') ok = o->kind == K_ARRAY;
            else ok = vm.isInstance(o, resolveClassCP(vm, cls, idx));
            if (op == 192) { if (!ok) vm.throwNew("java/lang/ClassCastException", o->cls->name); }
            else S[sp - 1].i = o && ok ? 1 : 0;
            break;
          }
          case 194: { Object* o = S[--sp].o; vm.monitorEnter(o); break; }
          case 195: { Object* o = S[--sp].o; vm.monitorExit(o); break; }
          case 197: {
            int idx = u2(pc); int dims = code[pc + 2]; pc += 3; int cnt[8];
            for (int i = dims - 1; i >= 0; i--) { cnt[i] = S[--sp].i; if (cnt[i] < 0) vm.throwNew("java/lang/NegativeArraySizeException"); }
            vm.maybeGC(); S[sp++].o = mkMulti(vm, cls->cp[cls->cp[idx].a].s, 0, cnt, dims); break;
          }
          case 198: { Object* o = S[--sp].o; if (!o) pc = curpc + s2(pc); else pc += 2; break; }
          case 199: { Object* o = S[--sp].o; if (o) pc = curpc + s2(pc); else pc += 2; break; }
          case 200: pc = curpc + s4(pc); break;
          default:
            vm.fatal("opcode não suportado " + std::to_string(op) + " em " + cls->name + "." + m->name + m->desc + " pc=" + std::to_string(curpc));
        }
      }
    } catch (JavaThrow& jt) {
      bool found = false;
      for (const ExcEntry& x : m->exc) {
        if (curpc < x.start || curpc >= x.end) continue;
        if (x.catchType) {
          ClassInfo* ct = resolveClassCP(vm, cls, x.catchType);
          if (!vm.isInstance(jt.ex, ct)) continue;
        }
        sp = 0; S[sp++].o = jt.ex; pc = x.handler; found = true; break;
      }
      if (!found) throw;
    }
  }
}
}  // namespace

void VM::invoke(Method* m, Value* args, Value* ret) {
  if (m->native) { m->native(*this, args, ret); return; }
  if (m->code.empty()) fatal("método sem código: " + m->owner->name + "." + m->name + m->desc);
  ThreadCtx* t = tctx;
  if (!t) fatal("invoke chamado sem ThreadCtx ativo no método: " + m->owner->name + "." + m->name);
  size_t base = t->sp;
  int nargs = m->argSlots + (m->isStatic ? 0 : 1);
  size_t need = base + m->maxLocals + m->maxStack + 8;
  if (need >= t->stack.size()) throwNew("java/lang/Error", "StackOverflow");
  Value* L = &t->stack[base];
  for (int i = 0; i < nargs; i++) L[i] = args[i];
  for (int i = nargs; i < m->maxLocals + m->maxStack; i++) L[i].l = 0;
  t->sp = need; SpGuard g{t, base};
  if (trace) fprintf(stderr, "[call] %s.%s%s\n", m->owner->name.c_str(), m->name.c_str(), m->desc.c_str());
  exec(*this, m, L, ret);

  // Restaura a lógica original de BGM de mapa de ae.a() (presente em ai.a() da build K700 v2.0.7,
  // que havia sido removida na versão PT-BR junto com 24.mid e 26..31.mid).
  if (!m->isStatic && m->name == "a" && m->desc == "()V" && m->owner->name == "ae") {
    Instance* aeInst = static_cast<Instance*>(L[0].o);
    FieldInfo* fa = findField(m->owner, "a:B"); // var_byte_a (Map ID)
    FieldInfo* fb = findField(m->owner, "b:B"); // var_byte_b (Tileset ID)
    if (aeInst && fa && fb) {
      int mapId = (int8_t)aeInst->f[fa->index].i;
      int tilesetId = (int8_t)aeInst->f[fb->index].i;
      if (mapId != 79 && mapId != 80 && mapId != 81) {
        static const int8_t kTilesetBgm[20] = {
            28, 0, 27, 29, -1, 1, 26, 31, 31, 2, -1, 25, 24, 24, 30, 3, -1, -1, -1, -1
        };
        if (tilesetId >= 0 && tilesetId < 20) {
          int8_t sndId = kTilesetBgm[tilesetId];
          if (sndId != -1) {
            ClassInfo* cbw = mustClass("bw");
            initClass(cbw);
            Method* mLoad = mustMethod(cbw, "a:(B)V");
            Method* mPlay = mustMethod(cbw, "b:(I)V");
            Value aLoad[1]; aLoad[0].i = sndId;
            Value rDummy[2];
            invoke(mLoad, aLoad, rDummy);
            Value aPlay[1]; aPlay[0].i = sndId;
            invoke(mPlay, aPlay, rDummy);
          }
        }
      }
    }
  }
}

}  // namespace hl
