/* elf.c -- Get debug data from an ELF file for backtraces.
   Copyright (C) 2012-2014 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Google.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    (1) Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer. 

    (2) Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.  
    
    (3) The name of the author may not be used to
    endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.  */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>

#ifdef HAVE_DL_ITERATE_PHDR
#include <link.h>
#endif

#include "backtrace.h"
#include "internal.h"

#ifndef HAVE_DL_ITERATE_PHDR

/* Dummy version of dl_iterate_phdr for systems that don't have it.  */

#define dl_phdr_info x_dl_phdr_info
#define dl_iterate_phdr x_dl_iterate_phdr

struct dl_phdr_info
{
  uintptr_t dlpi_addr;
  const char *dlpi_name;
};

static int
dl_iterate_phdr (int (*callback) (struct dl_phdr_info *,
				  size_t, void *) ATTRIBUTE_UNUSED,
		 void *data ATTRIBUTE_UNUSED)
{
  return 0;
}

#endif /* ! defined (HAVE_DL_ITERATE_PHDR) */

/* The configure script must tell us whether we are 32-bit or 64-bit
   ELF.  We could make this code test and support either possibility,
   but there is no point.  This code only works for the currently
   running executable, which means that we know the ELF mode at
   configure mode.  */

#if BACKTRACE_ELF_SIZE != 32 && BACKTRACE_ELF_SIZE != 64
#error "Unknown BACKTRACE_ELF_SIZE"
#endif

/* <link.h> might #include <elf.h> which might define our constants
   with slightly different values.  Undefine them to be safe.  */

#undef EI_NIDENT
#undef EI_MAG0
#undef EI_MAG1
#undef EI_MAG2
#undef EI_MAG3
#undef EI_CLASS
#undef EI_DATA
#undef EI_VERSION
#undef ELF_MAG0
#undef ELF_MAG1
#undef ELF_MAG2
#undef ELF_MAG3
#undef ELFCLASS32
#undef ELFCLASS64
#undef ELFDATA2LSB
#undef ELFDATA2MSB
#undef EV_CURRENT
#undef ET_DYN
#undef SHN_LORESERVE
#undef SHN_XINDEX
#undef SHN_UNDEF
#undef SHT_SYMTAB
#undef SHT_STRTAB
#undef SHT_DYNSYM
#undef STT_OBJECT
#undef STT_FUNC

/* Basic types.  */

typedef uint16_t b_elf_half;    /* Elf_Half.  */
typedef uint32_t b_elf_word;    /* Elf_Word.  */
typedef int32_t  b_elf_sword;   /* Elf_Sword.  */

#if BACKTRACE_ELF_SIZE == 32

typedef uint32_t b_elf_addr;    /* Elf_Addr.  */
typedef uint32_t b_elf_off;     /* Elf_Off.  */

typedef uint32_t b_elf_wxword;  /* 32-bit Elf_Word, 64-bit ELF_Xword.  */

#else

typedef uint64_t b_elf_addr;    /* Elf_Addr.  */
typedef uint64_t b_elf_off;     /* Elf_Off.  */
typedef uint64_t b_elf_xword;   /* Elf_Xword.  */

typedef uint64_t b_elf_wxword;  /* 32-bit Elf_Word, 64-bit ELF_Xword.  */

#endif

/* Data structures and associated constants.  */

#define EI_NIDENT 16

typedef struct {
  unsigned char	e_ident[EI_NIDENT];	/* ELF "magic number" */
  b_elf_half	e_type;			/* Identifies object file type */
  b_elf_half	e_machine;		/* Specifies required architecture */
  b_elf_word	e_version;		/* Identifies object file version */
  b_elf_addr	e_entry;		/* Entry point virtual address */
  b_elf_off	e_phoff;		/* Program header table file offset */
  b_elf_off	e_shoff;		/* Section header table file offset */
  b_elf_word	e_flags;		/* Processor-specific flags */
  b_elf_half	e_ehsize;		/* ELF header size in bytes */
  b_elf_half	e_phentsize;		/* Program header table entry size */
  b_elf_half	e_phnum;		/* Program header table entry count */
  b_elf_half	e_shentsize;		/* Section header table entry size */
  b_elf_half	e_shnum;		/* Section header table entry count */
  b_elf_half	e_shstrndx;		/* Section header string table index */
} b_elf_ehdr;  /* Elf_Ehdr.  */

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1
#define ELFCLASS64 2

#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define EV_CURRENT 1

#define ET_DYN 3

typedef struct {
  b_elf_word	sh_name;		/* Section name, index in string tbl */
  b_elf_word	sh_type;		/* Type of section */
  b_elf_wxword	sh_flags;		/* Miscellaneous section attributes */
  b_elf_addr	sh_addr;		/* Section virtual addr at execution */
  b_elf_off	sh_offset;		/* Section file offset */
  b_elf_wxword	sh_size;		/* Size of section in bytes */
  b_elf_word	sh_link;		/* Index of another section */
  b_elf_word	sh_info;		/* Additional section information */
  b_elf_wxword	sh_addralign;		/* Section alignment */
  b_elf_wxword	sh_entsize;		/* Entry size if section holds table */
} b_elf_shdr;  /* Elf_Shdr.  */

#define SHN_UNDEF	0x0000		/* Undefined section */
#define SHN_LORESERVE	0xFF00		/* Begin range of reserved indices */
#define SHN_XINDEX	0xFFFF		/* Section index is held elsewhere */

#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_DYNSYM 11

#if BACKTRACE_ELF_SIZE == 32

typedef struct
{
  b_elf_word	st_name;		/* Symbol name, index in string tbl */
  b_elf_addr	st_value;		/* Symbol value */
  b_elf_word	st_size;		/* Symbol size */
  unsigned char	st_info;		/* Symbol binding and type */
  unsigned char	st_other;		/* Visibility and other data */
  b_elf_half	st_shndx;		/* Symbol section index */
} b_elf_sym;  /* Elf_Sym.  */

#else /* BACKTRACE_ELF_SIZE != 32 */

typedef struct
{
  b_elf_word	st_name;		/* Symbol name, index in string tbl */
  unsigned char	st_info;		/* Symbol binding and type */
  unsigned char	st_other;		/* Visibility and other data */
  b_elf_half	st_shndx;		/* Symbol section index */
  b_elf_addr	st_value;		/* Symbol value */
  b_elf_xword	st_size;		/* Symbol size */
} b_elf_sym;  /* Elf_Sym.  */

#endif /* BACKTRACE_ELF_SIZE != 32 */

#define STT_OBJECT 1
#define STT_FUNC 2

/* An index of ELF sections we care about.  */

enum debug_section
{
  DEBUG_INFO,
  DEBUG_LINE,
  DEBUG_ABBREV,
  DEBUG_RANGES,
  DEBUG_STR,
  GNU_DEBUGLINK,
  DEBUG_MAX
};

/* Names of sections, indexed by enum elf_section.  */

static const char * const debug_section_names[DEBUG_MAX] =
{
  ".debug_info",
  ".debug_line",
  ".debug_abbrev",
  ".debug_ranges",
  ".debug_str",
  ".gnu_debuglink"
};

/* Information we gather for the sections we care about.  */

struct debug_section_info
{
  /* Section file offset.  */
  off_t offset;
  /* Section size.  */
  size_t size;
  /* Section contents, after read from file.  */
  const unsigned char *data;
};

/* Information we keep for an ELF symbol.  */

struct elf_symbol
{
  /* The name of the symbol.  */
  const char *name;
  /* The address of the symbol.  */
  uintptr_t address;
  /* The size of the symbol.  */
  size_t size;
};

/* Information to pass to elf_syminfo.  */

struct elf_syminfo_data
{
  /* Symbols for the next module.  */
  struct elf_syminfo_data *next;
  /* The ELF symbols, sorted by address.  */
  struct elf_symbol *symbols;
  /* The number of symbols.  */
  size_t count;
  /* The base address for this module. */
  uintptr_t base_address;
  /* Address symbols size. */
  uintptr_t symbol_size;
};

/* A dummy callback function used when we can't find any debug info.  */

static int
elf_nodebug (struct backtrace_state *state ATTRIBUTE_UNUSED,
	     uintptr_t pc ATTRIBUTE_UNUSED,
	     backtrace_full_callback callback ATTRIBUTE_UNUSED,
	     backtrace_error_callback error_callback, void *data)
{
  error_callback (data, "no debug info in ELF executable", -1);
  return 0;
}

/* A dummy callback function used when we can't find a symbol
   table.  */

static void
elf_nosyms (struct backtrace_state *state ATTRIBUTE_UNUSED,
	    uintptr_t addr ATTRIBUTE_UNUSED,
	    backtrace_syminfo_callback callback ATTRIBUTE_UNUSED,
	    backtrace_error_callback error_callback, void *data)
{
  error_callback (data, "no symbol table in ELF executable", -1);
}

/* Compare struct elf_symbol for qsort.  */

static int
elf_symbol_compare (const void *v1, const void *v2)
{
  const struct elf_symbol *e1 = (const struct elf_symbol *) v1;
  const struct elf_symbol *e2 = (const struct elf_symbol *) v2;

  if (e1->address < e2->address)
    return -1;
  else if (e1->address > e2->address)
    return 1;
  else
    return 0;
}

/* Compare an ADDR against an elf_symbol for bsearch.  We allocate one
   extra entry in the array so that this can look safely at the next
   entry.  */

static int
elf_symbol_search (const void *vkey, const void *ventry)
{
  const uintptr_t *key = (const uintptr_t *) vkey;
  const struct elf_symbol *entry = (const struct elf_symbol *) ventry;
  uintptr_t addr;

  addr = *key;
  if (addr < entry->address)
    return -1;
  else if (addr >= entry->address + entry->size)
    return 1;
  else
    return 0;
}

/* Initialize the symbol table info for elf_syminfo.  */

static int
elf_initialize_syminfo (struct backtrace_state *state,
			uintptr_t base_address,
			const unsigned char *symtab_data, size_t symtab_size,
			const unsigned char *strtab, size_t strtab_size,
			backtrace_error_callback error_callback,
			void *data, struct elf_syminfo_data *sdata)
{
  size_t sym_count;
  const b_elf_sym *sym;
  size_t elf_symbol_count;
  size_t elf_symbol_size;
  struct elf_symbol *elf_symbols;
  size_t symbol_size = 0;
  size_t i;
  unsigned int j;

  sym_count = symtab_size / sizeof (b_elf_sym);

  /* We only care about function symbols.  Count them.  */
  sym = (const b_elf_sym *) symtab_data;
  elf_symbol_count = 0;
  for (i = 0; i < sym_count; ++i, ++sym)
    {
      int info;

      info = sym->st_info & 0xf;
      if ((info == STT_FUNC || info == STT_OBJECT)
	  && sym->st_shndx != SHN_UNDEF)
	++elf_symbol_count;
    }

  elf_symbol_size = elf_symbol_count * sizeof (struct elf_symbol);
  elf_symbols = ((struct elf_symbol *)
		 backtrace_alloc (state, elf_symbol_size, error_callback,
				  data));
  if (elf_symbols == NULL)
    return 0;

  sym = (const b_elf_sym *) symtab_data;
  j = 0;
  for (i = 0; i < sym_count; ++i, ++sym)
    {
      int info;

      info = sym->st_info & 0xf;
      if (info != STT_FUNC && info != STT_OBJECT)
	continue;
      if (sym->st_shndx == SHN_UNDEF)
	continue;
      if (sym->st_name >= strtab_size)
	{
	  error_callback (data, "symbol string index out of range", 0);
	  backtrace_free (state, elf_symbols, elf_symbol_size, error_callback,
			  data);
	  return 0;
	}
      elf_symbols[j].name = (const char *) strtab + sym->st_name;
      elf_symbols[j].address = sym->st_value + base_address;
      elf_symbols[j].size = sym->st_size;

      if (symbol_size < sym->st_value + sym->st_size)
        symbol_size = sym->st_value + sym->st_size;
      ++j;
    }

  qsort (elf_symbols, elf_symbol_count, sizeof (struct elf_symbol),
	 elf_symbol_compare);

  sdata->next = NULL;
  sdata->symbols = elf_symbols;
  sdata->count = elf_symbol_count;
  sdata->base_address = base_address;
  sdata->symbol_size = symbol_size;

  return 1;
}

/* Add EDATA to the list in STATE.  */

static void
elf_add_syminfo_data (struct backtrace_state *state,
		      struct elf_syminfo_data *edata)
{
  if (!state->threaded)
    {
      struct elf_syminfo_data **pp;

      for (pp = (struct elf_syminfo_data **) (void *) &state->syminfo_data;
	   *pp != NULL;
	   pp = &(*pp)->next)
	;
      *pp = edata;
    }
  else
    {
      while (1)
	{
	  struct elf_syminfo_data **pp;

	  pp = (struct elf_syminfo_data **) (void *) &state->syminfo_data;

	  while (1)
	    {
	      struct elf_syminfo_data *p;

	      p = backtrace_atomic_load_pointer (pp);

	      if (p == NULL)
		break;

	      pp = &p->next;
	    }

	  if (__sync_bool_compare_and_swap (pp, NULL, edata))
	    break;
	}
    }
}

/* Return the symbol name and value for an ADDR.  */

static void
elf_syminfo (struct backtrace_state *state, uintptr_t addr,
	     backtrace_syminfo_callback callback,
	     backtrace_error_callback error_callback ATTRIBUTE_UNUSED,
	     void *data)
{
  struct elf_syminfo_data *edata;
  struct elf_symbol *sym = NULL;

  if (!state->threaded)
    {
      for (edata = (struct elf_syminfo_data *) state->syminfo_data;
	   edata != NULL;
	   edata = edata->next)
	{
          if ((addr >= edata->base_address) && (addr < edata->base_address + edata->symbol_size))
            {
	      sym = ((struct elf_symbol *)
		     bsearch (&addr, edata->symbols, edata->count,
			      sizeof (struct elf_symbol), elf_symbol_search));
	      if (sym != NULL)
	        break;
            }
	}
    }
  else
    {
      struct elf_syminfo_data **pp;

      pp = (struct elf_syminfo_data **) (void *) &state->syminfo_data;
      while (1)
	{
	  edata = backtrace_atomic_load_pointer (pp);
	  if (edata == NULL)
	    break;

          if ((addr >= edata->base_address) && (addr < edata->base_address + edata->symbol_size))
            {
	      sym = ((struct elf_symbol *)
		     bsearch (&addr, edata->symbols, edata->count,
			      sizeof (struct elf_symbol), elf_symbol_search));
	      if (sym != NULL)
	        break;
            }

	  pp = &edata->next;
	}
    }

  if (sym == NULL)
    callback (data, addr, NULL, 0, 0);
  else
    callback (data, addr, sym->name, sym->address, sym->size);
}

/* Search this section for the .gnu_debuglink build id uuid. */

static uint32_t 
elf_parse_gnu_buildid(const uint8_t *data, size_t len, uint8_t uuid[20])
{
    typedef struct
    {
        uint32_t name_len;  // Length of note name
        uint32_t desc_len;  // Length of note descriptor
        uint32_t type;      // Type of note (1 is ABI_TAG, 3 is BUILD_ID)
        char name[4];       // "GNU\0"
        uint8_t uuid[16];   // This can be 16 or 20 bytes
    } elf_notehdr;

    /* Try to parse the note section (ie .note.gnu.build-id|.notes|.note|...) and get the build id.
       BuildID documentation: https://fedoraproject.org/wiki/Releases/FeatureBuildId */
    static const uint32_t s_gnu_build_id = 3; // NT_GNU_BUILD_ID from elf.h

    while (len >= sizeof(elf_notehdr))
    {
        const elf_notehdr *notehdr = (const elf_notehdr *)data;
        uint32_t name_len = (notehdr->name_len + 3) & ~3;
        uint32_t desc_len = (notehdr->desc_len + 3) & ~3;
        size_t offset_next_note = name_len + desc_len;

        /* 16 bytes is UUID|MD5, 20 bytes is SHA1 */
        if ((notehdr->type == s_gnu_build_id) &&
            (desc_len == 16 || desc_len == 20) &&
            (name_len == 4) && !strcmp(notehdr->name, "GNU"))
        {
            const uint8_t *uuidbuf = data + offsetof(elf_notehdr, uuid);

            memcpy(uuid, uuidbuf, desc_len);
            return desc_len;
        }

        if (offset_next_note >= len)
            break;
        data += offset_next_note;
        len -= offset_next_note;
    }

    return 0;
}

static void
uuid_to_str(char *uuid_str, const uint8_t *uuid, int len) 
{
    int i;
    static const char hex[] = "0123456789abcdef";

    for (i = 0; i < len; i++)
      {
        uint8_t c = *uuid++;

        *uuid_str++ = hex[c >> 4];
        *uuid_str++ = hex[c & 0xf];
      }    
    *uuid_str = 0;
}

static int
elf_add (struct backtrace_state *state, const char *filename, uintptr_t base_address,
	 backtrace_error_callback error_callback, void *data,
	 fileline *fileline_fn, int *found_sym, int *found_dwarf, int exe,
         const uint8_t *uuid_to_match, uint32_t uuid_to_match_len);

/* Search and add the backtrace for a gnu_debuglink file. */

static int
elf_add_gnu_debuglink (struct backtrace_state *state, const char *modulename, const char *gnu_debuglink,
                       uintptr_t base_address, backtrace_error_callback error_callback, void *data,
               	       fileline *fileline_fn, int *found_sym, int *found_dwarf,
                       const uint8_t *uuid_to_match, uint32_t uuid_to_match_len)
{
  int pass;
  char file[PATH_MAX];
  char uuid_str[20 * 2 + 1];
  const char *homedir = NULL;
  const char *modulefile = strrchr(modulename, '/');
  int moduledirlen = modulefile ? (modulefile - modulename) : 0;

  uuid_to_str(uuid_str, uuid_to_match, uuid_to_match_len); 

  for (pass = 0;; ++pass)
    {
      file[0] = 0;

      switch (pass)
      {
      case 0:
        /* Try the gnu_debuglink filename we were passed in. */
        strncpy(file, gnu_debuglink, sizeof(file));
        break;

      case 1:
        if (moduledirlen > 0)
        {
          /* Try current module directory. */
          snprintf(file, sizeof(file), "%.*s/%s", moduledirlen, modulename, gnu_debuglink);
        }
        break;

      case 2:
        {
          /* current exe directory. */
          char buf[PATH_MAX];
          int len = readlink("/proc/self/exe", buf, sizeof(buf));
          if (len > 0)
            {
              snprintf(file, sizeof(file), "%.*s/%s", len, buf, gnu_debuglink);
            }
        }
        break;

      case 3:
        if (moduledirlen > 0)
        {
          /* Try /lib/x86_64-linux-gnu/libc-2.15.so --> /usr/lib/debug/lib/x86_64-linux-gnu/libc-2.15.so */
          snprintf(file, sizeof(file), "/usr/lib/debug%.*s/%s", moduledirlen, modulename, gnu_debuglink);
        }
        break;

      case 4:
        {
          struct passwd *pw = getpwuid(getuid());
          homedir = pw ? pw->pw_dir : NULL;
          if (homedir)
            {
              /* try ~/.debug/.build-id/af/1da84a6c83719a4c92cc6e7144623c5a1d1f6d */
              snprintf(file, sizeof(file), "%s/.debug/.build-id/%.2s/%s", homedir, uuid_str, uuid_str + 2);
            }
        }
        break;

      case 5:
        if (homedir)
        {
            /* try /home/mikesart/.debug/.build-id/af/1da84a6c83719a4c92cc6e7144623c5a1d1f6d.debug */
            snprintf(file, sizeof(file), "%s/.debug/.build-id/%.2s/%s.debug", homedir, uuid_str, uuid_str + 2);
        }
        break;

      case 6:
          snprintf(file, sizeof(file), "%s/.build-id/%.2s/%s.debug", "/mnt/symstoresymbols/Debug", uuid_str, uuid_str + 2);
          //$ TODO: mikesart
          // Check debug-file-directory from .gdbinit maybe?
          //   /mnt/symstoresymbols/Debug/.build-id/f5/a99d37b2b105c50bccc3fd87e4cd27492f3032.debug
          //   set debug-file-directory /usr/lib/debug:/mnt/symstoresymbols/debug
          break;

      default:
        return 0;
      }

      file[sizeof(file) - 1] = 0;

      /* If we've got a filename and the file exists, try loading it. */

      if (file[0] && (access(file, F_OK) == 0))
        {
          if (elf_add(state, file, base_address,
                      error_callback, data,
                      fileline_fn, found_sym, found_dwarf, 0,
                      uuid_to_match, uuid_to_match_len))
          {
            /* Success. Woot! Return our fresh new symbols. */
            return 1;
          }
        }
    }

  return 0;
}


/* Add the backtrace data for one ELF file.  */

static int
elf_add (struct backtrace_state *state, const char *filename, uintptr_t base_address,
	 backtrace_error_callback error_callback, void *data,
	 fileline *fileline_fn, int *found_sym, int *found_dwarf, int exe,
         const uint8_t *uuid_to_match, uint32_t uuid_to_match_len)
{
  struct backtrace_view ehdr_view;
  b_elf_ehdr ehdr;
  off_t shoff;
  unsigned int shnum;
  unsigned int shstrndx;
  struct backtrace_view shdrs_view;
  int shdrs_view_valid;
  const b_elf_shdr *shdrs;
  const b_elf_shdr *shstrhdr;
  size_t shstr_size;
  off_t shstr_off;
  struct backtrace_view names_view;
  int names_view_valid;
  const char *names;
  unsigned int symtab_shndx;
  unsigned int dynsym_shndx;
  unsigned int i;
  struct debug_section_info sections[DEBUG_MAX];
  struct backtrace_view symtab_view;
  int symtab_view_valid;
  struct backtrace_view strtab_view;
  int strtab_view_valid;
  off_t min_offset;
  off_t max_offset;
  struct backtrace_view debug_view;
  int debug_view_valid;
  int retval = 0;
  uint32_t uuid_len = 0;
  uint8_t uuid[20];

  *found_sym = 0;
  *found_dwarf = 0;

  shdrs_view_valid = 0;
  names_view_valid = 0;
  symtab_view_valid = 0;
  strtab_view_valid = 0;
  debug_view_valid = 0;

  int descriptor = backtrace_open (filename, error_callback, data, NULL);
  if (descriptor < 0)
    return 0;

  if (!backtrace_get_view (state, descriptor, 0, sizeof ehdr, error_callback,
			   data, &ehdr_view))
    goto fail;

  memcpy (&ehdr, ehdr_view.data, sizeof ehdr);

  backtrace_release_view (state, &ehdr_view, error_callback, data);

  if (ehdr.e_ident[EI_MAG0] != ELFMAG0
      || ehdr.e_ident[EI_MAG1] != ELFMAG1
      || ehdr.e_ident[EI_MAG2] != ELFMAG2
      || ehdr.e_ident[EI_MAG3] != ELFMAG3)
    {
      error_callback (data, "executable file is not ELF", 0);
      goto fail;
    }
  if (ehdr.e_ident[EI_VERSION] != EV_CURRENT)
    {
      error_callback (data, "executable file is unrecognized ELF version", 0);
      goto fail;
    }

#if BACKTRACE_ELF_SIZE == 32
#define BACKTRACE_ELFCLASS ELFCLASS32
#else
#define BACKTRACE_ELFCLASS ELFCLASS64
#endif

  if (ehdr.e_ident[EI_CLASS] != BACKTRACE_ELFCLASS)
    {
      error_callback (data, "executable file is unexpected ELF class", 0);
      goto fail;
    }

  if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB
      && ehdr.e_ident[EI_DATA] != ELFDATA2MSB)
    {
      error_callback (data, "executable file has unknown endianness", 0);
      goto fail;
    }

  /* If the executable is ET_DYN, it is either a PIE, or we are running
     directly a shared library with .interp.  We need to wait for
     dl_iterate_phdr in that case to determine the actual base_address.  */
  if (exe)
  {
    /* If we were given a base address and this isn't PIE, set addr to 0. */
    if (base_address && (ehdr.e_type != ET_DYN))
      base_address = 0;
    else if (!base_address && (ehdr.e_type == ET_DYN))
      goto succeeded;
  }

  shoff = ehdr.e_shoff;
  shnum = ehdr.e_shnum;
  shstrndx = ehdr.e_shstrndx;

  if ((shnum == 0 || shstrndx == SHN_XINDEX)
      && shoff != 0)
    {
      struct backtrace_view shdr_view;
      const b_elf_shdr *shdr;

      if (!backtrace_get_view (state, descriptor, shoff, sizeof shdr,
			       error_callback, data, &shdr_view))
	goto fail;

      shdr = (const b_elf_shdr *) shdr_view.data;

      if (shnum == 0)
	shnum = shdr->sh_size;

      if (shstrndx == SHN_XINDEX)
	{
	  shstrndx = shdr->sh_link;

	  /* Versions of the GNU binutils between 2.12 and 2.18 did
	     not handle objects with more than SHN_LORESERVE sections
	     correctly.  All large section indexes were offset by
	     0x100.  There is more information at
	     http://sourceware.org/bugzilla/show_bug.cgi?id-5900 .
	     Fortunately these object files are easy to detect, as the
	     GNU binutils always put the section header string table
	     near the end of the list of sections.  Thus if the
	     section header string table index is larger than the
	     number of sections, then we know we have to subtract
	     0x100 to get the real section index.  */
	  if (shstrndx >= shnum && shstrndx >= SHN_LORESERVE + 0x100)
	    shstrndx -= 0x100;
	}

      backtrace_release_view (state, &shdr_view, error_callback, data);
    }

  /* To translate PC to file/line when using DWARF, we need to find
     the .debug_info and .debug_line sections.  */

  /* Read the section headers, skipping the first one.  */

  if (!backtrace_get_view (state, descriptor, shoff + sizeof (b_elf_shdr),
			   (shnum - 1) * sizeof (b_elf_shdr),
			   error_callback, data, &shdrs_view))
    goto fail;
  shdrs_view_valid = 1;
  shdrs = (const b_elf_shdr *) shdrs_view.data;

  /* Read the section names.  */

  shstrhdr = &shdrs[shstrndx - 1];
  shstr_size = shstrhdr->sh_size;
  shstr_off = shstrhdr->sh_offset;

  if (!backtrace_get_view (state, descriptor, shstr_off, shstr_size,
			   error_callback, data, &names_view))
    goto fail;
  names_view_valid = 1;
  names = (const char *) names_view.data;

  symtab_shndx = 0;
  dynsym_shndx = 0;

  memset (sections, 0, sizeof sections);
  memset (uuid, 0, sizeof(uuid));

  /* Look for the symbol table.  */
  for (i = 1; i < shnum; ++i)
    {
      const b_elf_shdr *shdr;
      unsigned int sh_name;
      const char *name;
      int j;

      shdr = &shdrs[i - 1];

      if (shdr->sh_type == SHT_SYMTAB)
	symtab_shndx = i;
      else if (shdr->sh_type == SHT_DYNSYM)
	dynsym_shndx = i;
      else if ((shdr->sh_type == SHT_NOTE) && (uuid_len == 0))
      {
        struct backtrace_view note_view;

        if (backtrace_get_view (state, descriptor, shdr->sh_offset,
                                 shdr->sh_size, error_callback, data,
                                 &note_view))
        {
          uuid_len = elf_parse_gnu_buildid(note_view.data, shdr->sh_size, uuid);
          backtrace_release_view (state, &note_view, error_callback, data);
        }
      }

      sh_name = shdr->sh_name;
      if (sh_name >= shstr_size)
	{
	  error_callback (data, "ELF section name out of range", 0);
	  goto fail;
	}

      name = names + sh_name;

      for (j = 0; j < (int) DEBUG_MAX; ++j)
	{
	  if (strcmp (name, debug_section_names[j]) == 0)
	    {
	      sections[j].offset = shdr->sh_offset;
	      sections[j].size = shdr->sh_size;
	      break;
	    }
	}
    }

  if (uuid_to_match_len)
    {
      /* We're supposed to be looking for a gnu_debuglink file... */

      /* Found debug file but uuids don't match... */
      if ((uuid_to_match_len != uuid_len) || memcmp(uuid_to_match, uuid, uuid_len))
        goto fail;

      /* Found debug file, but no .debuginfo in it? */
      if (!sections[DEBUG_INFO].size)
        goto fail;
    }
  else if (uuid_len && !sections[DEBUG_INFO].size && sections[GNU_DEBUGLINK].size)
    {
      /* We're not searching for a gnu_debuglink file, we have no .debug_info section, we
         have a uuid, and we have a gnu_debuglink filename - try looking for this debug file. */
      struct backtrace_view gnu_debuglink_view;

      if (backtrace_get_view (state, descriptor, sections[GNU_DEBUGLINK].offset,
		              sections[GNU_DEBUGLINK].size, error_callback, data,
		              &gnu_debuglink_view))
        {
          int added = 0;
          int debug_found_sym = 0;
          int debug_found_dwarf = 0;
          const char *gnu_debuglink_file = gnu_debuglink_view.data;

          if (gnu_debuglink_file && gnu_debuglink_file[0])
            {
              added = elf_add_gnu_debuglink (state, filename, gnu_debuglink_file, base_address, error_callback,
                                             data, fileline_fn, &debug_found_sym, &debug_found_dwarf,
                                             uuid, uuid_len);
            }

          backtrace_release_view (state, &gnu_debuglink_view, error_callback, data);
          gnu_debuglink_file = NULL;

          /* If we found symbols of any kind, head on out... */
          if (added && (debug_found_sym || debug_found_dwarf))
            {
              *found_sym = debug_found_sym;
              *found_dwarf = debug_found_dwarf;
              goto succeeded;
            }
        }
    }

  if (symtab_shndx == 0)
    symtab_shndx = dynsym_shndx;
  if (symtab_shndx != 0)
    {
      const b_elf_shdr *symtab_shdr;
      unsigned int strtab_shndx;
      const b_elf_shdr *strtab_shdr;
      struct elf_syminfo_data *sdata;

      symtab_shdr = &shdrs[symtab_shndx - 1];
      strtab_shndx = symtab_shdr->sh_link;
      if (strtab_shndx >= shnum)
	{
	  error_callback (data,
			  "ELF symbol table strtab link out of range", 0);
	  goto fail;
	}
      strtab_shdr = &shdrs[strtab_shndx - 1];

      if (!backtrace_get_view (state, descriptor, symtab_shdr->sh_offset,
			       symtab_shdr->sh_size, error_callback, data,
			       &symtab_view))
	goto fail;
      symtab_view_valid = 1;

      if (!backtrace_get_view (state, descriptor, strtab_shdr->sh_offset,
			       strtab_shdr->sh_size, error_callback, data,
			       &strtab_view))
	goto fail;
      strtab_view_valid = 1;

      sdata = ((struct elf_syminfo_data *)
	       backtrace_alloc (state, sizeof *sdata, error_callback, data));
      if (sdata == NULL)
	goto fail;

      if (!elf_initialize_syminfo (state, base_address,
				   symtab_view.data, symtab_shdr->sh_size,
				   strtab_view.data, strtab_shdr->sh_size,
				   error_callback, data, sdata))
	{
	  backtrace_free (state, sdata, sizeof *sdata, error_callback, data);
	  goto fail;
	}

      /* We no longer need the symbol table, but we hold on to the
	 string table permanently.  */
      backtrace_release_view (state, &symtab_view, error_callback, data);

      *found_sym = 1;

      elf_add_syminfo_data (state, sdata);
    }

  /* FIXME: Need to handle compressed debug sections.  */

  backtrace_release_view (state, &shdrs_view, error_callback, data);
  shdrs_view_valid = 0;
  backtrace_release_view (state, &names_view, error_callback, data);
  names_view_valid = 0;

  /* Read all the debug sections in a single view, since they are
     probably adjacent in the file.  We never release this view.  */

  min_offset = 0;
  max_offset = 0;
  for (i = 0; i < (int) DEBUG_MAX; ++i)
    {
      off_t end;

      if (sections[i].size == 0)
	continue;
      if (min_offset == 0 || sections[i].offset < min_offset)
	min_offset = sections[i].offset;
      end = sections[i].offset + sections[i].size;
      if (end > max_offset)
	max_offset = end;
    }
  if (min_offset == 0 || max_offset == 0)
    {
      if (!backtrace_close (descriptor, error_callback, data))
      {
        descriptor = -1;
	goto fail;
      }
      *fileline_fn = elf_nodebug;
      return 1;
    }

  if (!backtrace_get_view (state, descriptor, min_offset,
			   max_offset - min_offset,
			   error_callback, data, &debug_view))
    goto fail;
  debug_view_valid = 1;

  /* We've read all we need from the executable.  */
  if (!backtrace_close (descriptor, error_callback, data))
  {
    descriptor = -1;
    goto fail;
  }
  descriptor = -1;

  for (i = 0; i < (int) DEBUG_MAX; ++i)
    {
      if (sections[i].size == 0)
	sections[i].data = NULL;
      else
	sections[i].data = ((const unsigned char *) debug_view.data
			    + (sections[i].offset - min_offset));
    }

  if (!backtrace_dwarf_add (state, base_address,
			    sections[DEBUG_INFO].data,
			    sections[DEBUG_INFO].size,
			    sections[DEBUG_LINE].data,
			    sections[DEBUG_LINE].size,
			    sections[DEBUG_ABBREV].data,
			    sections[DEBUG_ABBREV].size,
			    sections[DEBUG_RANGES].data,
			    sections[DEBUG_RANGES].size,
			    sections[DEBUG_STR].data,
			    sections[DEBUG_STR].size,
			    ehdr.e_ident[EI_DATA] == ELFDATA2MSB,
			    error_callback, data, fileline_fn))
    goto fail;

  state->debug_filename = backtrace_strdup(state, filename, error_callback, data);
  *found_dwarf = 1;
  return 1;

succeeded:
  retval = 1;

fail:
  if (shdrs_view_valid)
    backtrace_release_view (state, &shdrs_view, error_callback, data);
  if (names_view_valid)
    backtrace_release_view (state, &names_view, error_callback, data);
  if (symtab_view_valid)
    backtrace_release_view (state, &symtab_view, error_callback, data);
  if (strtab_view_valid)
    backtrace_release_view (state, &strtab_view, error_callback, data);
  if (debug_view_valid)
    backtrace_release_view (state, &debug_view, error_callback, data);
  if (descriptor != -1)
    backtrace_close (descriptor, error_callback, data);
  return retval;
}

/* Data passed to phdr_callback.  */

struct phdr_data
{
  struct backtrace_state *state;
  backtrace_error_callback error_callback;
  void *data;
  fileline *fileline_fn;
  int *found_sym;
  int *found_dwarf;
  const char * exe_filename;
};

/* Callback passed to dl_iterate_phdr.  Load debug info from shared
   libraries.  */

static int
phdr_callback (struct dl_phdr_info *info, size_t size ATTRIBUTE_UNUSED,
	       void *pdata)
{
  struct phdr_data *pd = (struct phdr_data *) pdata;
  fileline elf_fileline_fn;
  int found_dwarf;
  const char * filename = info->dlpi_name;

  /* There is not much we can do if we don't have the module name,
     unless executable is ET_DYN, where we expect the very first
     phdr_callback to be for the PIE.  */
  if (filename == NULL || filename[0] == '\0')
    {
      if (!pd->exe_filename)
	return 0;
      filename = pd->exe_filename;
      pd->exe_filename = NULL;
    }

  if (elf_add (pd->state, filename, info->dlpi_addr, pd->error_callback,
	       pd->data, &elf_fileline_fn, pd->found_sym, &found_dwarf, 0,
               NULL, 0))
    {
      if (found_dwarf)
	{
	  *pd->found_dwarf = 1;
	  *pd->fileline_fn = elf_fileline_fn;
	}
    }

  return 0;
}

/* Initialize the backtrace data we need from an ELF executable.  At
   the ELF level, all we need to do is find the debug info
   sections.  */

int
backtrace_initialize (struct backtrace_state *state, const char *filename,
		      uintptr_t base_address, int is_exe,
		      backtrace_error_callback error_callback,
		      void *data, fileline *fileline_fn)
{
  int ret;
  int found_sym;
  int found_dwarf;
  fileline elf_fileline_fn;

  ret = elf_add (state, filename, base_address, error_callback, data, &elf_fileline_fn,
		&found_sym, &found_dwarf, is_exe, NULL, 0);
  if (!ret)
    return 0;

  if (!base_address)
    {
      struct phdr_data pd;

      pd.state = state;
      pd.error_callback = error_callback;
      pd.data = data;
      pd.fileline_fn = &elf_fileline_fn;
      pd.found_sym = &found_sym;
      pd.found_dwarf = &found_dwarf;
      pd.exe_filename = filename;
    
      dl_iterate_phdr (phdr_callback, (void *) &pd);
    }

  if (!state->threaded)
    {
      if (found_sym)
	state->syminfo_fn = elf_syminfo;
      else if (state->syminfo_fn == NULL)
	state->syminfo_fn = elf_nosyms;
    }
  else
    {
      if (found_sym)
	backtrace_atomic_store_pointer (&state->syminfo_fn, elf_syminfo);
      else
	__sync_bool_compare_and_swap (&state->syminfo_fn, NULL, elf_nosyms);
    }

  if (!state->threaded)
    {
      if (state->fileline_fn == NULL || state->fileline_fn == elf_nodebug)
	*fileline_fn = elf_fileline_fn;
    }
  else
    {
      fileline current_fn;

      current_fn = backtrace_atomic_load_pointer (&state->fileline_fn);
      if (current_fn == NULL || current_fn == elf_nodebug)
	*fileline_fn = elf_fileline_fn;
    }

  return 1;
}

static void
elf_get_uuid_error_callback(void *data, const char *msg, int errnum)
{
    // Do nothing here. elf_get_uuid() will return the error.
}

int
elf_get_uuid (struct backtrace_state *state,
              const char *filename, uint8_t uuid[20], int *uuid_len)
{
  struct backtrace_view ehdr_view;
  b_elf_ehdr ehdr;
  off_t shoff;
  unsigned int shnum;
  struct backtrace_view shdrs_view;
  int shdrs_view_valid = 0;
  const b_elf_shdr *shdrs;
  unsigned int i;
  void *data = NULL;
  int retval = 0;
  struct backtrace_state *state_alloced = NULL;

  if (!state)
  {
      state_alloced = backtrace_create_state(filename, 0, elf_get_uuid_error_callback, NULL);
      state = state_alloced;
  }

  int descriptor = backtrace_open (filename, elf_get_uuid_error_callback, data, NULL);
  if (descriptor < 0)
    return 0;

  if (!backtrace_get_view (state, descriptor, 0, sizeof ehdr, elf_get_uuid_error_callback,
			   data, &ehdr_view))
    goto fail;

  memcpy (&ehdr, ehdr_view.data, sizeof ehdr);

  backtrace_release_view (state, &ehdr_view, elf_get_uuid_error_callback, data);

  if (ehdr.e_ident[EI_MAG0] != ELFMAG0
      || ehdr.e_ident[EI_MAG1] != ELFMAG1
      || ehdr.e_ident[EI_MAG2] != ELFMAG2
      || ehdr.e_ident[EI_MAG3] != ELFMAG3)
    {
      // elf_get_uuid_error_callback (data, "executable file is not ELF", 0);
      goto fail;
    }
  if (ehdr.e_ident[EI_VERSION] != EV_CURRENT)
    {
      // elf_get_uuid_error_callback (data, "executable file is unrecognized ELF version", 0);
      goto fail;
    }

#if BACKTRACE_ELF_SIZE == 32
#define BACKTRACE_ELFCLASS ELFCLASS32
#else
#define BACKTRACE_ELFCLASS ELFCLASS64
#endif

  if (ehdr.e_ident[EI_CLASS] != BACKTRACE_ELFCLASS)
    {
      // elf_get_uuid_error_callback (data, "executable file is unexpected ELF class", 0);
      goto fail;
    }

  if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB
      && ehdr.e_ident[EI_DATA] != ELFDATA2MSB)
    {
      // elf_get_uuid_error_callback (data, "executable file has unknown endianness", 0);
      goto fail;
    }

  shoff = ehdr.e_shoff;
  shnum = ehdr.e_shnum;

  if ((shnum == 0) && (shoff != 0))
    {
      struct backtrace_view shdr_view;
      const b_elf_shdr *shdr;

      if (!backtrace_get_view (state, descriptor, shoff, sizeof shdr,
			       elf_get_uuid_error_callback, data, &shdr_view))
	goto fail;

      shdr = (const b_elf_shdr *) shdr_view.data;

      if (shnum == 0)
	shnum = shdr->sh_size;

      backtrace_release_view (state, &shdr_view, elf_get_uuid_error_callback, data);
    }

  if (!backtrace_get_view (state, descriptor, shoff + sizeof (b_elf_shdr),
			   (shnum - 1) * sizeof (b_elf_shdr),
			   elf_get_uuid_error_callback, data, &shdrs_view))
    goto fail;
  shdrs_view_valid = 1;
  shdrs = (const b_elf_shdr *) shdrs_view.data;

  for (i = 1; i < shnum; ++i)
    {
      const b_elf_shdr *shdr = &shdrs[i - 1];

      if (shdr->sh_type == SHT_NOTE)
      {
        struct backtrace_view note_view;

        if (backtrace_get_view (state, descriptor, shdr->sh_offset,
                                 shdr->sh_size, elf_get_uuid_error_callback, data,
                                 &note_view))
        {
          *uuid_len = elf_parse_gnu_buildid(note_view.data, shdr->sh_size, uuid);
          backtrace_release_view (state, &note_view, elf_get_uuid_error_callback, data);
          if (*uuid_len)
          {
             retval = 1;
             break;
          }
        }
      }
    }

fail:
  if (shdrs_view_valid)
    backtrace_release_view (state, &shdrs_view, elf_get_uuid_error_callback, data);
  if (descriptor != -1)
    backtrace_close (descriptor, elf_get_uuid_error_callback, data);
  if (state_alloced)
    backtrace_free(state_alloced, state_alloced, sizeof(*state_alloced), elf_get_uuid_error_callback, NULL);
  return retval;
}
