/* dwarf2.h -- minimal GCC dwarf2.h replacement for libbacktrace
   Contributed by Alexander Monakov, ISP RAS

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

#ifndef BACKTRACE_AUX_DWARF2_H
#define BACKTRACE_AUX_DWARF2_H

/* Use the system header for the bulk of the definitions.  */
#include <dwarf.h>

/* Provide stub enum tags.  */
enum dwarf_attribute {_dummy_dwarf_attribute};
enum dwarf_form {_dummy_dwarf_form};
enum dwarf_tag {_dummy_dwarf_tag};

/* Define potentially missing enum values.  */
#define DW_FORM_GNU_addr_index 0x1f01
#define DW_FORM_GNU_str_index  0x1f02

#define DW_FORM_GNU_ref_alt    0x1f20
#define DW_FORM_GNU_strp_alt   0x1f21

#define DW_LNS_extended_op 0

#endif
