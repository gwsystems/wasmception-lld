//===- lib/FileFormat/MachO/ArchHandler_arm.cpp ---------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "Atoms.h"
#include "MachONormalizedFileBinaryUtils.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"

#include "llvm/Support/ErrorHandling.h"

using namespace llvm::MachO;
using namespace lld::mach_o::normalized;

namespace lld {
namespace mach_o {

class ArchHandler_arm : public ArchHandler {
public:
           ArchHandler_arm();
  virtual ~ArchHandler_arm();

  const Registry::KindStrings *kindStrings() override { return _sKindStrings; }

  Reference::KindArch kindArch() override { return Reference::KindArch::ARM; }

  const ArchHandler::StubInfo &stubInfo() override;
  bool isCallSite(const Reference &) override;
  bool isPointer(const Reference &) override;
  bool isPairedReloc(const normalized::Relocation &) override;
  std::error_code getReferenceInfo(const normalized::Relocation &reloc,
                                   const DefinedAtom *inAtom,
                                   uint32_t offsetInAtom,
                                   uint64_t fixupAddress, bool swap,
                                   FindAtomBySectionAndAddress atomFromAddress,
                                   FindAtomBySymbolIndex atomFromSymbolIndex,
                                   Reference::KindValue *kind,
                                   const lld::Atom **target,
                                   Reference::Addend *addend) override;
  std::error_code
      getPairReferenceInfo(const normalized::Relocation &reloc1,
                           const normalized::Relocation &reloc2,
                           const DefinedAtom *inAtom,
                           uint32_t offsetInAtom,
                           uint64_t fixupAddress, bool swap,
                           FindAtomBySectionAndAddress atomFromAddress,
                           FindAtomBySymbolIndex atomFromSymbolIndex,
                           Reference::KindValue *kind,
                           const lld::Atom **target,
                           Reference::Addend *addend) override;

  void generateAtomContent(const DefinedAtom &atom, bool relocatable,
                           FindAddressForAtom findAddress,
                           uint8_t *atomContentBuffer) override;

  void appendSectionRelocations(const DefinedAtom &atom,
                                uint64_t atomSectionOffset,
                                const Reference &ref,
                                FindSymbolIndexForAtom,
                                FindSectionIndexForAtom,
                                FindAddressForAtom,
                                normalized::Relocations &) override;

  void addAdditionalReferences(MachODefinedAtom &atom) override;

  bool isThumbFunction(const DefinedAtom &atom) override;

private:
  static const Registry::KindStrings _sKindStrings[];
  static const StubInfo              _sStubInfoArmPIC;

  enum : Reference::KindValue {
    invalid,               /// for error condition

    modeThumbCode,         /// Content starting at this offset is thumb.
    modeArmCode,           /// Content starting at this offset is arm.

    // Kinds found in mach-o .o files:
    thumb_b22,             /// ex: bl _foo
    thumb_movw,            /// ex: movw	r1, :lower16:_foo
    thumb_movt,            /// ex: movt	r1, :lower16:_foo
    thumb_movw_funcRel,    /// ex: movw	r1, :lower16:(_foo-(L1+4))
    thumb_movt_funcRel,    /// ex: movt r1, :upper16:(_foo-(L1+4))
    arm_b24,               /// ex: bl _foo
    arm_movw,              /// ex: movw	r1, :lower16:_foo
    arm_movt,              /// ex: movt	r1, :lower16:_foo
    arm_movw_funcRel,      /// ex: movw	r1, :lower16:(_foo-(L1+4))
    arm_movt_funcRel,      /// ex: movt r1, :upper16:(_foo-(L1+4))
    pointer32,             /// ex: .long _foo
    delta32,               /// ex: .long _foo - .

    // Kinds introduced by Passes:
    lazyPointer,           /// Location contains a lazy pointer.
    lazyImmediateLocation, /// Location contains immediate value used in stub.
  };

  // Utility functions for inspecting/updating instructions.
  static bool isThumbMovw(uint32_t instruction);
  static bool isThumbMovt(uint32_t instruction);
  static bool isArmMovw(uint32_t instruction);
  static bool isArmMovt(uint32_t instruction);
  static int32_t getDisplacementFromThumbBranch(uint32_t instruction);
  static int32_t getDisplacementFromArmBranch(uint32_t instruction);
  static uint16_t getWordFromThumbMov(uint32_t instruction);
  static uint16_t getWordFromArmMov(uint32_t instruction);
  static uint32_t clearThumbBit(uint32_t value, const Atom *target);
  static uint32_t setDisplacementInArmBranch(uint32_t instr, int32_t disp);
  static uint32_t setDisplacementInThumbBranch(uint32_t instr, int32_t disp);
  static uint32_t setWordFromThumbMov(uint32_t instruction, uint16_t word);
  static uint32_t setWordFromArmMov(uint32_t instruction, uint16_t word);
  
  bool useExternalRelocationTo(const Atom &target);

  void applyFixupFinal(const Reference &ref, uint8_t *location,
                       uint64_t fixupAddress, uint64_t targetAddress,
                       uint64_t inAtomAddress, bool &thumbMode);

  void applyFixupRelocatable(const Reference &ref, uint8_t *location,
                             uint64_t fixupAddress,
                             uint64_t targetAddress,
                             uint64_t inAtomAddress, bool &thumbMode);
  
  const bool _swap;
};

//===----------------------------------------------------------------------===//
//  ArchHandler_arm
//===----------------------------------------------------------------------===//

ArchHandler_arm::ArchHandler_arm() :
  _swap(!MachOLinkingContext::isHostEndian(MachOLinkingContext::arch_armv7)) {}

ArchHandler_arm::~ArchHandler_arm() { }

const Registry::KindStrings ArchHandler_arm::_sKindStrings[] = {
  LLD_KIND_STRING_ENTRY(modeThumbCode),
  LLD_KIND_STRING_ENTRY(modeArmCode),
  LLD_KIND_STRING_ENTRY(thumb_b22),
  LLD_KIND_STRING_ENTRY(thumb_movw),
  LLD_KIND_STRING_ENTRY(thumb_movt),
  LLD_KIND_STRING_ENTRY(thumb_movw_funcRel),
  LLD_KIND_STRING_ENTRY(thumb_movt_funcRel),
  LLD_KIND_STRING_ENTRY(arm_b24),
  LLD_KIND_STRING_ENTRY(arm_movw),
  LLD_KIND_STRING_ENTRY(arm_movt),
  LLD_KIND_STRING_ENTRY(arm_movw_funcRel),
  LLD_KIND_STRING_ENTRY(arm_movt_funcRel),
  LLD_KIND_STRING_ENTRY(pointer32),
  LLD_KIND_STRING_ENTRY(delta32),
  LLD_KIND_STRING_ENTRY(lazyPointer),
  LLD_KIND_STRING_ENTRY(lazyImmediateLocation),
  LLD_KIND_STRING_END
};

const ArchHandler::StubInfo ArchHandler_arm::_sStubInfoArmPIC = {
  "dyld_stub_binder",

  // References in lazy pointer  
  { Reference::KindArch::ARM, pointer32, 0, 0 },
  { Reference::KindArch::ARM, lazyPointer, 0, 0 },
  
  // GOT pointer to dyld_stub_binder
  { Reference::KindArch::ARM, pointer32, 0, 0 },

  // arm code alignment 2^2
  2, 
  
  // Stub size and code
  16, 
  { 0x04, 0xC0, 0x9F, 0xE5,       // 	ldr ip, pc + 12
    0x0C, 0xC0, 0x8F, 0xE0,       //  add ip, pc, ip
    0x00, 0xF0, 0x9C, 0xE5,       // 	ldr pc, [ip]
    0x00, 0x00, 0x00, 0x00 },     // 	.long L_foo$lazy_ptr - (L1$scv + 8)
  { Reference::KindArch::ARM, delta32, 12, 0 },
  
  // Stub Helper size and code
  12,
  { 0x00, 0xC0, 0x9F, 0xE5,       // ldr   ip, [pc, #0]
    0x00, 0x00, 0x00, 0xEA,       // b	     _helperhelper
    0x00, 0x00, 0x00, 0x00 },     // .long  lazy-info-offset
  { Reference::KindArch::ARM, lazyImmediateLocation, 8, 0 },
  { Reference::KindArch::ARM, arm_b24, 4, 0 },
  
  // Stub Helper-Common size and code
  36,
	{ // push lazy-info-offset
    0x04, 0xC0, 0x2D, 0xE5,       // str ip, [sp, #-4]!
		// push address of dyld_mageLoaderCache
    0x10, 0xC0, 0x9F, 0xE5,       // ldr	ip, L1
    0x0C, 0xC0, 0x8F, 0xE0,       // add	ip, pc, ip
    0x04, 0xC0, 0x2D, 0xE5,       // str ip, [sp, #-4]!
		// jump through dyld_stub_binder
    0x08, 0xC0, 0x9F, 0xE5,       // ldr	ip, L2
    0x0C, 0xC0, 0x8F, 0xE0,       // add	ip, pc, ip
    0x00, 0xF0, 0x9C, 0xE5,       // ldr	pc, [ip]
    0x00, 0x00, 0x00, 0x00,       // L1: .long fFastStubGOTAtom - (helper+16)
    0x00, 0x00, 0x00, 0x00 },     // L2: .long dyld_stub_binder - (helper+28)
  { Reference::KindArch::ARM, delta32, 28, 0xC },
  { Reference::KindArch::ARM, delta32, 32, 0x04 }
};

const ArchHandler::StubInfo &ArchHandler_arm::stubInfo() {
  // If multiple kinds of stubs are supported, select which StubInfo here.
  return _sStubInfoArmPIC;
}

bool ArchHandler_arm::isCallSite(const Reference &ref) {
  return (ref.kindValue() == thumb_b22) || (ref.kindValue() == arm_b24);
}

bool ArchHandler_arm::isPointer(const Reference &ref) {
  return (ref.kindValue() == pointer32);
}

bool ArchHandler_arm::isPairedReloc(const Relocation &reloc) {
  switch (reloc.type) {
  case ARM_RELOC_SECTDIFF:
  case ARM_RELOC_LOCAL_SECTDIFF:
  case ARM_RELOC_HALF_SECTDIFF:
  case ARM_RELOC_HALF:
    return true;
  default:
    return false;
  }
}

int32_t ArchHandler_arm::getDisplacementFromThumbBranch(uint32_t instruction) {
  uint32_t s = (instruction >> 10) & 0x1;
  uint32_t j1 = (instruction >> 29) & 0x1;
  uint32_t j2 = (instruction >> 27) & 0x1;
  uint32_t imm10 = instruction & 0x3FF;
  uint32_t imm11 = (instruction >> 16) & 0x7FF;
  uint32_t i1 = (j1 == s);
  uint32_t i2 = (j2 == s);
  uint32_t dis =
      (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
  int32_t sdis = dis;
  if (s)
    return (sdis | 0xFE000000);
  else
    return sdis;
}

int32_t ArchHandler_arm::getDisplacementFromArmBranch(uint32_t instruction) {
  // Sign-extend imm24
  int32_t displacement = (instruction & 0x00FFFFFF) << 2;
  if ((displacement & 0x02000000) != 0)
    displacement |= 0xFC000000;
  // If this is BLX and H bit set, add 2.
  if ((instruction & 0xFF000000) == 0xFB000000)
    displacement += 2;
  return displacement;
}

uint32_t ArchHandler_arm::setDisplacementInArmBranch(uint32_t instruction,
                                                     int32_t displacement) {
  // FIXME: handle BLX and out-of-range.
  uint32_t newInstruction = (instruction & 0xFF000000);
  newInstruction |= ((displacement >> 2) & 0x00FFFFFF);
  return newInstruction;
}

uint32_t ArchHandler_arm::setDisplacementInThumbBranch(uint32_t instruction,
                                                       int32_t displacement) {
  // FIXME: handle BLX and out-of-range.
  uint32_t newInstruction = (instruction & 0xF800D000);
  uint32_t s = (uint32_t)(displacement >> 24) & 0x1;
  uint32_t i1 = (uint32_t)(displacement >> 23) & 0x1;
  uint32_t i2 = (uint32_t)(displacement >> 22) & 0x1;
  uint32_t imm10 = (uint32_t)(displacement >> 12) & 0x3FF;
  uint32_t imm11 = (uint32_t)(displacement >> 1) & 0x7FF;
  uint32_t j1 = (i1 == s);
  uint32_t j2 = (i2 == s);
  uint32_t nextDisp = (j1 << 13) | (j2 << 11) | imm11;
  uint32_t firstDisp = (s << 10) | imm10;
  newInstruction |= (nextDisp << 16) | firstDisp;
  return newInstruction;
}

bool ArchHandler_arm::isThumbMovw(uint32_t instruction) {
  return (instruction & 0x8000FBF0) == 0x0000F240;
}

bool ArchHandler_arm::isThumbMovt(uint32_t instruction) {
  return (instruction & 0x8000FBF0) == 0x0000F2C0;
}

bool ArchHandler_arm::isArmMovw(uint32_t instruction) {
  return (instruction & 0x0FF00000) == 0x03000000;
}

bool ArchHandler_arm::isArmMovt(uint32_t instruction) {
  return (instruction & 0x0FF00000) == 0x03400000;
}


uint16_t ArchHandler_arm::getWordFromThumbMov(uint32_t instruction) {
  assert(isThumbMovw(instruction) || isThumbMovt(instruction));
  uint32_t i = ((instruction & 0x00000400) >> 10);
  uint32_t imm4 = (instruction & 0x0000000F);
  uint32_t imm3 = ((instruction & 0x70000000) >> 28);
  uint32_t imm8 = ((instruction & 0x00FF0000) >> 16);
  return (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
}

uint16_t ArchHandler_arm::getWordFromArmMov(uint32_t instruction) {
  assert(isArmMovw(instruction) || isArmMovt(instruction));
  uint32_t imm4 = ((instruction & 0x000F0000) >> 16);
  uint32_t imm12 = (instruction & 0x00000FFF);
  return (imm4 << 12) | imm12;
}


uint32_t ArchHandler_arm::setWordFromThumbMov(uint32_t instr, uint16_t word) {
  assert(isThumbMovw(instr) || isThumbMovt(instr));
  uint32_t imm4 = (word & 0xF000) >> 12;
  uint32_t i =    (word & 0x0800) >> 11;
  uint32_t imm3 = (word & 0x0700) >> 8;
  uint32_t imm8 =  word & 0x00FF;
	return (instr & 0x8F00FBF0) | imm4 | (i << 10) | (imm3 << 28) | (imm8 << 16);
}

uint32_t ArchHandler_arm::setWordFromArmMov(uint32_t instr, uint16_t word) {
  assert(isArmMovw(instr) || isArmMovt(instr));
  uint32_t imm4 = (word & 0xF000) >> 12;
  uint32_t imm12 = word & 0x0FFF;
  return (instr & 0xFFF0F000) | (imm4 << 16) | imm12;
}


uint32_t ArchHandler_arm::clearThumbBit(uint32_t value, const Atom *target) {
  // The assembler often adds one to the address of a thumb function.
  // We need to undo that so it does not look like an addend.
  if (value & 1) {
    if (isa<DefinedAtom>(target)) {
      const MachODefinedAtom *machoTarget =
          reinterpret_cast<const MachODefinedAtom *>(target);
      if (machoTarget->isThumb())
        value &= -2; // mask off thumb-bit
    }
  }
  return value;
}

std::error_code ArchHandler_arm::getReferenceInfo(
    const Relocation &reloc, const DefinedAtom *inAtom, uint32_t offsetInAtom,
    uint64_t fixupAddress, bool swap,
    FindAtomBySectionAndAddress atomFromAddress,
    FindAtomBySymbolIndex atomFromSymbolIndex, Reference::KindValue *kind,
    const lld::Atom **target, Reference::Addend *addend) {
  typedef std::error_code E;
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  uint64_t targetAddress;
  uint32_t instruction = readU32(swap, fixupContent);
  int32_t displacement;
  switch (relocPattern(reloc)) {
  case ARM_THUMB_RELOC_BR22 | rPcRel | rExtern | rLength4:
    // ex: bl _foo (and _foo is undefined)
    *kind = thumb_b22;
    if (E ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    // Instruction contains branch to addend.
    displacement = getDisplacementFromThumbBranch(instruction);
    *addend = fixupAddress + 4 + displacement;
    return std::error_code();
  case ARM_THUMB_RELOC_BR22 | rPcRel | rLength4:
    // ex: bl _foo (and _foo is defined)
    *kind = thumb_b22;
    displacement = getDisplacementFromThumbBranch(instruction);
    targetAddress = fixupAddress + 4 + displacement;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ARM_THUMB_RELOC_BR22 | rScattered | rPcRel | rLength4:
    // ex: bl _foo+4 (and _foo is defined)
    *kind = thumb_b22;
    displacement = getDisplacementFromThumbBranch(instruction);
    targetAddress = fixupAddress + 4 + displacement;
    if (E ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    // reloc.value is target atom's address.  Instruction contains branch
    // to atom+addend.
    *addend += (targetAddress - reloc.value);
    return std::error_code();
  case ARM_RELOC_BR24 | rPcRel | rExtern | rLength4:
    // ex: bl _foo (and _foo is undefined)
    *kind = arm_b24;
    if (E ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    // Instruction contains branch to addend.
    displacement = getDisplacementFromArmBranch(instruction);
    *addend = fixupAddress + 8 + displacement;
    return std::error_code();
  case ARM_RELOC_BR24 | rPcRel | rLength4:
    // ex: bl _foo (and _foo is defined)
    *kind = arm_b24;
    displacement = getDisplacementFromArmBranch(instruction);
    targetAddress = fixupAddress + 8 + displacement;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ARM_RELOC_BR24 | rScattered | rPcRel | rLength4:
    // ex: bl _foo+4 (and _foo is defined)
    *kind = arm_b24;
    displacement = getDisplacementFromArmBranch(instruction);
    targetAddress = fixupAddress + 8 + displacement;
    if (E ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    // reloc.value is target atom's address.  Instruction contains branch
    // to atom+addend.
    *addend += (targetAddress - reloc.value);
    return std::error_code();
  case ARM_RELOC_VANILLA | rExtern | rLength4:
    // ex: .long _foo (and _foo is undefined)
    *kind = pointer32;
    if (E ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = instruction;
    return std::error_code();
  case ARM_RELOC_VANILLA | rLength4:
    // ex: .long _foo (and _foo is defined)
    *kind = pointer32;
    if (E ec = atomFromAddress(reloc.symbol, instruction, target, addend))
      return ec;
    *addend = clearThumbBit((uint32_t) * addend, *target);
    return std::error_code();
  case ARM_RELOC_VANILLA | rScattered | rLength4:
    // ex: .long _foo+a (and _foo is defined)
    *kind = pointer32;
    if (E ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    *addend += (clearThumbBit(instruction, *target) - reloc.value);
    return std::error_code();
  default:
    return make_dynamic_error_code(Twine("unsupported arm relocation type"));
  }
  return std::error_code();
}

std::error_code
ArchHandler_arm::getPairReferenceInfo(const normalized::Relocation &reloc1,
                                     const normalized::Relocation &reloc2,
                                     const DefinedAtom *inAtom,
                                     uint32_t offsetInAtom,
                                     uint64_t fixupAddress, bool swap,
                                     FindAtomBySectionAndAddress atomFromAddr,
                                     FindAtomBySymbolIndex atomFromSymbolIndex,
                                     Reference::KindValue *kind,
                                     const lld::Atom **target,
                                     Reference::Addend *addend) {
  bool pointerDiff = false;
  bool funcRel;
  bool top;
  bool thumbReloc;
  switch(relocPattern(reloc1) << 16 | relocPattern(reloc2)) {
  case ((ARM_RELOC_HALF_SECTDIFF  | rScattered | rLenThmbLo) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLenThmbLo):
    // ex: movw	r1, :lower16:(_x-L1) [thumb mode]
    *kind = thumb_movw_funcRel;
    funcRel = true;
    top = false;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF_SECTDIFF  | rScattered | rLenThmbHi) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLenThmbHi):
    // ex: movt	r1, :upper16:(_x-L1) [thumb mode]
    *kind = thumb_movt_funcRel;
    funcRel = true;
    top = true;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF_SECTDIFF  | rScattered | rLenArmLo) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLenArmLo):
    // ex: movw	r1, :lower16:(_x-L1) [arm mode]
    *kind = arm_movw_funcRel;
    funcRel = true;
    top = false;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF_SECTDIFF  | rScattered | rLenArmHi) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLenArmHi):
    // ex: movt	r1, :upper16:(_x-L1) [arm mode]
    *kind = arm_movt_funcRel;
    funcRel = true;
    top = true;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF     | rLenThmbLo) << 16 |
         ARM_RELOC_PAIR     | rLenThmbLo):
    // ex: movw	r1, :lower16:_x [thumb mode]
    *kind = thumb_movw;
    funcRel = false;
    top = false;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF     | rLenThmbHi) << 16 |
         ARM_RELOC_PAIR     | rLenThmbHi):
    // ex: movt	r1, :upper16:_x [thumb mode]
    *kind = thumb_movt;
    funcRel = false;
    top = true;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF     | rLenArmLo) << 16 |
         ARM_RELOC_PAIR     | rLenArmLo):
    // ex: movw	r1, :lower16:_x [arm mode]
    *kind = arm_movw;
    funcRel = false;
    top = false;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF     | rLenArmHi) << 16 |
         ARM_RELOC_PAIR     | rLenArmHi):
    // ex: movt	r1, :upper16:_x [arm mode]
    *kind = arm_movt;
    funcRel = false;
    top = true;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF | rScattered  | rLenThmbLo) << 16 |
         ARM_RELOC_PAIR               | rLenThmbLo):
    // ex: movw	r1, :lower16:_x+a [thumb mode]
    *kind = thumb_movw;
    funcRel = false;
    top = false;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF | rScattered  | rLenThmbHi) << 16 |
         ARM_RELOC_PAIR               | rLenThmbHi):
    // ex: movt	r1, :upper16:_x+a [thumb mode]
    *kind = thumb_movt;
    funcRel = false;
    top = true;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF | rScattered  | rLenArmLo) << 16 |
         ARM_RELOC_PAIR               | rLenArmLo):
    // ex: movw	r1, :lower16:_x+a [arm mode]
    *kind = arm_movw;
    funcRel = false;
    top = false;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF | rScattered  | rLenArmHi) << 16 |
         ARM_RELOC_PAIR               | rLenArmHi):
    // ex: movt	r1, :upper16:_x+a [arm mode]
    *kind = arm_movt;
    funcRel = false;
    top = true;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF | rExtern   | rLenThmbLo) << 16 |
         ARM_RELOC_PAIR             | rLenThmbLo):
    // ex: movw	r1, :lower16:_undef [thumb mode]
    *kind = thumb_movw;
    funcRel = false;
    top = false;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF | rExtern   | rLenThmbHi) << 16 |
         ARM_RELOC_PAIR             | rLenThmbHi):
    // ex: movt	r1, :upper16:_undef [thumb mode]
    *kind = thumb_movt;
    funcRel = false;
    top = true;
    thumbReloc = true;
    break;
  case ((ARM_RELOC_HALF | rExtern   | rLenArmLo) << 16 |
         ARM_RELOC_PAIR             | rLenArmLo):
    // ex: movw	r1, :lower16:_undef [arm mode]
    *kind = arm_movw;
    funcRel = false;
    top = false;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_HALF | rExtern   | rLenArmHi) << 16 |
         ARM_RELOC_PAIR             | rLenArmHi):
    // ex: movt	r1, :upper16:_undef [arm mode]
    *kind = arm_movt;
    funcRel = false;
    top = true;
    thumbReloc = false;
    break;
  case ((ARM_RELOC_SECTDIFF       | rScattered | rLength4) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLength4):
  case ((ARM_RELOC_LOCAL_SECTDIFF | rScattered | rLength4) << 16 |
         ARM_RELOC_PAIR           | rScattered | rLength4):
    // ex: .long _foo - .
    pointerDiff = true;
    break;
  default:
    return make_dynamic_error_code(Twine("unsupported arm relocation pair"));
  }
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  std::error_code ec;
  uint32_t instruction = readU32(swap, fixupContent);
  uint32_t value;
  uint32_t fromAddress;
  uint32_t toAddress;
  uint16_t instruction16;
  uint16_t other16;
  const lld::Atom *fromTarget;
  Reference::Addend offsetInTo;
  Reference::Addend offsetInFrom;
  if (pointerDiff) {
    toAddress = reloc1.value;
    fromAddress = reloc2.value;
    ec = atomFromAddr(0, toAddress, target, &offsetInTo);
    if (ec)
      return ec;
    ec = atomFromAddr(0, fromAddress, &fromTarget, &offsetInFrom);
    if (ec)
      return ec;
    if (fromTarget != inAtom)
      return make_dynamic_error_code(Twine("SECTDIFF relocation where "
                                           "subtrahend label is not in atom"));
    *kind = delta32;
    value = clearThumbBit(instruction, *target);
    *addend = (int32_t)(value - (toAddress - fixupAddress));
  } else if (funcRel) {
    toAddress = reloc1.value;
    fromAddress = reloc2.value;
    ec = atomFromAddr(0, toAddress, target, &offsetInTo);
    if (ec)
      return ec;
    ec = atomFromAddr(0, fromAddress, &fromTarget, &offsetInFrom);
    if (ec)
      return ec;
    if (fromTarget != inAtom)
      return make_dynamic_error_code(
          Twine("ARM_RELOC_HALF_SECTDIFF relocation "
                "where subtrahend label is not in atom"));
    other16 = (reloc2.offset & 0xFFFF);
    if (thumbReloc) {
      if (top) {
        if (!isThumbMovt(instruction))
          return make_dynamic_error_code(Twine("expected movt instruction"));
      }
      else {
        if (!isThumbMovw(instruction))
          return make_dynamic_error_code(Twine("expected movw instruction"));
      }
      instruction16 = getWordFromThumbMov(instruction);
    }
    else {
      if (top) {
        if (!isArmMovt(instruction))
          return make_dynamic_error_code(Twine("expected movt instruction"));
      }
      else {
        if (!isArmMovw(instruction))
          return make_dynamic_error_code(Twine("expected movw instruction"));
      }
      instruction16 = getWordFromArmMov(instruction);
    }
    if (top)
      value = (instruction16 << 16) | other16;
    else
      value = (other16 << 16) | instruction16;
    value = clearThumbBit(value, *target);
    int64_t ta = (int64_t) value - (toAddress - fromAddress);
    *addend = ta - offsetInFrom;
    return std::error_code();
  } else {
    uint32_t sectIndex;
    if (thumbReloc) {
      if (top) {
        if (!isThumbMovt(instruction))
          return make_dynamic_error_code(Twine("expected movt instruction"));
      }
      else {
        if (!isThumbMovw(instruction))
          return make_dynamic_error_code(Twine("expected movw instruction"));
      }
      instruction16 = getWordFromThumbMov(instruction);
    }
    else {
      if (top) {
        if (!isArmMovt(instruction))
          return make_dynamic_error_code(Twine("expected movt instruction"));
      }
      else {
        if (!isArmMovw(instruction))
          return make_dynamic_error_code(Twine("expected movw instruction"));
      }
      instruction16 = getWordFromArmMov(instruction);
    }
    other16 = (reloc2.offset & 0xFFFF);
    if (top)
      value = (instruction16 << 16) | other16;
    else
      value = (other16 << 16) | instruction16;
    if (reloc1.isExtern) {
      ec = atomFromSymbolIndex(reloc1.symbol, target);
      if (ec)
        return ec;
      *addend = value;
    } else {
      if (reloc1.scattered) {
        toAddress = reloc1.value;
        sectIndex = 0;
      } else {
        toAddress = value;
        sectIndex = reloc1.symbol;
      }
      ec = atomFromAddr(sectIndex, toAddress, target, &offsetInTo);
      if (ec)
        return ec;
      *addend = value - toAddress;
    }
  }

  return std::error_code();
}

void ArchHandler_arm::applyFixupFinal(const Reference &ref, uint8_t *location,
                                      uint64_t fixupAddress,
                                      uint64_t targetAddress,
                                      uint64_t inAtomAddress,
                                      bool &thumbMode) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::ARM);
  int32_t *loc32 = reinterpret_cast<int32_t *>(location);
  int32_t displacement;
  uint16_t value16;
  switch (ref.kindValue()) {
  case modeThumbCode:
    thumbMode = true;
    break;
  case modeArmCode:
    thumbMode = false;
    break;
  case thumb_b22:
    assert(thumbMode);
    displacement = (targetAddress - (fixupAddress + 4)) + ref.addend();
    write32(*loc32, _swap, setDisplacementInThumbBranch(*loc32, displacement));
    break;
  case thumb_movw:
    assert(thumbMode);
    value16 = (targetAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movt:
    assert(thumbMode);
    value16 = (targetAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movw_funcRel:
    assert(thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movt_funcRel:
    assert(thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case arm_b24:
    assert(!thumbMode);
    displacement = (targetAddress - (fixupAddress + 8)) + ref.addend();
    *loc32 = setDisplacementInArmBranch(*loc32, displacement);
    break;
  case arm_movw:
    assert(!thumbMode);
    value16 = (targetAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movt:
    assert(!thumbMode);
    value16 = (targetAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movw_funcRel:
    assert(!thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movt_funcRel:
    assert(!thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case pointer32:
    write32(*loc32, _swap, targetAddress + ref.addend());
    break;
  case delta32:
    write32(*loc32, _swap, targetAddress - fixupAddress + ref.addend());
    break;
  case lazyPointer:
  case lazyImmediateLocation:
    // do nothing
    break;
  case invalid:
    llvm_unreachable("invalid ARM Reference Kind");
    break;
  }
}

void ArchHandler_arm::generateAtomContent(const DefinedAtom &atom,
                                           bool relocatable,
                                           FindAddressForAtom findAddress,
                                           uint8_t *atomContentBuffer) {
  // Copy raw bytes.
  memcpy(atomContentBuffer, atom.rawContent().data(), atom.size());
  // Apply fix-ups.
  bool thumbMode = false;
  for (const Reference *ref : atom) {
    uint32_t offset = ref->offsetInAtom();
    const Atom *target = ref->target();
    uint64_t targetAddress = 0;
    if (isa<DefinedAtom>(target))
      targetAddress = findAddress(*target);
    uint64_t atomAddress = findAddress(atom);
    uint64_t fixupAddress = atomAddress + offset;
    if (relocatable) {
      applyFixupRelocatable(*ref, &atomContentBuffer[offset],
                                        fixupAddress, targetAddress,
                                        atomAddress, thumbMode);
    } else {
      applyFixupFinal(*ref, &atomContentBuffer[offset],
                                  fixupAddress, targetAddress,
                                  atomAddress, thumbMode);
    }
  }
}


bool ArchHandler_arm::useExternalRelocationTo(const Atom &target) {
  // Undefined symbols are referenced via external relocations.
  if (isa<UndefinedAtom>(&target))
    return true;
  if (const DefinedAtom *defAtom = dyn_cast<DefinedAtom>(&target)) {
     switch (defAtom->merge()) {
     case DefinedAtom::mergeAsTentative:
       // Tentative definitions are referenced via external relocations.
       return true;
     case DefinedAtom::mergeAsWeak:
     case DefinedAtom::mergeAsWeakAndAddressUsed:
       // Global weak-defs are referenced via external relocations.
       return (defAtom->scope() == DefinedAtom::scopeGlobal);
     default:
       break;
    }
  }
  // Everything else is reference via an internal relocation.
  return false;
}

void ArchHandler_arm::applyFixupRelocatable(const Reference &ref,
                                             uint8_t *location,
                                             uint64_t fixupAddress,
                                             uint64_t targetAddress,
                                             uint64_t inAtomAddress,
                                             bool &thumbMode) {
  bool useExternalReloc = useExternalRelocationTo(*ref.target());
  int32_t *loc32 = reinterpret_cast<int32_t *>(location);
  int32_t displacement;
  uint16_t value16;
  switch (ref.kindValue()) {
  case modeThumbCode:
    thumbMode = true;
    break;
  case modeArmCode:
    thumbMode = false;
    break;
  case thumb_b22:
    assert(thumbMode);
    if (useExternalReloc)
      displacement = (ref.addend() - (fixupAddress + 4));
    else
      displacement = (targetAddress - (fixupAddress + 4)) + ref.addend();
    write32(*loc32, _swap, setDisplacementInThumbBranch(*loc32, displacement));
    break;
  case thumb_movw:
    assert(thumbMode);
    if (useExternalReloc)
      value16 = ref.addend() & 0xFFFF;
    else
      value16 = (targetAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movt:
    assert(thumbMode);
    if (useExternalReloc)
      value16 = ref.addend() >> 16;
    else
      value16 = (targetAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movw_funcRel:
    assert(thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case thumb_movt_funcRel:
    assert(thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromThumbMov(*loc32, value16));
    break;
  case arm_b24:
    assert(!thumbMode);
    if (useExternalReloc)
      displacement = (ref.addend() - (fixupAddress + 8));
    else
      displacement = (targetAddress - (fixupAddress + 8)) + ref.addend();
    write32(*loc32, _swap, setDisplacementInArmBranch(*loc32, displacement));
    break;
  case arm_movw:
    assert(!thumbMode);
    if (useExternalReloc)
      value16 = ref.addend() & 0xFFFF;
    else
      value16 = (targetAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movt:
    assert(!thumbMode);
    if (useExternalReloc)
      value16 = ref.addend() >> 16;
    else
      value16 = (targetAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movw_funcRel:
    assert(!thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) & 0xFFFF;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case arm_movt_funcRel:
    assert(!thumbMode);
    value16 = (targetAddress - inAtomAddress + ref.addend()) >> 16;
    write32(*loc32, _swap, setWordFromArmMov(*loc32, value16));
    break;
  case pointer32:
    write32(*loc32, _swap, targetAddress + ref.addend());
    break;
  case delta32:
    write32(*loc32, _swap, targetAddress - fixupAddress + ref.addend());
    break;
  case lazyPointer:
  case lazyImmediateLocation:
    // do nothing
    break;
  default:
    llvm_unreachable("invalid ARM Reference Kind");
    break;
  }
}

void ArchHandler_arm::appendSectionRelocations(
                                   const DefinedAtom &atom,
                                   uint64_t atomSectionOffset,
                                   const Reference &ref,
                                   FindSymbolIndexForAtom symbolIndexForAtom,
                                   FindSectionIndexForAtom sectionIndexForAtom,
                                   FindAddressForAtom addressForAtom,
                                   normalized::Relocations &relocs) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::ARM);
  uint32_t sectionOffset = atomSectionOffset + ref.offsetInAtom();
  bool useExternalReloc = useExternalRelocationTo(*ref.target());
  uint32_t targetAtomAddress;
  uint32_t fromAtomAddress;
  uint16_t other16;
  switch (ref.kindValue()) {
  case modeThumbCode:
  case modeArmCode:
    // Do nothing.
    break;
  case thumb_b22:
    if (useExternalReloc) {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_THUMB_RELOC_BR22 | rExtern    | rPcRel | rLength4);
    } else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                  ARM_THUMB_RELOC_BR22 | rScattered | rPcRel | rLength4);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  ARM_THUMB_RELOC_BR22 |              rPcRel | rLength4);
    }
    break;
  case thumb_movw:
    if (useExternalReloc) {
      other16 = ref.addend() >> 16;
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_RELOC_HALF | rExtern    | rLenThmbLo);
      appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenThmbLo);
    } else {
      targetAtomAddress = addressForAtom(*ref.target());
      if (ref.addend() != 0) {
        other16 = (targetAtomAddress + ref.addend()) >> 16;
        appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                  ARM_RELOC_HALF | rScattered | rLenThmbLo);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenThmbLo);
      } else {
        other16 = (targetAtomAddress + ref.addend()) >> 16;
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  ARM_RELOC_HALF              | rLenThmbLo);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenThmbLo);
      }
    }
    break;
  case thumb_movt:
    if (useExternalReloc) {
      other16 = ref.addend() & 0xFFFF;
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_RELOC_HALF | rExtern    | rLenThmbHi);
      appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenThmbHi);
    } else {
      targetAtomAddress = addressForAtom(*ref.target());
      if (ref.addend() != 0) {
        other16 = (targetAtomAddress + ref.addend()) & 0xFFFF;
        appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                    ARM_RELOC_HALF | rScattered | rLenThmbHi);
        appendReloc(relocs, other16, 0, 0,
                    ARM_RELOC_PAIR              | rLenThmbHi);
      } else {
        other16 = (targetAtomAddress + ref.addend()) & 0xFFFF;
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                    ARM_RELOC_HALF              | rLenThmbHi);
        appendReloc(relocs, other16, 0, 0,
                    ARM_RELOC_PAIR              | rLenThmbHi);
      }
    }
    break;
  case thumb_movw_funcRel:
    fromAtomAddress = addressForAtom(atom);
    targetAtomAddress = addressForAtom(*ref.target());
    other16 = (targetAtomAddress - fromAtomAddress + ref.addend()) >> 16;
    appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                ARM_RELOC_HALF_SECTDIFF | rScattered | rLenThmbLo);
    appendReloc(relocs, other16, 0, fromAtomAddress,
                ARM_RELOC_PAIR          | rScattered | rLenThmbLo);
    break;
  case thumb_movt_funcRel:
    fromAtomAddress = addressForAtom(atom);
    targetAtomAddress = addressForAtom(*ref.target());
    other16 = (targetAtomAddress - fromAtomAddress + ref.addend()) & 0xFFFF;
    appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                ARM_RELOC_HALF_SECTDIFF | rScattered | rLenThmbHi);
    appendReloc(relocs, other16, 0, fromAtomAddress,
                ARM_RELOC_PAIR          | rScattered | rLenThmbHi);
    break;
  case arm_b24:
    if (useExternalReloc) {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_RELOC_BR24 | rExtern    | rPcRel | rLength4);
    } else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                  ARM_RELOC_BR24 | rScattered | rPcRel | rLength4);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  ARM_RELOC_BR24 |              rPcRel | rLength4);
    }
    break;
  case arm_movw:
    if (useExternalReloc) {
      other16 = ref.addend() >> 16;
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_RELOC_HALF | rExtern    | rLenArmLo);
      appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmLo);
    } else {
      targetAtomAddress = addressForAtom(*ref.target());
      if (ref.addend() != 0) {
        other16 = (targetAtomAddress + ref.addend()) >> 16;
        appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                  ARM_RELOC_HALF | rScattered | rLenArmLo);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmLo);
      } else {
        other16 = (targetAtomAddress + ref.addend()) >> 16;
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  ARM_RELOC_HALF              | rLenArmLo);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmLo);
      }
    }
    break;
  case arm_movt:
    if (useExternalReloc) {
      other16 = ref.addend() & 0xFFFF;
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM_RELOC_HALF | rExtern    | rLenArmHi);
      appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmHi);
    } else {
      targetAtomAddress = addressForAtom(*ref.target());
      if (ref.addend() != 0) {
        other16 = (targetAtomAddress + ref.addend()) & 0xFFFF;
        appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                  ARM_RELOC_HALF | rScattered | rLenArmHi);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmHi);
      } else {
        other16 = (targetAtomAddress + ref.addend()) & 0xFFFF;
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  ARM_RELOC_HALF              | rLenArmHi);
        appendReloc(relocs, other16, 0, 0,
                  ARM_RELOC_PAIR              | rLenArmHi);
      }
    }
    break;
  case arm_movw_funcRel:
    fromAtomAddress = addressForAtom(atom);
    targetAtomAddress = addressForAtom(*ref.target());
    other16 = (targetAtomAddress - fromAtomAddress + ref.addend()) >> 16;
    appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                ARM_RELOC_HALF_SECTDIFF | rScattered | rLenArmLo);
    appendReloc(relocs, other16, 0, fromAtomAddress,
                ARM_RELOC_PAIR          | rScattered | rLenArmLo);
    break;
  case arm_movt_funcRel:
    fromAtomAddress = addressForAtom(atom);
    targetAtomAddress = addressForAtom(*ref.target());
    other16 = (targetAtomAddress - fromAtomAddress + ref.addend()) & 0xFFFF;
    appendReloc(relocs, sectionOffset, 0, targetAtomAddress,
                ARM_RELOC_HALF_SECTDIFF | rScattered | rLenArmHi);
    appendReloc(relocs, other16, 0, fromAtomAddress,
                ARM_RELOC_PAIR          | rScattered | rLenArmHi);
    break;
  case pointer32:
    if (useExternalReloc) {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()),  0,
                ARM_RELOC_VANILLA |    rExtern     |  rLength4);
    }
    else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                ARM_RELOC_VANILLA |    rScattered  |  rLength4);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                ARM_RELOC_VANILLA |                   rLength4);
    }
    break;
  case delta32:
    appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
              ARM_RELOC_SECTDIFF  |  rScattered    | rLength4);
    appendReloc(relocs, sectionOffset, 0, addressForAtom(atom) +
                                                           ref.offsetInAtom(),
              ARM_RELOC_PAIR      |  rScattered    | rLength4);
    break;
  case lazyPointer:
  case lazyImmediateLocation:
    // do nothing
    break;
  default:
    llvm_unreachable("invalid ARM Reference Kind");
    break;
  }
}

void ArchHandler_arm::addAdditionalReferences(MachODefinedAtom &atom) {
  if (atom.isThumb()) {
    atom.addReference(0, modeThumbCode, &atom, 0, Reference::KindArch::ARM);
  }
}

bool ArchHandler_arm::isThumbFunction(const DefinedAtom &atom) {
  for (const Reference *ref : atom) {
    if (ref->offsetInAtom() != 0)
      return false;
    if (ref->kindNamespace() != Reference::KindNamespace::mach_o)
      continue;
     assert(ref->kindArch() == Reference::KindArch::ARM);
    if (ref->kindValue() == modeThumbCode)
      return true;
  }
  return false;
}

std::unique_ptr<mach_o::ArchHandler> ArchHandler::create_arm() {
  return std::unique_ptr<mach_o::ArchHandler>(new ArchHandler_arm());
}

} // namespace mach_o
} // namespace lld
