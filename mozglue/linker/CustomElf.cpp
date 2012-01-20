/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <cstring>
#include <sys/mman.h>
#include <vector>
#include <dlfcn.h>
#include "CustomElf.h"
#include "Mappable.h"
#include "Logging.h"

using namespace Elf;
using namespace mozilla;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef PAGE_MASK
#define PAGE_MASK (~ (PAGE_SIZE - 1))
#endif

/* TODO: Fill ElfLoader::Singleton.lastError on errors. */

/* Function used to report library mappings from the custom linker to Gecko
 * crash reporter */
#ifdef ANDROID
extern "C" {
  void report_mapping(char *name, void *base, uint32_t len, uint32_t offset);
}
#else
#define report_mapping(...)
#endif

const Ehdr *Ehdr::validate(const void *buf)
{
  if (!buf || buf == MAP_FAILED)
    return NULL;

  const Ehdr *ehdr = reinterpret_cast<const Ehdr *>(buf);

  /* Only support ELF executables or libraries for the host system */
  if (memcmp(ELFMAG, &ehdr->e_ident, SELFMAG) ||
      ehdr->e_ident[EI_CLASS] != ELFCLASS ||
      ehdr->e_ident[EI_DATA] != ELFDATA ||
      ehdr->e_ident[EI_VERSION] != 1 ||
      (ehdr->e_ident[EI_OSABI] != ELFOSABI && ehdr->e_ident[EI_OSABI] != ELFOSABI_NONE) ||
#ifdef EI_ABIVERSION
      ehdr->e_ident[EI_ABIVERSION] != ELFABIVERSION ||
#endif
      (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
      ehdr->e_machine != ELFMACHINE ||
      ehdr->e_version != 1 ||
      ehdr->e_phentsize != sizeof(Phdr))
    return NULL;

  return ehdr;
}

namespace {

void debug_phdr(const char *type, const Phdr *phdr)
{
  debug("%s @0x%08" PRIxAddr " ("
        "filesz: 0x%08" PRIxAddr ", "
        "memsz: 0x%08" PRIxAddr ", "
        "offset: 0x%08" PRIxAddr ", "
        "flags: %c%c%c)",
        type, phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz,
        phdr->p_offset, phdr->p_flags & PF_R ? 'r' : '-',
        phdr->p_flags & PF_W ? 'w' : '-', phdr->p_flags & PF_X ? 'x' : '-');
}

} /* anonymous namespace */

/**
 * RAII wrapper for a mapping of the first page off a Mappable object.
 * This calls Mappable::munmap instead of system munmap.
 */
class Mappable1stPagePtr: public GenericMappedPtr<Mappable1stPagePtr> {
public:
  Mappable1stPagePtr(Mappable *mappable)
  : GenericMappedPtr<Mappable1stPagePtr>(
      mappable->mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, 0), PAGE_SIZE)
  , mappable(mappable)
  { }

  void munmap(void *buf, size_t length) {
    mappable->munmap(buf, length);
  }
private:
  Mappable *mappable;
};


TemporaryRef<LibHandle>
CustomElf::Load(Mappable *mappable, const char *path, int flags)
{
  debug("CustomElf::Load(\"%s\", %x) = ...", path, flags);
  if (!mappable)
    return NULL;
  /* Keeping a RefPtr of the CustomElf is going to free the appropriate
   * resources when returning NULL */
  RefPtr<CustomElf> elf = new CustomElf(mappable, path);
  /* Map the first page of the Elf object to access Elf and program headers */
  Mappable1stPagePtr ehdr_raw(mappable);
  if (ehdr_raw == MAP_FAILED)
    return NULL;

  const Ehdr *ehdr = Ehdr::validate(ehdr_raw);
  if (!ehdr)
    return NULL;

  /* Scan Elf Program Headers and gather some information about them */
  std::vector<const Phdr *> pt_loads;
  Addr min_vaddr = (Addr) -1; // We want to find the lowest and biggest
  Addr max_vaddr = 0;         // virtual address used by this Elf.
  const Phdr *dyn = NULL;

  const Phdr *first_phdr = reinterpret_cast<const Phdr *>(
                           reinterpret_cast<const char *>(ehdr) + ehdr->e_phoff);
  const Phdr *end_phdr = &first_phdr[ehdr->e_phnum];

  for (const Phdr *phdr = first_phdr; phdr < end_phdr; phdr++) {
    switch (phdr->p_type) {
      case PT_LOAD:
        debug_phdr("PT_LOAD", phdr);
        pt_loads.push_back(phdr);
        if (phdr->p_vaddr < min_vaddr)
          min_vaddr = phdr->p_vaddr;
        if (max_vaddr < phdr->p_vaddr + phdr->p_memsz)
          max_vaddr = phdr->p_vaddr + phdr->p_memsz;
        break;
      case PT_DYNAMIC:
        debug_phdr("PT_DYNAMIC", phdr);
        if (!dyn) {
          dyn = phdr;
        } else {
          log("%s: Multiple PT_DYNAMIC segments detected", elf->GetPath());
          return NULL;
        }
        break;
      case PT_TLS:
        debug_phdr("PT_TLS", phdr);
        if (phdr->p_memsz) {
          log("%s: TLS is not supported", elf->GetPath());
          return NULL;
        }
        break;
      case PT_GNU_STACK:
        debug_phdr("PT_GNU_STACK", phdr);
// Skip on Android until bug 706116 is fixed
#ifndef ANDROID
        if (phdr->p_flags & PF_X) {
          log("%s: Executable stack is not supported", elf->GetPath());
          return NULL;
        }
#endif
        break;
      default:
        debug("%s: Warning: program header type #%d not handled",
              elf->GetPath(), phdr->p_type);
    }
  }

  if (min_vaddr != 0) {
    log("%s: Unsupported minimal virtual address: 0x%08" PRIxAddr,
        elf->GetPath(), min_vaddr);
    return NULL;
  }
  if (!dyn) {
    log("%s: No PT_DYNAMIC segment found", elf->GetPath());
    return NULL;
  }

  /* Reserve enough memory to map the complete virtual address space for this
   * library. */
  elf->base.Init(mmap(NULL, max_vaddr, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0), max_vaddr);
  if (elf->base == MAP_FAILED) {
    log("%s: Failed to mmap", elf->GetPath());
    return NULL;
  }

  /* Load and initialize library */
  for (std::vector<const Phdr *>::iterator it = pt_loads.begin();
       it < pt_loads.end(); ++it)
    if (!elf->LoadSegment(*it))
      return NULL;

  /* We're not going to mmap anymore */
  mappable->finalize();

  report_mapping(const_cast<char *>(elf->GetName()), elf->base,
                 (max_vaddr + PAGE_SIZE - 1) & PAGE_MASK, 0);

  if (!elf->InitDyn(dyn))
    return NULL;

  debug("CustomElf::Load(\"%s\", %x) = %p", path, flags,
        static_cast<void *>(elf));
  return elf;
}

CustomElf::~CustomElf()
{
  debug("CustomElf::~CustomElf(%p [\"%s\"])",
        reinterpret_cast<void *>(this), GetPath());
  CallFini();
  /* Normally, __cxa_finalize is called by the .fini function. However,
   * Android NDK before r6b doesn't do that. Our wrapped cxa_finalize only
   * calls destructors once, so call it in all cases. */
  ElfLoader::__wrap_cxa_finalize(this);
  delete mappable;
}

namespace {

/**
 * Hash function for symbol lookup, as defined in ELF standard for System V
 */
unsigned long
ElfHash(const char *symbol)
{
  const unsigned char *sym = reinterpret_cast<const unsigned char *>(symbol);
  unsigned long h = 0, g;
  while (*sym) {
    h = (h << 4) + *sym++;
    if ((g = h & 0xf0000000))
      h ^= g >> 24;
    h &= ~g;
  }
  return h;
}

} /* anonymous namespace */

void *
CustomElf::GetSymbolPtr(const char *symbol) const
{
  return GetSymbolPtr(symbol, ElfHash(symbol));
}

void *
CustomElf::GetSymbolPtr(const char *symbol, unsigned long hash) const
{
  const Sym *sym = GetSymbol(symbol, hash);
  void *ptr = NULL;
  if (sym && sym->st_shndx != SHN_UNDEF)
    ptr = GetPtr(sym->st_value);
  debug("CustomElf::GetSymbolPtr(%p [\"%s\"], \"%s\") = %p",
        reinterpret_cast<const void *>(this), GetPath(), symbol, ptr);
  return ptr;
}

void *
CustomElf::GetSymbolPtrInDeps(const char *symbol) const
{
  /* Resolve dlopen and related functions to point to ours */
  if (symbol[0] == 'd' && symbol[1] == 'l') {
    if (strcmp(symbol + 2, "open") == 0)
      return FunctionPtr(__wrap_dlopen);
    if (strcmp(symbol + 2, "error") == 0)
      return FunctionPtr(__wrap_dlerror);
    if (strcmp(symbol + 2, "close") == 0)
      return FunctionPtr(__wrap_dlclose);
    if (strcmp(symbol + 2, "sym") == 0)
      return FunctionPtr(__wrap_dlsym);
    if (strcmp(symbol + 2, "addr") == 0)
      return FunctionPtr(__wrap_dladdr);
  } else if (symbol[0] == '_' && symbol[1] == '_') {
  /* Resolve a few C++ ABI specific functions to point to ours */
#ifdef __ARM_EABI__
    if (strcmp(symbol + 2, "aeabi_atexit") == 0)
      return FunctionPtr(&ElfLoader::__wrap_aeabi_atexit);
#else
    if (strcmp(symbol + 2, "cxa_atexit") == 0)
      return FunctionPtr(&ElfLoader::__wrap_cxa_atexit);
#endif
    if (strcmp(symbol + 2, "cxa_finalize") == 0)
      return FunctionPtr(&ElfLoader::__wrap_cxa_finalize);
    if (strcmp(symbol + 2, "dso_handle") == 0)
      return const_cast<CustomElf *>(this);
  }

  void *sym;
  /* Search the symbol in the main program. Note this also tries all libraries
   * the system linker will have loaded RTLD_GLOBAL. Unfortunately, that doesn't
   * work with bionic, but its linker doesn't normally search the main binary
   * anyways. Moreover, on android, the main binary is dalvik. */
#ifdef __GLIBC__
  sym = dlsym(RTLD_DEFAULT, symbol);
  debug("dlsym(RTLD_DEFAULT, \"%s\") = %p", symbol, sym);
  if (sym)
    return sym;
#endif

  /* Then search the symbol in our dependencies. Since we already searched in
   * libraries the system linker loaded, skip those (on glibc systems). We
   * also assume the symbol is to be found in one of the dependent libraries
   * directly, not in their own dependent libraries. Building libraries with
   * --no-allow-shlib-undefined ensures such indirect symbol dependency don't
   * happen. */
  unsigned long hash = ElfHash(symbol);
  for (std::vector<RefPtr<LibHandle> >::const_iterator it = dependencies.begin();
       it < dependencies.end(); ++it) {
    if (!(*it)->IsSystemElf()) {
      sym = reinterpret_cast<CustomElf *>((*it).get())->GetSymbolPtr(symbol, hash);
#ifndef __GLIBC__
    } else {
      sym = (*it)->GetSymbolPtr(symbol);
#endif
    }
    if (sym)
      return sym;
  }
  return NULL;
}

const Sym *
CustomElf::GetSymbol(const char *symbol, unsigned long hash) const
{
  /* Search symbol with the buckets and chains tables.
   * The hash computed from the symbol name gives an index in the buckets
   * table. The corresponding value in the bucket table is an index in the
   * symbols table and in the chains table.
   * If the corresponding symbol in the symbols table matches, we're done.
   * Otherwise, the corresponding value in the chains table is a new index
   * in both tables, which corresponding symbol is tested and so on and so
   * forth */
  size_t bucket = hash % buckets.numElements();
  for (size_t y = buckets[bucket]; y != STN_UNDEF; y = chains[y]) {
    if (strcmp(symbol, strtab.GetStringAt(symtab[y].st_name)))
      continue;
    return &symtab[y];
  }
  return NULL;
}

bool
CustomElf::Contains(void *addr) const
{
  return base.Contains(addr);
}

bool
CustomElf::LoadSegment(const Phdr *pt_load) const
{
  if (pt_load->p_type != PT_LOAD) {
    debug("%s: Elf::LoadSegment only takes PT_LOAD program headers", GetPath());
    return false;;
  }

  int prot = ((pt_load->p_flags & PF_X) ? PROT_EXEC : 0) |
             ((pt_load->p_flags & PF_W) ? PROT_WRITE : 0) |
             ((pt_load->p_flags & PF_R) ? PROT_READ : 0);

  /* Mmap at page boundary */
  Addr page_offset = pt_load->p_vaddr & ~PAGE_MASK;
  void *where = GetPtr(pt_load->p_vaddr - page_offset);
  debug("%s: Loading segment @%p %c%c%c", GetPath(), where,
                                          prot & PROT_READ ? 'r' : '-',
                                          prot & PROT_WRITE ? 'w' : '-',
                                          prot & PROT_EXEC ? 'x' : '-');
  void *mapped = mappable->mmap(where, pt_load->p_filesz + page_offset,
                                prot, MAP_PRIVATE | MAP_FIXED,
                                pt_load->p_offset - page_offset);
  if (mapped != where) {
    if (mapped == MAP_FAILED) {
      log("%s: Failed to mmap", GetPath());
    } else {
      log("%s: Didn't map at the expected location (wanted: %p, got: %p)",
          GetPath(), where, mapped);
    }
    return false;
  }

  /* When p_memsz is greater than p_filesz, we need to have nulled out memory
   * after p_filesz and before p_memsz.
   * Mappable::mmap already guarantees that after p_filesz and up to the end
   * of the page p_filesz is in, memory is nulled out.
   * Above the end of that page, and up to p_memsz, we already have nulled out
   * memory because we mapped anonymous memory on the whole library virtual
   * address space. We just need to adjust this anonymous memory protection
   * flags. */
  if (pt_load->p_memsz > pt_load->p_filesz) {
    Addr file_end = pt_load->p_vaddr + pt_load->p_filesz;
    Addr mem_end = pt_load->p_vaddr + pt_load->p_memsz;
    Addr next_page = (file_end & ~(PAGE_SIZE - 1)) + PAGE_SIZE;
    if (mem_end > next_page) {
      if (mprotect(GetPtr(next_page), mem_end - next_page, prot) < 0) {
        log("%s: Failed to mprotect", GetPath());
        return false;
      }
    }
  }
  return true;
}

namespace {

void debug_dyn(const char *type, const Dyn *dyn)
{
  debug("%s 0x%08" PRIxAddr, type, dyn->d_un.d_val);
}

} /* anonymous namespace */

bool
CustomElf::InitDyn(const Phdr *pt_dyn)
{
  /* Scan PT_DYNAMIC segment and gather some information */
  const Dyn *first_dyn = GetPtr<Dyn>(pt_dyn->p_vaddr);
  const Dyn *end_dyn = GetPtr<Dyn>(pt_dyn->p_vaddr + pt_dyn->p_filesz);
  std::vector<Word> dt_needed;
  size_t symnum = 0;
  for (const Dyn *dyn = first_dyn; dyn < end_dyn && dyn->d_tag; dyn++) {
    switch (dyn->d_tag) {
      case DT_NEEDED:
        debug_dyn("DT_NEEDED", dyn);
        dt_needed.push_back(dyn->d_un.d_val);
        break;
      case DT_HASH:
        {
          debug_dyn("DT_HASH", dyn);
          const Word *hash_table_header = GetPtr<Word>(dyn->d_un.d_ptr);
          symnum = hash_table_header[1];
          buckets.Init(&hash_table_header[2], hash_table_header[0]);
          chains.Init(&*buckets.end());
        }
        break;
      case DT_STRTAB:
        debug_dyn("DT_STRTAB", dyn);
        strtab.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case DT_SYMTAB:
        debug_dyn("DT_SYMTAB", dyn);
        symtab.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case DT_SYMENT:
        debug_dyn("DT_SYMENT", dyn);
        if (dyn->d_un.d_val != sizeof(Sym)) {
          log("%s: Unsupported DT_SYMENT", GetPath());
          return false;
        }
        break;
      case DT_TEXTREL:
        log("%s: Text relocations are not supported", GetPath());
        return false;
      case DT_STRSZ: /* Ignored */
        debug_dyn("DT_STRSZ", dyn);
        break;
      case UNSUPPORTED_RELOC():
      case UNSUPPORTED_RELOC(SZ):
      case UNSUPPORTED_RELOC(ENT):
        log("%s: Unsupported relocations", GetPath());
        return false;
      case RELOC():
        debug_dyn(STR_RELOC(), dyn);
        relocations.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case RELOC(SZ):
        debug_dyn(STR_RELOC(SZ), dyn);
        relocations.InitSize(dyn->d_un.d_val);
        break;
      case RELOC(ENT):
        debug_dyn(STR_RELOC(ENT), dyn);
        if (dyn->d_un.d_val != sizeof(Reloc)) {
          log("%s: Unsupported DT_RELENT", GetPath());
          return false;
        }
        break;
      case DT_JMPREL:
        debug_dyn("DT_JMPREL", dyn);
        jumprels.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case DT_PLTRELSZ:
        debug_dyn("DT_PLTRELSZ", dyn);
        jumprels.InitSize(dyn->d_un.d_val);
        break;
      case DT_PLTGOT:
        debug_dyn("DT_PLTGOT", dyn);
        break;
      case DT_INIT:
        debug_dyn("DT_INIT", dyn);
        init = dyn->d_un.d_ptr;
        break;
      case DT_INIT_ARRAY:
        debug_dyn("DT_INIT_ARRAY", dyn);
        init_array.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case DT_INIT_ARRAYSZ:
        debug_dyn("DT_INIT_ARRAYSZ", dyn);
        init_array.InitSize(dyn->d_un.d_val);
        break;
      case DT_FINI:
        debug_dyn("DT_FINI", dyn);
        fini = dyn->d_un.d_ptr;
        break;
      case DT_FINI_ARRAY:
        debug_dyn("DT_FINI_ARRAY", dyn);
        fini_array.Init(GetPtr(dyn->d_un.d_ptr));
        break;
      case DT_FINI_ARRAYSZ:
        debug_dyn("DT_FINI_ARRAYSZ", dyn);
        fini_array.InitSize(dyn->d_un.d_val);
        break;
      default:
        log("%s: Warning: dynamic header type #%" PRIxAddr" not handled",
            GetPath(), dyn->d_tag);
    }
  }

  if (!buckets || !symnum) {
    log("%s: Missing or broken DT_HASH", GetPath());
    return false;
  }
  if (!strtab) {
    log("%s: Missing DT_STRTAB", GetPath());
    return false;
  }
  if (!symtab) {
    log("%s: Missing DT_SYMTAB", GetPath());
    return false;
  }

  /* Load dependent libraries */
  for (size_t i = 0; i < dt_needed.size(); i++) {
    const char *name = strtab.GetStringAt(dt_needed[i]);
    RefPtr<LibHandle> handle =
      ElfLoader::Singleton.Load(name, RTLD_GLOBAL | RTLD_LAZY, this);
    if (!handle)
      return false;
    dependencies.push_back(handle);
  }

  /* Finish initialization */
  return Relocate() && RelocateJumps() && CallInit();
}

bool
CustomElf::Relocate()
{
  debug("Relocate %s @%p", GetPath(), static_cast<void *>(base));
  for (Array<Reloc>::iterator rel = relocations.begin();
       rel < relocations.end(); ++rel) {
    /* Location of the relocation */
    void *ptr = GetPtr(rel->r_offset);

    /* R_*_RELATIVE relocations apply directly at the given location */
    if (ELF_R_TYPE(rel->r_info) == R_RELATIVE) {
      *(void **) ptr = GetPtr(rel->GetAddend(base));
      continue;
    }
    /* Other relocation types need a symbol resolution */
    const Sym sym = symtab[ELF_R_SYM(rel->r_info)];
    void *symptr;
    if (sym.st_shndx != SHN_UNDEF) {
      symptr = GetPtr(sym.st_value);
    } else {
      /* TODO: avoid symbol resolution when it's the same symbol as last
       * iteration */
      /* TODO: handle symbol resolving to NULL vs. being undefined. */
      symptr = GetSymbolPtrInDeps(strtab.GetStringAt(sym.st_name));
    }

    if (symptr == NULL)
      log("%s: Warning: relocation to NULL @0x%08" PRIxAddr,
          GetPath(), rel->r_offset);

    /* Apply relocation */
    switch (ELF_R_TYPE(rel->r_info)) {
    case R_GLOB_DAT:
      /* R_*_GLOB_DAT relocations simply use the symbol value */
      *(void **) ptr = symptr;
      break;
    case R_ABS:
      /* R_*_ABS* relocations add the relocation added to the symbol value */
      *(const char **) ptr = (const char *)symptr + rel->GetAddend(base);
      break;
    default:
      log("%s: Unsupported relocation type: 0x%" PRIxAddr,
          GetPath(), ELF_R_TYPE(rel->r_info));
      return false;
    }
  }
  return true;
}

bool
CustomElf::RelocateJumps()
{
  /* TODO: Dynamic symbol resolution */
  for (Array<Reloc>::iterator rel = jumprels.begin();
       rel < jumprels.end(); ++rel) {
    /* Location of the relocation */
    void *ptr = GetPtr(rel->r_offset);

    /* Only R_*_JMP_SLOT relocations are expected */
    if (ELF_R_TYPE(rel->r_info) != R_JMP_SLOT) {
      log("%s: Jump relocation type mismatch", GetPath());
      return false;
    }

    /* TODO: Avoid code duplication with the relocations above */
    const Sym sym = symtab[ELF_R_SYM(rel->r_info)];
    void *symptr;
    if (sym.st_shndx != SHN_UNDEF)
      symptr = GetPtr(sym.st_value);
    else
      symptr = GetSymbolPtrInDeps(strtab.GetStringAt(sym.st_name));

    if (symptr == NULL) {
      log("%s: Error: relocation to NULL @0x%08" PRIxAddr, GetPath(), rel->r_offset);
      return false;
    }
    /* Apply relocation */
    *(void **) ptr = symptr;
  }
  return true;
}

bool
CustomElf::CallInit()
{
  if (init)
    CallFunction(init);

  for (Array<void *>::iterator it = init_array.begin();
       it < init_array.end(); ++it) {
    if (*it)
      CallFunction(*it);
  }
  initialized = true;
  return true;
}

void
CustomElf::CallFini()
{
  if (!initialized)
    return;
  for (Array<void *>::iterator it = fini_array.begin();
       it < fini_array.end(); ++it) {
    if (*it)
      CallFunction(*it);
  }
  if (fini)
    CallFunction(fini);
}
