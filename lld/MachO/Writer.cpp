//===- Writer.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Writer.h"
#include "ConcatOutputSection.h"
#include "Config.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "LinkerOptimizationHints.h"
#include "MapFile.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "SectionPriorities.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "UnwindInfoSection.h"

#include "lld/Common/Arrays.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/thread.h"
#include "llvm/Support/xxhash.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::sys;
using namespace lld;
using namespace lld::macho;

namespace {
class LCUuid;

class Writer {
public:
  Writer(Ctx &ctx) : ctx(ctx), buffer(ctx.e.outputBuffer) {}

  void treatSpecialUndefineds();
  void scanRelocations();
  void scanSymbols();
  template <class LP> void createOutputSections();
  template <class LP> void createLoadCommands();
  void finalizeAddresses();
  void finalizeLinkEditSegment();
  void assignAddresses(OutputSegment *);

  void openFile();
  void writeSections();
  void applyOptimizationHints();
  void buildFixupChains();
  void writeUuid();
  void writeCodeSignature();
  void writeOutputFile();

  template <class LP> void run();

  Ctx &ctx;
  std::unique_ptr<FileOutputBuffer> &buffer;
  uint64_t addr = 0;
  uint64_t fileOff = 0;
  MachHeaderSection *header = nullptr;
  StringTableSection *stringTableSection = nullptr;
  SymtabSection *symtabSection = nullptr;
  IndirectSymtabSection *indirectSymtabSection = nullptr;
  CodeSignatureSection *codeSignatureSection = nullptr;
  DataInCodeSection *dataInCodeSection = nullptr;
  FunctionStartsSection *functionStartsSection = nullptr;

  LCUuid *uuidCommand = nullptr;
  OutputSegment *linkEditSegment = nullptr;
};

// LC_DYLD_INFO_ONLY stores the offsets of symbol import/export information.
class LCDyldInfo final : public LoadCommand {
public:
  LCDyldInfo(RebaseSection *rebaseSection, BindingSection *bindingSection,
             WeakBindingSection *weakBindingSection,
             LazyBindingSection *lazyBindingSection,
             ExportSection *exportSection)
      : rebaseSection(rebaseSection), bindingSection(bindingSection),
        weakBindingSection(weakBindingSection),
        lazyBindingSection(lazyBindingSection), exportSection(exportSection) {}

  uint32_t getSize() const override { return sizeof(dyld_info_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<dyld_info_command *>(buf);
    c->cmd = LC_DYLD_INFO_ONLY;
    c->cmdsize = getSize();
    if (rebaseSection->isNeeded()) {
      c->rebase_off = rebaseSection->fileOff;
      c->rebase_size = rebaseSection->getFileSize();
    }
    if (bindingSection->isNeeded()) {
      c->bind_off = bindingSection->fileOff;
      c->bind_size = bindingSection->getFileSize();
    }
    if (weakBindingSection->isNeeded()) {
      c->weak_bind_off = weakBindingSection->fileOff;
      c->weak_bind_size = weakBindingSection->getFileSize();
    }
    if (lazyBindingSection->isNeeded()) {
      c->lazy_bind_off = lazyBindingSection->fileOff;
      c->lazy_bind_size = lazyBindingSection->getFileSize();
    }
    if (exportSection->isNeeded()) {
      c->export_off = exportSection->fileOff;
      c->export_size = exportSection->getFileSize();
    }
  }

  RebaseSection *rebaseSection;
  BindingSection *bindingSection;
  WeakBindingSection *weakBindingSection;
  LazyBindingSection *lazyBindingSection;
  ExportSection *exportSection;
};

class LCSubFramework final : public LoadCommand {
public:
  LCSubFramework(Ctx &ctx, StringRef umbrella) : ctx(ctx), umbrella(umbrella) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(sub_framework_command) + umbrella.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<sub_framework_command *>(buf);
    buf += sizeof(sub_framework_command);

    c->cmd = LC_SUB_FRAMEWORK;
    c->cmdsize = getSize();
    c->umbrella = sizeof(sub_framework_command);

    memcpy(buf, umbrella.data(), umbrella.size());
    buf[umbrella.size()] = '\0';
  }

private:
  Ctx &ctx;
  const StringRef umbrella;
};

class LCFunctionStarts final : public LoadCommand {
public:
  explicit LCFunctionStarts(FunctionStartsSection *functionStartsSection)
      : functionStartsSection(functionStartsSection) {}

  uint32_t getSize() const override { return sizeof(linkedit_data_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<linkedit_data_command *>(buf);
    c->cmd = LC_FUNCTION_STARTS;
    c->cmdsize = getSize();
    c->dataoff = functionStartsSection->fileOff;
    c->datasize = functionStartsSection->getFileSize();
  }

private:
  FunctionStartsSection *functionStartsSection;
};

class LCDataInCode final : public LoadCommand {
public:
  explicit LCDataInCode(DataInCodeSection *dataInCodeSection)
      : dataInCodeSection(dataInCodeSection) {}

  uint32_t getSize() const override { return sizeof(linkedit_data_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<linkedit_data_command *>(buf);
    c->cmd = LC_DATA_IN_CODE;
    c->cmdsize = getSize();
    c->dataoff = dataInCodeSection->fileOff;
    c->datasize = dataInCodeSection->getFileSize();
  }

private:
  DataInCodeSection *dataInCodeSection;
};

class LCDysymtab final : public LoadCommand {
public:
  LCDysymtab(SymtabSection *symtabSection,
             IndirectSymtabSection *indirectSymtabSection)
      : symtabSection(symtabSection),
        indirectSymtabSection(indirectSymtabSection) {}

  uint32_t getSize() const override { return sizeof(dysymtab_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<dysymtab_command *>(buf);
    c->cmd = LC_DYSYMTAB;
    c->cmdsize = getSize();

    c->ilocalsym = 0;
    c->iextdefsym = c->nlocalsym = symtabSection->getNumLocalSymbols();
    c->nextdefsym = symtabSection->getNumExternalSymbols();
    c->iundefsym = c->iextdefsym + c->nextdefsym;
    c->nundefsym = symtabSection->getNumUndefinedSymbols();

    c->indirectsymoff = indirectSymtabSection->fileOff;
    c->nindirectsyms = indirectSymtabSection->getNumSymbols();
  }

  SymtabSection *symtabSection;
  IndirectSymtabSection *indirectSymtabSection;
};

template <class LP> class LCSegment final : public LoadCommand {
public:
  LCSegment(StringRef name, OutputSegment *seg) : name(name), seg(seg) {}

  uint32_t getSize() const override {
    return sizeof(typename LP::segment_command) +
           seg->numNonHiddenSections() * sizeof(typename LP::section);
  }

  void writeTo(uint8_t *buf) const override {
    using SegmentCommand = typename LP::segment_command;
    using SectionHeader = typename LP::section;

    auto *c = reinterpret_cast<SegmentCommand *>(buf);
    buf += sizeof(SegmentCommand);

    c->cmd = LP::segmentLCType;
    c->cmdsize = getSize();
    memcpy(c->segname, name.data(), name.size());
    c->fileoff = seg->fileOff;
    c->maxprot = seg->maxProt;
    c->initprot = seg->initProt;

    c->vmaddr = seg->addr;
    c->vmsize = seg->vmSize;
    c->filesize = seg->fileSize;
    c->nsects = seg->numNonHiddenSections();
    c->flags = seg->flags;

    for (const OutputSection *osec : seg->getSections()) {
      if (osec->isHidden())
        continue;

      auto *sectHdr = reinterpret_cast<SectionHeader *>(buf);
      buf += sizeof(SectionHeader);

      memcpy(sectHdr->sectname, osec->name.data(), osec->name.size());
      memcpy(sectHdr->segname, name.data(), name.size());

      sectHdr->addr = osec->addr;
      sectHdr->offset = osec->fileOff;
      sectHdr->align = Log2_32(osec->align);
      sectHdr->flags = osec->flags;
      sectHdr->size = osec->getSize();
      sectHdr->reserved1 = osec->reserved1;
      sectHdr->reserved2 = osec->reserved2;
    }
  }

private:
  StringRef name;
  OutputSegment *seg;
};

class LCMain final : public LoadCommand {
public:
  explicit LCMain(Ctx &ctx) : ctx(ctx) {}

  uint32_t getSize() const override {
    return sizeof(structs::entry_point_command);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<structs::entry_point_command *>(buf);
    c->cmd = LC_MAIN;
    c->cmdsize = getSize();

    if (ctx.arg.entry->isInStubs())
      c->entryoff = ctx.in.stubs->fileOff +
                    ctx.arg.entry->stubsIndex * ctx.target->stubSize;
    else
      c->entryoff = ctx.arg.entry->getVA(ctx) - ctx.in.header->addr;

    c->stacksize = 0;
  }

private:
  Ctx &ctx;
};

class LCSymtab final : public LoadCommand {
public:
  LCSymtab(SymtabSection *symtabSection, StringTableSection *stringTableSection)
      : symtabSection(symtabSection), stringTableSection(stringTableSection) {}

  uint32_t getSize() const override { return sizeof(symtab_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<symtab_command *>(buf);
    c->cmd = LC_SYMTAB;
    c->cmdsize = getSize();
    c->symoff = symtabSection->fileOff;
    c->nsyms = symtabSection->getNumSymbols();
    c->stroff = stringTableSection->fileOff;
    c->strsize = stringTableSection->getFileSize();
  }

  SymtabSection *symtabSection = nullptr;
  StringTableSection *stringTableSection = nullptr;
};

// There are several dylib load commands that share the same structure:
//   * LC_LOAD_DYLIB
//   * LC_ID_DYLIB
//   * LC_REEXPORT_DYLIB
class LCDylib final : public LoadCommand {
public:
  LCDylib(Ctx &ctx, LoadCommandType type, StringRef path,
          uint32_t compatibilityVersion = 0, uint32_t currentVersion = 0)
      : ctx(ctx), type(type), path(path),
        compatibilityVersion(compatibilityVersion),
        currentVersion(currentVersion) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(dylib_command) + path.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<dylib_command *>(buf);
    buf += sizeof(dylib_command);

    c->cmd = type;
    c->cmdsize = getSize();
    c->dylib.name = sizeof(dylib_command);
    c->dylib.timestamp = 0;
    c->dylib.compatibility_version = compatibilityVersion;
    c->dylib.current_version = currentVersion;

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

private:
  Ctx &ctx;
  LoadCommandType type;
  StringRef path;
  uint32_t compatibilityVersion;
  uint32_t currentVersion;
};

class LCLoadDylinker final : public LoadCommand {
public:
  explicit LCLoadDylinker(Ctx &ctx) : ctx(ctx) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(dylinker_command) + path.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<dylinker_command *>(buf);
    buf += sizeof(dylinker_command);

    c->cmd = LC_LOAD_DYLINKER;
    c->cmdsize = getSize();
    c->name = sizeof(dylinker_command);

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

private:
  Ctx &ctx;
  // Recent versions of Darwin won't run any binary that has dyld at a
  // different location.
  const StringRef path = "/usr/lib/dyld";
};

class LCRPath final : public LoadCommand {
public:
  LCRPath(Ctx &ctx, StringRef path) : ctx(ctx), path(path) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(rpath_command) + path.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<rpath_command *>(buf);
    buf += sizeof(rpath_command);

    c->cmd = LC_RPATH;
    c->cmdsize = getSize();
    c->path = sizeof(rpath_command);

    memcpy(buf, path.data(), path.size());
    buf[path.size()] = '\0';
  }

private:
  Ctx &ctx;
  StringRef path;
};

class LCSubClient final : public LoadCommand {
public:
  LCSubClient(Ctx &ctx, StringRef client) : ctx(ctx), client(client) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(sub_client_command) + client.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<sub_client_command *>(buf);
    buf += sizeof(sub_client_command);

    c->cmd = LC_SUB_CLIENT;
    c->cmdsize = getSize();
    c->client = sizeof(sub_client_command);

    memcpy(buf, client.data(), client.size());
    buf[client.size()] = '\0';
  }

private:
  Ctx &ctx;
  StringRef client;
};

class LCDyldEnv final : public LoadCommand {
public:
  LCDyldEnv(Ctx &ctx, StringRef name) : ctx(ctx), name(name) {}

  uint32_t getSize() const override {
    return alignToPowerOf2(sizeof(dyld_env_command) + name.size() + 1,
                           ctx.target->wordSize);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<dyld_env_command *>(buf);
    buf += sizeof(dyld_env_command);

    c->cmd = LC_DYLD_ENVIRONMENT;
    c->cmdsize = getSize();
    c->name = sizeof(dyld_env_command);

    memcpy(buf, name.data(), name.size());
    buf[name.size()] = '\0';
  }

private:
  Ctx &ctx;
  StringRef name;
};

class LCMinVersion final : public LoadCommand {
public:
  explicit LCMinVersion(const PlatformInfo &platformInfo)
      : platformInfo(platformInfo) {}

  uint32_t getSize() const override { return sizeof(version_min_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<version_min_command *>(buf);
    switch (platformInfo.target.Platform) {
    case PLATFORM_MACOS:
      c->cmd = LC_VERSION_MIN_MACOSX;
      break;
    case PLATFORM_IOS:
    case PLATFORM_IOSSIMULATOR:
      c->cmd = LC_VERSION_MIN_IPHONEOS;
      break;
    case PLATFORM_TVOS:
    case PLATFORM_TVOSSIMULATOR:
      c->cmd = LC_VERSION_MIN_TVOS;
      break;
    case PLATFORM_WATCHOS:
    case PLATFORM_WATCHOSSIMULATOR:
      c->cmd = LC_VERSION_MIN_WATCHOS;
      break;
    default:
      llvm_unreachable("invalid platform");
      break;
    }
    c->cmdsize = getSize();
    c->version = encodeVersion(platformInfo.target.MinDeployment);
    c->sdk = encodeVersion(platformInfo.sdk);
  }

private:
  const PlatformInfo &platformInfo;
};

class LCBuildVersion final : public LoadCommand {
public:
  explicit LCBuildVersion(const PlatformInfo &platformInfo)
      : platformInfo(platformInfo) {}

  const int ntools = 1;

  uint32_t getSize() const override {
    return sizeof(build_version_command) + ntools * sizeof(build_tool_version);
  }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<build_version_command *>(buf);
    c->cmd = LC_BUILD_VERSION;
    c->cmdsize = getSize();

    c->platform = static_cast<uint32_t>(platformInfo.target.Platform);
    c->minos = encodeVersion(platformInfo.target.MinDeployment);
    c->sdk = encodeVersion(platformInfo.sdk);

    c->ntools = ntools;
    auto *t = reinterpret_cast<build_tool_version *>(&c[1]);
    t->tool = TOOL_LLD;
    t->version = encodeVersion(VersionTuple(
        LLVM_VERSION_MAJOR, LLVM_VERSION_MINOR, LLVM_VERSION_PATCH));
  }

private:
  const PlatformInfo &platformInfo;
};

// Stores a unique identifier for the output file based on an MD5 hash of its
// contents. In order to hash the contents, we must first write them, but
// LC_UUID itself must be part of the written contents in order for all the
// offsets to be calculated correctly. We resolve this circular paradox by
// first writing an LC_UUID with an all-zero UUID, then updating the UUID with
// its real value later.
class LCUuid final : public LoadCommand {
public:
  uint32_t getSize() const override { return sizeof(uuid_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<uuid_command *>(buf);
    c->cmd = LC_UUID;
    c->cmdsize = getSize();
    uuidBuf = c->uuid;
  }

  void writeUuid(uint64_t digest) const {
    // xxhash only gives us 8 bytes, so put some fixed data in the other half.
    static_assert(sizeof(uuid_command::uuid) == 16, "unexpected uuid size");
    memcpy(uuidBuf, "LLD\xa1UU1D", 8);
    memcpy(uuidBuf + 8, &digest, 8);

    // RFC 4122 conformance. We need to fix 4 bits in byte 6 and 2 bits in
    // byte 8. Byte 6 is already fine due to the fixed data we put in. We don't
    // want to lose bits of the digest in byte 8, so swap that with a byte of
    // fixed data that happens to have the right bits set.
    std::swap(uuidBuf[3], uuidBuf[8]);

    // Claim that this is an MD5-based hash. It isn't, but this signals that
    // this is not a time-based and not a random hash. MD5 seems like the least
    // bad lie we can put here.
    assert((uuidBuf[6] & 0xf0) == 0x30 && "See RFC 4122 Sections 4.2.2, 4.1.3");
    assert((uuidBuf[8] & 0xc0) == 0x80 && "See RFC 4122 Section 4.2.2");
  }

  mutable uint8_t *uuidBuf;
};

template <class LP> class LCEncryptionInfo final : public LoadCommand {
public:
  explicit LCEncryptionInfo(Ctx &ctx) : ctx(ctx) {}

  uint32_t getSize() const override {
    return sizeof(typename LP::encryption_info_command);
  }

  void writeTo(uint8_t *buf) const override {
    using EncryptionInfo = typename LP::encryption_info_command;
    auto *c = reinterpret_cast<EncryptionInfo *>(buf);
    buf += sizeof(EncryptionInfo);
    c->cmd = LP::encryptionInfoLCType;
    c->cmdsize = getSize();
    c->cryptoff = ctx.in.header->getSize();
    auto it = find_if(ctx.outputSegments, [](const OutputSegment *seg) {
      return seg->name == segment_names::text;
    });
    assert(it != ctx.outputSegments.end());
    c->cryptsize = (*it)->fileSize - c->cryptoff;
  }

private:
  Ctx &ctx;
};

class LCCodeSignature final : public LoadCommand {
public:
  LCCodeSignature(CodeSignatureSection *section) : section(section) {}

  uint32_t getSize() const override { return sizeof(linkedit_data_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<linkedit_data_command *>(buf);
    c->cmd = LC_CODE_SIGNATURE;
    c->cmdsize = getSize();
    c->dataoff = static_cast<uint32_t>(section->fileOff);
    c->datasize = section->getSize();
  }

  CodeSignatureSection *section;
};

class LCExportsTrie final : public LoadCommand {
public:
  LCExportsTrie(ExportSection *section) : section(section) {}

  uint32_t getSize() const override { return sizeof(linkedit_data_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<linkedit_data_command *>(buf);
    c->cmd = LC_DYLD_EXPORTS_TRIE;
    c->cmdsize = getSize();
    c->dataoff = section->fileOff;
    c->datasize = section->getSize();
  }

  ExportSection *section;
};

class LCChainedFixups final : public LoadCommand {
public:
  LCChainedFixups(ChainedFixupsSection *section) : section(section) {}

  uint32_t getSize() const override { return sizeof(linkedit_data_command); }

  void writeTo(uint8_t *buf) const override {
    auto *c = reinterpret_cast<linkedit_data_command *>(buf);
    c->cmd = LC_DYLD_CHAINED_FIXUPS;
    c->cmdsize = getSize();
    c->dataoff = section->fileOff;
    c->datasize = section->getSize();
  }

  ChainedFixupsSection *section;
};

} // namespace

void Writer::treatSpecialUndefineds() {
  if (ctx.arg.entry)
    if (auto *undefined = dyn_cast<Undefined>(ctx.arg.entry))
      treatUndefinedSymbol(ctx, *undefined, "the entry point");

  // FIXME: This prints symbols that are undefined both in input files and
  // via -u flag twice.
  for (const Symbol *sym : ctx.arg.explicitUndefineds) {
    if (const auto *undefined = dyn_cast<Undefined>(sym))
      treatUndefinedSymbol(ctx, *undefined, "-u");
  }
  // Literal exported-symbol names must be defined, but glob
  // patterns need not match.
  for (const CachedHashStringRef &cachedName :
       ctx.arg.exportedSymbols.literals) {
    if (const Symbol *sym = ctx.symtab->find(cachedName))
      if (const auto *undefined = dyn_cast<Undefined>(sym))
        treatUndefinedSymbol(ctx, *undefined, "-exported_symbol(s_list)");
  }
}

// Give a symbol its single non-lazy pointer slot. A GOT reference and a TLV
// reference to a thread-local both want the same value -- the address of its
// TLV descriptor -- so one __got entry serves both, and the result no longer
// depends on which reference the relocation scan reaches first.
static void addNonLazyPointerEntry(Ctx &ctx, Symbol *sym) {
  ctx.in.got->addEntry(sym);
}

static void prepareSymbolRelocation(Ctx &ctx, Symbol *sym,
                                    const InputSection *isec,
                                    const Relocation &r) {
  if (!sym->isLive()) {
    if (Defined *defined = dyn_cast<Defined>(sym)) {
      if (ctx.arg.emitInitOffsets &&
          defined->isec()->getName() == section_names::moduleInitFunc)
        ctx.e.fatal(isec->getLocation(r.offset) + ": cannot reference " +
                    sym->getName() +
                    " defined in __mod_init_func when -init_offsets is used");
    }
    assert(false && "referenced symbol must be live");
  }

  const RelocAttrs &relocAttrs = ctx.target->getRelocAttrs(r.type);

  if (relocAttrs.hasAttr(RelocAttrBits::BRANCH)) {
    if (needsBinding(sym))
      ctx.in.stubs->addEntry(sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::GOT)) {
    if (relocAttrs.hasAttr(RelocAttrBits::POINTER) || needsBinding(sym))
      addNonLazyPointerEntry(ctx, sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::TLV)) {
    if (needsBinding(sym))
      addNonLazyPointerEntry(ctx, sym);
  } else if (relocAttrs.hasAttr(RelocAttrBits::UNSIGNED)) {
    // References from thread-local variable sections are treated as offsets
    // relative to the start of the referent section, and therefore have no
    // need of rebase opcodes.
    if (!(isThreadLocalVariables(isec->getFlags()) && isa<Defined>(sym)))
      addNonLazyBindingEntries(ctx, sym, isec, r.offset, r.addend);
  }
}

void Writer::scanRelocations() {
  TimeTraceScope timeScope("Scan relocations");

  // This can't use a for-each loop: It calls treatUndefinedSymbol(), which can
  // add to inputSections, which invalidates inputSections's iterators.
  for (size_t i = 0; i < ctx.inputSections.size(); ++i) {
    ConcatInputSection *isec = ctx.inputSections[i];

    if (isec->shouldOmitFromOutput())
      continue;

    for (auto it = isec->relocs.begin(); it != isec->relocs.end(); ++it) {
      Relocation &r = *it;

      // Canonicalize the referent so that later accesses in Writer won't
      // have to worry about it.
      if (auto *referentIsec = dyn_cast_if_present<InputSection *>(r.referent))
        r.referent = referentIsec->canonical();

      if (ctx.target->hasAttr(r.type, RelocAttrBits::SUBTRAHEND)) {
        // Skip over the following UNSIGNED relocation -- it's just there as the
        // minuend, and doesn't have the usual UNSIGNED semantics. We don't want
        // to emit rebase opcodes for it.
        ++it;
        // Canonicalize the referent so that later accesses in Writer won't
        // have to worry about it.
        if (auto *referentIsec = it->referent.dyn_cast<InputSection *>())
          it->referent = referentIsec->canonical();
        continue;
      }
      if (auto *sym = dyn_cast_if_present<Symbol *>(r.referent)) {
        if (auto *undefined = dyn_cast<Undefined>(sym))
          treatUndefinedSymbol(ctx, *undefined, isec, r.offset);
        // treatUndefinedSymbol() can replace sym with a DylibSymbol; re-check.
        if (!isa<Undefined>(sym) && validateSymbolRelocation(sym, isec, r))
          prepareSymbolRelocation(ctx, sym, isec, r);
      } else {
        if (!r.pcrel) {
          if (ctx.arg.emitChainedFixups)
            ctx.in.chainedFixups->addRebase(isec, r.offset);
          else
            ctx.in.rebase->addEntry(isec, r.offset);
        }
      }
    }
  }

  ctx.in.unwindInfo->prepare();
}

static void addNonWeakDefinition(Ctx &ctx, const Defined *defined) {
  if (ctx.arg.emitChainedFixups)
    ctx.in.chainedFixups->setHasNonWeakDefinition();
  else
    ctx.in.weakBinding->addNonWeakDefinition(defined);
}

void Writer::scanSymbols() {
  TimeTraceScope timeScope("Scan symbols");
  ObjCSelRefsHelper::initialize(ctx);
  for (Symbol *sym : ctx.symtab->getSymbols()) {
    if (auto *defined = dyn_cast<Defined>(sym)) {
      if (!defined->isLive())
        continue;
      if (defined->overridesWeakDef)
        addNonWeakDefinition(ctx, defined);
      if (!defined->isAbsolute() && isCodeSection(defined->isec()))
        ctx.in.unwindInfo->addSymbol(defined);
    } else if (const auto *dysym = dyn_cast<DylibSymbol>(sym)) {
      // This branch intentionally doesn't check isLive().
      if (dysym->isDynamicLookup())
        continue;
      dysym->getFile()->refState =
          std::max(dysym->getFile()->refState, dysym->getRefState());
    } else if (isa<Undefined>(sym)) {
      if (ObjCStubsSection::isObjCStubSymbol(sym)) {
        // When -dead_strip is enabled, we don't want to emit any dead stubs.
        // Although this stub symbol is yet undefined, addSym() was called
        // during MarkLive.
        if (ctx.arg.deadStrip) {
          if (!sym->isLive())
            continue;
        }
        ctx.in.objcStubs->addEntry(sym);
      }
    }
  }

  for (const InputFile *file : ctx.inputFiles) {
    if (auto *objFile = dyn_cast<ObjFile>(file))
      for (Symbol *sym : objFile->symbols) {
        if (auto *defined = dyn_cast_or_null<Defined>(sym)) {
          if (!defined->isLive())
            continue;
          if (!defined->isExternal() && !defined->isAbsolute() &&
              isCodeSection(defined->isec()))
            ctx.in.unwindInfo->addSymbol(defined);
        }
      }
  }
}

// TODO: ld64 enforces the old load commands in a few other cases.
static bool useLCBuildVersion(const PlatformInfo &platformInfo) {
  static const std::array<std::pair<PlatformType, VersionTuple>, 7> minVersion =
      {{{PLATFORM_MACOS, VersionTuple(10, 14)},
        {PLATFORM_IOS, VersionTuple(12, 0)},
        {PLATFORM_IOSSIMULATOR, VersionTuple(13, 0)},
        {PLATFORM_TVOS, VersionTuple(12, 0)},
        {PLATFORM_TVOSSIMULATOR, VersionTuple(13, 0)},
        {PLATFORM_WATCHOS, VersionTuple(5, 0)},
        {PLATFORM_WATCHOSSIMULATOR, VersionTuple(6, 0)}}};
  auto it = llvm::find_if(minVersion, [&](const auto &p) {
    return p.first == platformInfo.target.Platform;
  });
  return it == minVersion.end()
             ? true
             : platformInfo.target.MinDeployment >= it->second;
}

template <class LP> void Writer::createLoadCommands() {
  uint32_t dylibCommandCount = 0;
  uint8_t segIndex = 0;
  for (OutputSegment *seg : ctx.outputSegments) {
    ctx.in.header->addLoadCommand(ctx.make<LCSegment<LP>>(seg->name, seg));
    seg->index = segIndex++;
  }

  if (ctx.arg.emitChainedFixups) {
    ctx.in.header->addLoadCommand(
        ctx.make<LCChainedFixups>(ctx.in.chainedFixups));
    ctx.in.header->addLoadCommand(ctx.make<LCExportsTrie>(ctx.in.exports));
  } else {
    ctx.in.header->addLoadCommand(
        ctx.make<LCDyldInfo>(ctx.in.rebase, ctx.in.binding, ctx.in.weakBinding,
                             ctx.in.lazyBinding, ctx.in.exports));
  }
  ctx.in.header->addLoadCommand(
      ctx.make<LCSymtab>(symtabSection, stringTableSection));
  ctx.in.header->addLoadCommand(
      ctx.make<LCDysymtab>(symtabSection, indirectSymtabSection));
  if (!ctx.arg.umbrella.empty())
    ctx.in.header->addLoadCommand(
        ctx.make<LCSubFramework>(ctx, ctx.arg.umbrella));
  if (ctx.arg.emitEncryptionInfo)
    ctx.in.header->addLoadCommand(ctx.make<LCEncryptionInfo<LP>>(ctx));
  for (StringRef path : ctx.arg.runtimePaths)
    ctx.in.header->addLoadCommand(ctx.make<LCRPath>(ctx, path));

  switch (ctx.arg.outputType) {
  case MH_EXECUTE:
    ctx.in.header->addLoadCommand(ctx.make<LCLoadDylinker>(ctx));
    break;
  case MH_DYLIB:
    ctx.in.header->addLoadCommand(ctx.make<LCDylib>(
        ctx, LC_ID_DYLIB, ctx.arg.installName,
        ctx.arg.dylibCompatibilityVersion, ctx.arg.dylibCurrentVersion));
    ++dylibCommandCount;
    for (StringRef client : ctx.arg.allowableClients)
      ctx.in.header->addLoadCommand(ctx.make<LCSubClient>(ctx, client));
    break;
  case MH_BUNDLE:
    break;
  default:
    llvm_unreachable("unhandled output file type");
  }

  if (ctx.arg.generateUuid) {
    uuidCommand = ctx.make<LCUuid>();
    ctx.in.header->addLoadCommand(uuidCommand);
  }

  if (useLCBuildVersion(ctx.arg.platformInfo))
    ctx.in.header->addLoadCommand(
        ctx.make<LCBuildVersion>(ctx.arg.platformInfo));
  else
    ctx.in.header->addLoadCommand(ctx.make<LCMinVersion>(ctx.arg.platformInfo));

  if (ctx.arg.secondaryPlatformInfo) {
    ctx.in.header->addLoadCommand(
        ctx.make<LCBuildVersion>(*ctx.arg.secondaryPlatformInfo));
  }

  // This is down here to match ld64's load command order.
  if (ctx.arg.outputType == MH_EXECUTE)
    ctx.in.header->addLoadCommand(ctx.make<LCMain>(ctx));

  // See ld64's OutputFile::buildDylibOrdinalMapping for the corresponding
  // library ordinal computation code in ld64.
  int64_t dylibOrdinal = 1;
  DenseMap<StringRef, int64_t> ordinalForInstallName;

  std::vector<DylibFile *> dylibFiles;
  for (InputFile *file : ctx.inputFiles) {
    if (auto *dylibFile = dyn_cast<DylibFile>(file))
      dylibFiles.push_back(dylibFile);
  }
  for (size_t i = 0; i < dylibFiles.size(); ++i)
    dylibFiles.insert(dylibFiles.end(), dylibFiles[i]->extraDylibs.begin(),
                      dylibFiles[i]->extraDylibs.end());

  for (DylibFile *dylibFile : dylibFiles) {
    if (dylibFile->isBundleLoader) {
      dylibFile->ordinal = BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE;
      // Shortcut since bundle-loader does not re-export the symbols.

      dylibFile->reexport = false;
      continue;
    }

    // Don't emit load commands for a dylib that is not referenced if:
    // - it was added implicitly (via a reexport, an LC_LOAD_DYLINKER --
    //   if it's on the linker command line, it's explicit)
    // - or it's marked MH_DEAD_STRIPPABLE_DYLIB
    // - or the flag -dead_strip_dylibs is used
    // FIXME: `isReferenced()` is currently computed before dead code
    // stripping, so references from dead code keep a dylib alive. This
    // matches ld64, but it's something we should do better.
    if (!dylibFile->isReferenced() && !dylibFile->forceNeeded &&
        (!dylibFile->isExplicitlyLinked() || dylibFile->deadStrippable ||
         ctx.arg.deadStripDylibs))
      continue;

    // Several DylibFiles can have the same installName. Only emit a single
    // load command for that installName and give all these DylibFiles the
    // same ordinal.
    // This can happen in several cases:
    // - a new framework could change its installName to an older
    //   framework name via an $ld$ symbol depending on platform_version
    // - symlinks (for example, libpthread.tbd is a symlink to libSystem.tbd;
    //   Foo.framework/Foo.tbd is usually a symlink to
    //   Foo.framework/Versions/Current/Foo.tbd, where
    //   Foo.framework/Versions/Current is usually a symlink to
    //   Foo.framework/Versions/A)
    // - a framework can be linked both explicitly on the linker
    //   command line and implicitly as a reexport from a different
    //   framework. The re-export will usually point to the tbd file
    //   in Foo.framework/Versions/A/Foo.tbd, while the explicit link will
    //   usually find Foo.framework/Foo.tbd. These are usually symlinks,
    //   but in a --reproduce archive they will be identical but distinct
    //   files.
    // In the first case, *semantically distinct* DylibFiles will have the
    // same installName.
    int64_t &ordinal = ordinalForInstallName[dylibFile->installName];
    if (ordinal) {
      dylibFile->ordinal = ordinal;
      continue;
    }

    ordinal = dylibFile->ordinal = dylibOrdinal++;
    LoadCommandType lcType = LC_LOAD_DYLIB;
    if (dylibFile->reexport) {
      if (dylibFile->forceWeakImport)
        ctx.e.warn(path::filename(dylibFile->getName()) +
                   " is re-exported so cannot be weak-linked");

      lcType = LC_REEXPORT_DYLIB;
    } else if (dylibFile->forceWeakImport ||
               dylibFile->refState == RefState::Weak) {
      lcType = LC_LOAD_WEAK_DYLIB;
    }
    ctx.in.header->addLoadCommand(ctx.make<LCDylib>(
        ctx, lcType, dylibFile->installName, dylibFile->compatibilityVersion,
        dylibFile->currentVersion));
    ++dylibCommandCount;
  }

  for (const auto &dyldEnv : ctx.arg.dyldEnvs)
    ctx.in.header->addLoadCommand(ctx.make<LCDyldEnv>(ctx, dyldEnv));

  if (functionStartsSection)
    ctx.in.header->addLoadCommand(
        ctx.make<LCFunctionStarts>(functionStartsSection));
  if (dataInCodeSection)
    ctx.in.header->addLoadCommand(ctx.make<LCDataInCode>(dataInCodeSection));
  if (codeSignatureSection)
    ctx.in.header->addLoadCommand(
        ctx.make<LCCodeSignature>(codeSignatureSection));

  const uint32_t MACOS_MAXPATHLEN = 1024;
  ctx.arg.headerPad =
      std::max(ctx.arg.headerPad, (ctx.arg.headerPadMaxInstallNames
                                       ? dylibCommandCount * MACOS_MAXPATHLEN
                                       : 0));
}

// __objc_stubs is synthetic, so the input section sorting in
// sortSegmentsAndSections() does not reach its entries. Order each stub by the
// priority of the earliest-laid-out section that calls it, which keeps the
// stubs reached during startup together.
static void orderObjCStubsByCallerPriority(
    Ctx &ctx, const DenseMap<const InputSection *, int> &priorities) {
  if (priorities.empty() || !ctx.in.objcStubs->isNeeded())
    return;

  // ICF may make the prioritized section differ from the section whose
  // relocations describe the original calls. Use the canonical section for
  // priority lookup, but scan the original section's relocations.
  DenseMap<const Symbol *, int> stubPriority;
  for (const ConcatInputSection *isec : ctx.inputSections) {
    if (!isCodeSection(isec))
      continue;
    const auto *priorityIsec = cast<ConcatInputSection>(isec->canonical());
    if (priorityIsec->shouldOmitFromOutput())
      continue;
    auto prio = priorities.find(priorityIsec);
    if (prio == priorities.end())
      continue;
    for (const Relocation &r : isec->relocs) {
      if (!ctx.target->hasAttr(r.type, RelocAttrBits::BRANCH))
        continue;
      auto *stub = dyn_cast_if_present<Symbol *>(r.referent);
      if (!stub || !ObjCStubsSection::isObjCStubSymbol(stub))
        continue;
      auto [it, inserted] = stubPriority.try_emplace(stub, prio->second);
      if (!inserted)
        it->second = std::min(it->second, prio->second);
    }
  }
  if (stubPriority.empty())
    return;

  ctx.in.objcStubs->sortSymbols(stubPriority);
}

// Sorting only can happen once all outputs have been collected. Here we sort
// segments, output sections within each segment, and input sections within each
// output segment.
static void sortSegmentsAndSections(Ctx &ctx) {
  TimeTraceScope timeScope("Sort segments and sections");
  sortOutputSegments(ctx);

  DenseMap<const InputSection *, int> isecPriorities =
      ctx.priorityBuilder->buildInputSectionPriorities();

  orderObjCStubsByCallerPriority(ctx, isecPriorities);

  uint32_t sectionIndex = 0;
  for (OutputSegment *seg : ctx.outputSegments) {
    seg->sortOutputSections();
    // References from thread-local variable sections are treated as offsets
    // relative to the start of the thread-local data memory area, which
    // is initialized via copying all the TLV data sections (which are all
    // contiguous). If later data sections require a greater alignment than
    // earlier ones, the offsets of data within those sections won't be
    // guaranteed to aligned unless we normalize alignments. We therefore use
    // the largest alignment for all TLV data sections.
    uint32_t tlvAlign = 0;
    for (const OutputSection *osec : seg->getSections())
      if (isThreadLocalData(osec->flags) && osec->align > tlvAlign)
        tlvAlign = osec->align;

    for (OutputSection *osec : seg->getSections()) {
      // Now that the output sections are sorted, assign the final
      // output section indices.
      if (!osec->isHidden())
        osec->index = ++sectionIndex;
      if (isThreadLocalData(osec->flags)) {
        if (!ctx.firstTLVDataSection)
          ctx.firstTLVDataSection = osec;
        osec->align = tlvAlign;
      }

      if (auto *merged = dyn_cast<ConcatOutputSection>(osec)) {
        auto coldIt = std::stable_partition(
            merged->inputs.begin(), merged->inputs.end(),
            [](InputSection *isec) { return !isec->isCold; });
        if (!isecPriorities.empty()) {
          std::stable_sort(merged->inputs.begin(), coldIt,
                           [&](InputSection *a, InputSection *b) {
                             return isecPriorities.lookup(a) <
                                    isecPriorities.lookup(b);
                           });
        }
      }
    }
  }
}

template <class LP> void Writer::createOutputSections() {
  TimeTraceScope timeScope("Create output sections");
  // First, create hidden sections
  stringTableSection = ctx.make<StringTableSection>(ctx);
  symtabSection = makeSymtabSection<LP>(ctx, *stringTableSection);
  indirectSymtabSection = ctx.make<IndirectSymtabSection>(ctx);
  if (ctx.arg.adhocCodesign)
    codeSignatureSection = ctx.make<CodeSignatureSection>(ctx);
  if (ctx.arg.emitDataInCodeInfo)
    dataInCodeSection = ctx.make<DataInCodeSection>(ctx);
  if (ctx.arg.emitFunctionStarts)
    functionStartsSection = ctx.make<FunctionStartsSection>(ctx);

  switch (ctx.arg.outputType) {
  case MH_EXECUTE:
    ctx.make<PageZeroSection>(ctx);
    break;
  case MH_DYLIB:
  case MH_BUNDLE:
    break;
  default:
    llvm_unreachable("unhandled output file type");
  }

  // Then add input sections to output sections.
  for (ConcatInputSection *isec : ctx.inputSections) {
    if (isec->shouldOmitFromOutput())
      continue;
    ConcatOutputSection *osec = cast<ConcatOutputSection>(isec->parent);
    osec->addInput(isec);
    osec->inputOrder =
        std::min(osec->inputOrder, static_cast<int>(isec->outSecOff));
  }

  // Once all the inputs are added, we can finalize the output section
  // properties and create the corresponding output segments.
  for (const auto &it : ctx.concatOutputSections) {
    StringRef segname = it.first.first;
    ConcatOutputSection *osec = it.second;
    assert(segname != segment_names::ld);
    if (osec->isNeeded()) {
      // See comment in ObjFile::splitEhFrames()
      if (osec->name == section_names::ehFrame &&
          segname == segment_names::text)
        osec->align = ctx.target->wordSize;

      // MC keeps the default 1-byte alignment for __thread_vars, even though it
      // contains pointers that are fixed up by dyld, which requires proper
      // alignment.
      if (isThreadLocalVariables(osec->flags))
        osec->align = std::max<uint32_t>(osec->align, ctx.target->wordSize);

      getOrCreateOutputSegment(ctx, segname)->addOutputSection(osec);
    }
  }

  for (SyntheticSection *ssec : ctx.syntheticSections) {
    auto it = ctx.concatOutputSections.find({ssec->segname, ssec->name});
    // We add all LinkEdit sections here because we don't know if they are
    // needed until their finalizeContents() methods get called later. While
    // this means that we add some redundant sections to __LINKEDIT, there is
    // is no redundancy in the output, as we do not emit section headers for
    // any LinkEdit sections.
    if (ssec->isNeeded() || ssec->segname == segment_names::linkEdit) {
      if (it == ctx.concatOutputSections.end()) {
        getOrCreateOutputSegment(ctx, ssec->segname)->addOutputSection(ssec);
      } else {
        ctx.e.fatal("section from " +
                    toString(it->second->firstSection()->getFile()) +
                    " conflicts with synthetic section " + ssec->segname + "," +
                    ssec->name);
      }
    }
  }

  // dyld requires __LINKEDIT segment to always exist (even if empty).
  linkEditSegment = getOrCreateOutputSegment(ctx, segment_names::linkEdit);
}

void Writer::finalizeAddresses() {
  TimeTraceScope timeScope("Finalize addresses");
  uint64_t pageSize = ctx.target->getPageSize();

  // We could parallelize this loop, but local benchmarking indicates it is
  // faster to do it all in the main thread.
  for (OutputSegment *seg : ctx.outputSegments) {
    if (seg == linkEditSegment)
      continue;
    for (OutputSection *osec : seg->getSections()) {
      if (!osec->isNeeded())
        continue;
      // Other kinds of OutputSections have already been finalized.
      if (auto *concatOsec = dyn_cast<ConcatOutputSection>(osec))
        concatOsec->finalizeContents();
    }
  }

  // Ensure that segments (and the sections they contain) are allocated
  // addresses in ascending order, which dyld requires.
  //
  // Note that at this point, __LINKEDIT sections are empty, but we need to
  // determine addresses of other segments/sections before generating its
  // contents.
  for (OutputSegment *seg : ctx.outputSegments) {
    if (seg == linkEditSegment)
      continue;
    seg->addr = addr;
    assignAddresses(seg);
    // codesign / libstuff checks for segment ordering by verifying that
    // `fileOff + fileSize == next segment fileOff`. So we call
    // alignToPowerOf2() before (instead of after) computing fileSize to ensure
    // that the segments are contiguous. We handle addr / vmSize similarly for
    // the same reason.
    fileOff = alignToPowerOf2(fileOff, pageSize);
    addr = alignToPowerOf2(addr, pageSize);
    seg->vmSize = addr - seg->addr;
    seg->fileSize = fileOff - seg->fileOff;
    seg->assignAddressesToStartEndSymbols();
  }
}

void Writer::finalizeLinkEditSegment() {
  TimeTraceScope timeScope("Finalize __LINKEDIT segment");
  // Fill __LINKEDIT contents.
  std::array<LinkEditSection *, 10> linkEditSections{
      ctx.in.rebase,         ctx.in.binding,        ctx.in.weakBinding,
      ctx.in.lazyBinding,    ctx.in.exports,        ctx.in.chainedFixups,
      symtabSection,         indirectSymtabSection, dataInCodeSection,
      functionStartsSection,
  };

  parallelForEach(linkEditSections.begin(), linkEditSections.end(),
                  [](LinkEditSection *osec) {
                    if (osec)
                      osec->finalizeContents();
                  });

  // Now that __LINKEDIT is filled out, do a proper calculation of its
  // addresses and offsets.
  linkEditSegment->addr = addr;
  assignAddresses(linkEditSegment);
  // No need to page-align fileOff / addr here since this is the last segment.
  linkEditSegment->vmSize = addr - linkEditSegment->addr;
  linkEditSegment->fileSize = fileOff - linkEditSegment->fileOff;
}

void Writer::assignAddresses(OutputSegment *seg) {
  seg->fileOff = fileOff;

  for (OutputSection *osec : seg->getSections()) {
    if (!osec->isNeeded())
      continue;
    addr = alignToPowerOf2(addr, osec->align);
    fileOff = alignToPowerOf2(fileOff, osec->align);
    osec->addr = addr;
    osec->fileOff = isZeroFill(osec->flags) ? 0 : fileOff;
    osec->finalize();
    osec->assignAddressesToStartEndSymbols();

    addr += osec->getSize();
    fileOff += osec->getFileSize();
  }
}

void Writer::openFile() {
  Expected<std::unique_ptr<FileOutputBuffer>> bufferOrErr =
      FileOutputBuffer::create(ctx.arg.outputFile, fileOff,
                               FileOutputBuffer::F_executable);

  if (!bufferOrErr)
    ctx.e.fatal("failed to open " + ctx.arg.outputFile + ": " +
                llvm::toString(bufferOrErr.takeError()));
  buffer = std::move(*bufferOrErr);
  ctx.in.bufferStart = buffer->getBufferStart();
}

void Writer::writeSections() {
  TimeTraceScope timeScope("Write output sections");

  uint8_t *buf = buffer->getBufferStart();
  std::vector<const OutputSection *> osecs;
  for (const OutputSegment *seg : ctx.outputSegments)
    append_range(osecs, seg->getSections());

  parallelForEach(osecs.begin(), osecs.end(), [&](const OutputSection *osec) {
    osec->writeTo(buf + osec->fileOff);
  });
}

void Writer::applyOptimizationHints() {
  if (!is_contained({AK_arm64, AK_arm64e, AK_arm64_32}, ctx.arg.arch()) ||
      ctx.arg.ignoreOptimizationHints)
    return;

  uint8_t *buf = buffer->getBufferStart();
  TimeTraceScope timeScope("Apply linker optimization hints");
  parallelForEach(ctx.inputFiles, [buf](const InputFile *file) {
    if (const auto *objFile = dyn_cast<ObjFile>(file))
      macho::applyOptimizationHints(buf, *objFile);
  });
}

// In order to utilize multiple cores, we first split the buffer into chunks,
// compute a hash for each chunk, and then compute a hash value of the hash
// values.
void Writer::writeUuid() {
  TimeTraceScope timeScope("Computing UUID");

  ArrayRef<uint8_t> data{buffer->getBufferStart(), buffer->getBufferEnd()};
  std::vector<ArrayRef<uint8_t>> chunks = split(data, 1024 * 1024);

  // Leave one slot for filename
  std::vector<uint64_t> hashes(chunks.size() + 1);
  parallelFor(0, chunks.size(),
              [&](size_t i) { hashes[i] = xxh3_64bits(chunks[i]); });
  // Append the output filename so that identical binaries with different names
  // don't get the same UUID.
  hashes[chunks.size()] = xxh3_64bits(sys::path::filename(ctx.arg.finalOutput));

  uint64_t digest = xxh3_64bits({reinterpret_cast<uint8_t *>(hashes.data()),
                                 hashes.size() * sizeof(uint64_t)});
  uuidCommand->writeUuid(digest);
}

// This is step 5 of the algorithm described in the class comment of
// ChainedFixupsSection.
void Writer::buildFixupChains() {
  if (!ctx.arg.emitChainedFixups)
    return;

  const std::vector<Location> &loc = ctx.in.chainedFixups->getLocations();
  if (loc.empty())
    return;

  TimeTraceScope timeScope("Build fixup chains");

  const uint64_t pageSize = ctx.target->getPageSize();
  constexpr uint32_t stride = 4; // for DYLD_CHAINED_PTR_64

  for (size_t i = 0, count = loc.size(); i < count;) {
    const OutputSegment *oseg = loc[i].isec->parent->parent;
    uint8_t *buf = buffer->getBufferStart() + oseg->fileOff;
    uint64_t pageIdx = loc[i].offset / pageSize;
    ++i;

    while (i < count && loc[i].isec->parent->parent == oseg &&
           (loc[i].offset / pageSize) == pageIdx) {
      uint64_t offset = loc[i].offset - loc[i - 1].offset;

      auto fail = [&](Twine message) {
        ctx.e.error(
            loc[i].isec->getSegName() + "," + loc[i].isec->getName() +
            ", offset " +
            Twine(loc[i].offset - loc[i].isec->parent->getSegmentOffset()) +
            ": " + message);
      };

      if (offset < ctx.target->wordSize)
        return fail("fixups overlap");
      if (offset % stride != 0)
        return fail(
            "fixups are unaligned (offset " + Twine(offset) +
            " is not a multiple of the stride). Re-link with -no_fixup_chains");

      // The "next" field is in the same location for bind and rebase entries.
      reinterpret_cast<dyld_chained_ptr_64_bind *>(buf + loc[i - 1].offset)
          ->next = offset / stride;
      ++i;
    }
  }
}

void Writer::writeCodeSignature() {
  if (codeSignatureSection) {
    TimeTraceScope timeScope("Write code signature");
    codeSignatureSection->writeHashes(buffer->getBufferStart());
  }
}

void Writer::writeOutputFile() {
  TimeTraceScope timeScope("Write output file");
  openFile();
  reportPendingUndefinedSymbols(ctx);
  if (ctx.e.errorCount)
    return;
  writeSections();
  applyOptimizationHints();
  buildFixupChains();
  if (ctx.arg.generateUuid)
    writeUuid();
  writeCodeSignature();

  if (auto e = buffer->commit())
    ctx.e.fatal("failed to write output '" + buffer->getPath() +
                "': " + toString(std::move(e)));
}

template <class LP> void Writer::run() {
  treatSpecialUndefineds();
  if (ctx.arg.entry && needsBinding(ctx.arg.entry))
    ctx.in.stubs->addEntry(ctx.arg.entry);

  // Canonicalization of all pointers to InputSections should be handled by
  // these two scan* methods. I.e. from this point onward, for all live
  // InputSections, we should have `isec->canonical() == isec`.
  scanSymbols();
  if (ctx.in.objcStubs->isNeeded())
    ctx.in.objcStubs->setUp();
  if (ctx.in.objcMethList->isNeeded())
    ctx.in.objcMethList->setUp();
  scanRelocations();
  if (ctx.in.initOffsets->isNeeded())
    ctx.in.initOffsets->setUp();

  // Do not proceed if there were undefined or duplicate symbols.
  reportPendingUndefinedSymbols(ctx);
  reportPendingDuplicateSymbols(ctx);
  if (ctx.e.errorCount)
    return;

  if (ctx.in.stubHelper && ctx.in.stubHelper->isNeeded())
    ctx.in.stubHelper->setUp();

  if (ctx.in.objCImageInfo->isNeeded())
    ctx.in.objCImageInfo->finalizeContents();

  // At this point, we should know exactly which output sections are needed,
  // courtesy of scanSymbols() and scanRelocations().
  createOutputSections<LP>();

  // After this point, we create no new segments; HOWEVER, we might
  // yet create branch-range extension thunks for architectures whose
  // hardware call instructions have limited range, e.g., ARM(64).
  // The thunks are created as InputSections interspersed among
  // the ordinary __TEXT,_text InputSections.
  sortSegmentsAndSections(ctx);
  createLoadCommands<LP>();
  finalizeAddresses();

  llvm::thread mapFileWriter([&] {
    if (LLVM_ENABLE_THREADS && ctx.arg.timeTraceEnabled)
      timeTraceProfilerInitialize(ctx.arg.timeTraceGranularity, "writeMapFile");
    writeMapFile(ctx);
    if (LLVM_ENABLE_THREADS && ctx.arg.timeTraceEnabled)
      timeTraceProfilerFinishThread();
  });

  finalizeLinkEditSegment();
  writeOutputFile();
  mapFileWriter.join();
}

template <class LP> void macho::writeResult(Ctx &ctx) { Writer(ctx).run<LP>(); }

void macho::createSyntheticSections(Ctx &ctx) {
  ctx.in.header = ctx.make<MachHeaderSection>(ctx);
  // Materialize cstring and objcMethname sections
  ctx.in.cStringSection =
      getOrCreateCStringSection(ctx, section_names::cString);
  ctx.in.objcMethnameSection = cast<DeduplicatedCStringSection>(
      getOrCreateCStringSection(ctx, section_names::objcMethname,
                                /*forceDedupStrings=*/true));
  ctx.in.wordLiteralSection = ctx.make<WordLiteralSection>(ctx);
  if (ctx.arg.emitChainedFixups) {
    ctx.in.chainedFixups = ctx.make<ChainedFixupsSection>(ctx);
  } else {
    ctx.in.rebase = ctx.make<RebaseSection>(ctx);
    ctx.in.binding = ctx.make<BindingSection>(ctx);
    ctx.in.weakBinding = ctx.make<WeakBindingSection>(ctx);
    ctx.in.lazyBinding = ctx.make<LazyBindingSection>(ctx);
    ctx.in.lazyPointers = ctx.make<LazyPointerSection>(ctx);
    ctx.in.stubHelper = ctx.make<StubHelperSection>(ctx);
  }
  ctx.in.exports = ctx.make<ExportSection>(ctx);
  ctx.in.got = ctx.make<GotSection>(ctx);
  ctx.in.stubs = ctx.make<StubsSection>(ctx);
  ctx.in.objcStubs = ctx.make<ObjCStubsSection>(ctx);
  ctx.in.unwindInfo = makeUnwindInfoSection(ctx);
  ctx.in.objCImageInfo = ctx.make<ObjCImageInfoSection>(ctx);
  ctx.in.initOffsets = ctx.make<InitOffsetsSection>(ctx);
  ctx.in.objcMethList = ctx.make<ObjCMethListSection>(ctx);

  // This section contains space for just a single word, and will be used by
  // dyld to cache an address to the image loader it uses.
  uint8_t *arr = ctx.bAlloc.Allocate<uint8_t>(ctx.target->wordSize);
  memset(arr, 0, ctx.target->wordSize);
  ctx.in.imageLoaderCache = makeSyntheticInputSection(
      ctx, segment_names::data, section_names::data, S_REGULAR,
      ArrayRef<uint8_t>{arr, ctx.target->wordSize},
      /*align=*/ctx.target->wordSize);
  assert(ctx.in.imageLoaderCache->live);
}

template void macho::writeResult<LP64>(Ctx &);
template void macho::writeResult<ILP32>(Ctx &);
