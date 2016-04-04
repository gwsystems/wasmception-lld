//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// All symbols are handled as SymbolBodies regardless of their types.
// This file defines various types of SymbolBodies.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SYMBOLS_H
#define LLD_ELF_SYMBOLS_H

#include "InputSection.h"

#include "lld/Core/LLVM.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELF.h"

namespace lld {
namespace elf {

class ArchiveFile;
class InputFile;
class SymbolBody;
template <class ELFT> class ObjectFile;
template <class ELFT> class OutputSection;
template <class ELFT> class OutputSectionBase;
template <class ELFT> class SharedFile;

// Returns a demangled C++ symbol name. If Name is not a mangled
// name or the system does not provide __cxa_demangle function,
// it returns the unmodified string.
std::string demangle(StringRef Name);

// A real symbol object, SymbolBody, is usually accessed indirectly
// through a Symbol. There's always one Symbol for each symbol name.
// The resolver updates SymbolBody pointers as it resolves symbols.
struct Symbol {
  SymbolBody *Body;
};

// The base class for real symbol classes.
class SymbolBody {
public:
  enum Kind {
    DefinedFirst,
    DefinedRegularKind = DefinedFirst,
    SharedKind,
    DefinedCommonKind,
    DefinedBitcodeKind,
    DefinedSyntheticKind,
    DefinedLast = DefinedSyntheticKind,
    UndefinedElfKind,
    UndefinedKind,
    LazyKind
  };

  Kind kind() const { return static_cast<Kind>(SymbolKind); }

  bool isWeak() const { return Binding == llvm::ELF::STB_WEAK; }
  bool isUndefined() const {
    return SymbolKind == UndefinedKind || SymbolKind == UndefinedElfKind;
  }
  bool isDefined() const { return SymbolKind <= DefinedLast; }
  bool isCommon() const { return SymbolKind == DefinedCommonKind; }
  bool isLazy() const { return SymbolKind == LazyKind; }
  bool isShared() const { return SymbolKind == SharedKind; }
  bool isLocal() const { return Binding == llvm::ELF::STB_LOCAL; }
  bool isUsedInRegularObj() const { return IsUsedInRegularObj; }
  bool isPreemptible() const;

  // Returns the symbol name.
  StringRef getName() const {
    assert(!isLocal());
    return Name;
  }
  uint32_t getNameOffset() const {
    assert(isLocal());
    return NameOffset;
  }

  uint8_t getVisibility() const { return Other & 0x3; }

  unsigned DynsymIndex = 0;
  uint32_t GlobalDynIndex = -1;
  uint32_t GotIndex = -1;
  uint32_t GotPltIndex = -1;
  uint32_t PltIndex = -1;
  uint32_t ThunkIndex = -1;
  bool hasGlobalDynIndex() { return GlobalDynIndex != uint32_t(-1); }
  bool isInGot() const { return GotIndex != -1U; }
  bool isInPlt() const { return PltIndex != -1U; }
  bool hasThunk() const { return ThunkIndex != -1U; }

  void setUsedInRegularObj() { IsUsedInRegularObj = true; }

  template <class ELFT>
  typename ELFT::uint getVA(typename ELFT::uint Addend = 0) const;

  template <class ELFT> typename ELFT::uint getGotVA() const;
  template <class ELFT> typename ELFT::uint getGotPltVA() const;
  template <class ELFT> typename ELFT::uint getPltVA() const;
  template <class ELFT> typename ELFT::uint getThunkVA() const;
  template <class ELFT> typename ELFT::uint getSize() const;

  // A SymbolBody has a backreference to a Symbol. Originally they are
  // doubly-linked. A backreference will never change. But the pointer
  // in the Symbol may be mutated by the resolver. If you have a
  // pointer P to a SymbolBody and are not sure whether the resolver
  // has chosen the object among other objects having the same name,
  // you can access P->Backref->Body to get the resolver's result.
  void setBackref(Symbol *P) { Backref = P; }
  SymbolBody &repl() { return Backref ? *Backref->Body : *this; }
  Symbol *getSymbol() const { return Backref; }

  // Decides which symbol should "win" in the symbol table, this or
  // the Other. Returns 1 if this wins, -1 if the Other wins, or 0 if
  // they are duplicate (conflicting) symbols.
  template <class ELFT> int compare(SymbolBody *Other);

protected:
  SymbolBody(Kind K, StringRef Name, uint8_t Binding, uint8_t Other,
             uint8_t Type)
      : SymbolKind(K), MustBeInDynSym(false), NeedsCopyOrPltAddr(false),
        Type(Type), Binding(Binding), Other(Other), Name(Name) {
    assert(!isLocal());
    IsUsedInRegularObj =
        K != SharedKind && K != LazyKind && K != DefinedBitcodeKind;
  }

  SymbolBody(Kind K, uint32_t NameOffset, uint8_t Other, uint8_t Type);

  const unsigned SymbolKind : 8;

  // True if the symbol was used for linking and thus need to be
  // added to the output file's symbol table. It is usually true,
  // but if it is a shared symbol that were not referenced by anyone,
  // it can be false.
  unsigned IsUsedInRegularObj : 1;

public:
  // If true, the symbol is added to .dynsym symbol table.
  unsigned MustBeInDynSym : 1;

  // True if the linker has to generate a copy relocation for this shared
  // symbol or if the symbol should point to its plt entry.
  unsigned NeedsCopyOrPltAddr : 1;

  uint8_t Type;
  uint8_t Binding;
  uint8_t Other;
  bool isSection() const { return Type == llvm::ELF::STT_SECTION; }
  bool isTls() const { return Type == llvm::ELF::STT_TLS; }
  bool isFunc() const { return Type == llvm::ELF::STT_FUNC; }
  bool isGnuIFunc() const { return Type == llvm::ELF::STT_GNU_IFUNC; }
  bool isObject() const { return Type == llvm::ELF::STT_OBJECT; }
  bool isFile() const { return Type == llvm::ELF::STT_FILE; }
  void setVisibility(uint8_t V) { Other = (Other & ~0x3) | V; }

protected:
  union {
    StringRef Name;
    uint32_t NameOffset;
  };
  Symbol *Backref = nullptr;
};

// The base class for any defined symbols.
class Defined : public SymbolBody {
public:
  Defined(Kind K, StringRef Name, uint8_t Binding, uint8_t Other, uint8_t Type);
  Defined(Kind K, uint32_t NameOffset, uint8_t Other, uint8_t Type);
  static bool classof(const SymbolBody *S) { return S->isDefined(); }
};

// The defined symbol in LLVM bitcode files.
class DefinedBitcode : public Defined {
public:
  DefinedBitcode(StringRef Name, bool IsWeak, uint8_t Other);
  static bool classof(const SymbolBody *S);
};

class DefinedCommon : public Defined {
public:
  DefinedCommon(StringRef N, uint64_t Size, uint64_t Alignment, uint8_t Binding,
                uint8_t Other, uint8_t Type);

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedCommonKind;
  }

  // The output offset of this common symbol in the output bss. Computed by the
  // writer.
  uint64_t OffsetInBss;

  // The maximum alignment we have seen for this symbol.
  uint64_t Alignment;

  uint64_t Size;
};

// Regular defined symbols read from object file symbol tables.
template <class ELFT> class DefinedRegular : public Defined {
  typedef typename ELFT::Sym Elf_Sym;
  typedef typename ELFT::uint uintX_t;

public:
  DefinedRegular(StringRef Name, const Elf_Sym &Sym,
                 InputSectionBase<ELFT> *Section)
      : Defined(SymbolBody::DefinedRegularKind, Name, Sym.getBinding(),
                Sym.st_other, Sym.getType()),
        Value(Sym.st_value), Size(Sym.st_size),
        Section(Section ? Section->Repl : NullInputSection) {}

  DefinedRegular(uint32_t NameOffset, const Elf_Sym &Sym,
                 InputSectionBase<ELFT> *Section)
      : Defined(SymbolBody::DefinedRegularKind, NameOffset, Sym.st_other,
                Sym.getType()),
        Value(Sym.st_value), Size(Sym.st_size),
        Section(Section ? Section->Repl : NullInputSection) {
    assert(isLocal());
  }

  DefinedRegular(StringRef Name, uint8_t Binding, uint8_t Other)
      : Defined(SymbolBody::DefinedRegularKind, Name, Binding, Other,
                llvm::ELF::STT_NOTYPE),
        Value(0), Size(0), Section(NullInputSection) {}

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedRegularKind;
  }

  uintX_t Value;
  uintX_t Size;

  // The input section this symbol belongs to. Notice that this is
  // a reference to a pointer. We are using two levels of indirections
  // because of ICF. If ICF decides two sections need to be merged, it
  // manipulates this Section pointers so that they point to the same
  // section. This is a bit tricky, so be careful to not be confused.
  // If this is null, the symbol is an absolute symbol.
  InputSectionBase<ELFT> *&Section;

private:
  static InputSectionBase<ELFT> *NullInputSection;
};

template <class ELFT>
InputSectionBase<ELFT> *DefinedRegular<ELFT>::NullInputSection;

// DefinedSynthetic is a class to represent linker-generated ELF symbols.
// The difference from the regular symbol is that DefinedSynthetic symbols
// don't belong to any input files or sections. Thus, its constructor
// takes an output section to calculate output VA, etc.
template <class ELFT> class DefinedSynthetic : public Defined {
public:
  typedef typename ELFT::uint uintX_t;
  DefinedSynthetic(StringRef N, uintX_t Value, OutputSectionBase<ELFT> &Section,
                   uint8_t Other);

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedSyntheticKind;
  }

  // Special value designates that the symbol 'points'
  // to the end of the section.
  static const uintX_t SectionEnd = uintX_t(-1);

  uintX_t Value;
  const OutputSectionBase<ELFT> &Section;
};

// Undefined symbol.
class Undefined : public SymbolBody {
  typedef SymbolBody::Kind Kind;
  bool CanKeepUndefined;

protected:
  Undefined(Kind K, StringRef N, uint8_t Binding, uint8_t Other, uint8_t Type);
  Undefined(Kind K, uint32_t NameOffset, uint8_t Other, uint8_t Type);

public:
  Undefined(StringRef N, bool IsWeak, uint8_t Other, bool CanKeepUndefined);

  static bool classof(const SymbolBody *S) { return S->isUndefined(); }

  bool canKeepUndefined() const { return CanKeepUndefined; }
};

template <class ELFT> class UndefinedElf : public Undefined {
  typedef typename ELFT::uint uintX_t;
  typedef typename ELFT::Sym Elf_Sym;

public:
  UndefinedElf(StringRef N, const Elf_Sym &Sym);
  UndefinedElf(uint32_t NameOffset, const Elf_Sym &Sym);

  uintX_t Size;

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::UndefinedElfKind;
  }
};

template <class ELFT> class SharedSymbol : public Defined {
  typedef typename ELFT::Sym Elf_Sym;
  typedef typename ELFT::uint uintX_t;

public:
  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::SharedKind;
  }

  SharedSymbol(SharedFile<ELFT> *F, StringRef Name, const Elf_Sym &Sym)
      : Defined(SymbolBody::SharedKind, Name, Sym.getBinding(), Sym.st_other,
                Sym.getType()),
        File(F), Sym(Sym) {}

  SharedFile<ELFT> *File;
  const Elf_Sym &Sym;

  // OffsetInBss is significant only when needsCopy() is true.
  uintX_t OffsetInBss = 0;

  bool needsCopy() const { return this->NeedsCopyOrPltAddr && !this->isFunc(); }
};

// This class represents a symbol defined in an archive file. It is
// created from an archive file header, and it knows how to load an
// object file from an archive to replace itself with a defined
// symbol. If the resolver finds both Undefined and Lazy for
// the same name, it will ask the Lazy to load a file.
class Lazy : public SymbolBody {
public:
  Lazy(ArchiveFile *F, const llvm::object::Archive::Symbol S)
      : SymbolBody(LazyKind, S.getName(), llvm::ELF::STB_GLOBAL,
                   llvm::ELF::STV_DEFAULT, /* Type */ 0),
        File(F), Sym(S) {}

  static bool classof(const SymbolBody *S) { return S->kind() == LazyKind; }

  // Returns an object file for this symbol, or a nullptr if the file
  // was already returned.
  std::unique_ptr<InputFile> getMember();

private:
  ArchiveFile *File;
  const llvm::object::Archive::Symbol Sym;
};

// Some linker-generated symbols need to be created as
// DefinedRegular symbols.
template <class ELFT> struct ElfSym {
  // The content for _etext and etext symbols.
  static DefinedRegular<ELFT> *Etext;
  static DefinedRegular<ELFT> *Etext2;

  // The content for _edata and edata symbols.
  static DefinedRegular<ELFT> *Edata;
  static DefinedRegular<ELFT> *Edata2;

  // The content for _end and end symbols.
  static DefinedRegular<ELFT> *End;
  static DefinedRegular<ELFT> *End2;

  // The content for _gp symbol for MIPS target.
  static DefinedRegular<ELFT> *MipsGp;

  // __rel_iplt_start/__rel_iplt_end for signaling
  // where R_[*]_IRELATIVE relocations do live.
  static DefinedRegular<ELFT> *RelaIpltStart;
  static DefinedRegular<ELFT> *RelaIpltEnd;
};

template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Etext;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Etext2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Edata;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Edata2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::End;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::End2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::MipsGp;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::RelaIpltStart;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::RelaIpltEnd;

} // namespace elf
} // namespace lld

#endif
