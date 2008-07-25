/*
 * DMI Decode
 *
 *   (C) 2000-2002 Alan Cox <alan@redhat.com>
 *   (C) 2002-2007 Jean Delvare <khali@linux-fr.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *   For the avoidance of doubt the "preferred form" of this code is one which
 *   is in an open unpatent encumbered format. Where cryptographic key signing
 *   forms part of the process of creating an executable the information
 *   including keys needed to generate an equivalently functional executable
 *   are deemed to be part of the source code.
 *
 * Unless specified otherwise, all references are aimed at the "System
 * Management BIOS Reference Specification, Version 2.5" document,
 * available from http://www.dmtf.org/standards/smbios/.
 *
 * Note to contributors:
 * Please reference every value you add or modify, especially if the
 * information does not come from the above mentioned specification.
 *
 * Additional references:
 *  - Intel AP-485 revision 31
 *    "Intel Processor Identification and the CPUID Instruction"
 *    http://developer.intel.com/design/xeon/applnots/241618.htm
 *  - DMTF Master MIF version 040707
 *    "DMTF approved standard groups"
 *    http://www.dmtf.org/standards/dmi
 *  - IPMI 2.0 revision 1.0
 *    "Intelligent Platform Management Interface Specification"
 *    http://developer.intel.com/design/servers/ipmi/spec.htm
 *  - AMD publication #25481 revision 2.18
 *    "CPUID Specification"
 *    http://www.amd.com/us-en/assets/content_type/white_papers_and_tech_docs/25481.pdf
 */

#include <Python.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "version.h"
#include "config.h"
#include "types.h"
#include "util.h"
#include "dmidecode.h"
#include "dmiopt.h"
#include "dmioem.h"

#include "global.h"
#include "catsprintf.h"

static const char *out_of_spec = "<OUT OF SPEC>";
static const char *bad_index = "<BAD INDEX>";

/*******************************************************************************
** Type-independant Stuff
*/

PyObject *dmi_string_py(struct dmi_header *dm, u8 s) {
  char *bp=(char *)dm->data;
  size_t i, len;

  if(s==0) return PyString_FromString("Not Specified");

  bp += dm->length;
  while(s>1 && *bp) {
    bp += strlen(bp);
    bp++;
    s--;
  }

  if(!*bp) return PyString_FromString(bad_index);

  /* ASCII filtering */
  len=strlen(bp);
  for(i=0; i<len; i++)
    if(bp[i]<32 || bp[i]==127)
      bp[i]='.';

  return PyString_FromString(bp);
}

const char *dmi_string(struct dmi_header *dm, u8 s) {
  char *bp=(char *)dm->data;
  size_t i, len;

  if(s==0) return "Not Specified";

  bp += dm->length;
  while(s>1 && *bp) {
    bp+=strlen(bp);
    bp++;
    s--;
  }

  if(!*bp) return bad_index;

  /* ASCII filtering */
  len=strlen(bp);
  for(i=0; i<len; i++)
    if(bp[i]<32 || bp[i]==127)
      bp[i]='.';

  return bp;
}




static const char *dmi_smbios_structure_type(u8 code) {
  static const char *type[]={
    "BIOS", /* 0 */
    "System",
    "Base Board",
    "Chassis",
    "Processor",
    "Memory Controller",
    "Memory Module",
    "Cache",
    "Port Connector",
    "System Slots",
    "On Board Devices",
    "OEM Strings",
    "System Configuration Options",
    "BIOS Language",
    "Group Associations",
    "System Event Log",
    "Physical Memory Array",
    "Memory Device",
    "32-bit Memory Error",
    "Memory Array Mapped Address",
    "Memory Device Mapped Address",
    "Built-in Pointing Device",
    "Portable Battery",
    "System Reset",
    "Hardware Security",
    "System Power Controls",
    "Voltage Probe",
    "Cooling Device",
    "Temperature Probe",
    "Electrical Current Probe",
    "Out-of-band Remote Access",
    "Boot Integrity Services",
    "System Boot",
    "64-bit Memory Error",
    "Management Device",
    "Management Device Component",
    "Management Device Threshold Data",
    "Memory Channel",
    "IPMI Device",
    "Power Supply" /* 39 */
  };

  if(code<=39) return(type[code]);
  return out_of_spec;
}

static int dmi_bcd_range(u8 value, u8 low, u8 high) {
  if(value>0x99 || (value&0x0F)>0x09) return 0;
  if(value<low || value>high) return 0;
  return 1;
}

const char *dmi_dump(struct dmi_header *h, char *_) {
  int row, i;
  const char *s;

  sprintf(_, "Header and Data");
  for(row=0; row<((h->length-1)>>4)+1; row++) {
    catsprintf(_, "{");
    for(i=0; i<16 && i<h->length-(row<<4); i++)
      catsprintf(_, "%s%02X", i?" ":"", (h->data)[(row<<4)+i]);
    catsprintf(_, "}");
  }

  if((h->data)[h->length] || (h->data)[h->length+1]) {
    catsprintf(_, "Strings:");
    i=1;
    while((s=dmi_string(h, i++))!=bad_index) {
      if(opt.flags & FLAG_DUMP) {
        int j, l = strlen(s)+1;
        for(row=0; row<((l-1)>>4)+1; row++) {
          catsprintf(_, "{");
          for(j=0; j<16 && j<l-(row<<4); j++)
            catsprintf(_, "%s%02X", j?" ":"", s[(row<<4)+j]);
          catsprintf(_, "}");
        }
        catsprintf(_, "\"%s\"|", s);
      }
      else catsprintf(_, "%s|", s);
    }
  }
  return _;
}

/*******************************************************************************
** 3.3.1 BIOS Information (Type 0)
*/

static const char* dmi_bios_runtime_size(u32 code, char* _) {
  if(code&0x000003FF) sprintf(_, "%u bytes", code);
  else sprintf(_, "%u kB", code>>10);
  return _;
}

/* 3.3.1.1 */
static const char* dmi_bios_characteristics(u64 code, char *_) {
  static const char *characteristics[] = {
    "BIOS characteristics not supported", /* 3 */
    "ISA is supported",
    "MCA is supported",
    "EISA is supported",
    "PCI is supported",
    "PC Card (PCMCIA) is supported",
    "PNP is supported",
    "APM is supported",
    "BIOS is upgradeable",
    "BIOS shadowing is allowed",
    "VLB is supported",
    "ESCD support is available",
    "Boot from CD is supported",
    "Selectable boot is supported",
    "BIOS ROM is socketed",
    "Boot from PC Card (PCMCIA) is supported",
    "EDD is supported",
    "Japanese floppy for NEC 9800 1.2 MB is supported (int 13h)",
    "Japanese floppy for Toshiba 1.2 MB is supported (int 13h)",
    "5.25\"/360 KB floppy services are supported (int 13h)",
    "5.25\"/1.2 MB floppy services are supported (int 13h)",
    "3.5\"/720 KB floppy services are supported (int 13h)",
    "3.5\"/2.88 MB floppy services are supported (int 13h)",
    "Print screen service is supported (int 5h)",
    "8042 keyboard services are supported (int 9h)",
    "Serial services are supported (int 14h)",
    "Printer services are supported (int 17h)",
    "CGA/mono video services are supported (int 10h)",
    "NEC PC-98" /* 31 */
  };

  /*
  ** TODO: This isn't very clear what this bit is supposed to mean
  */
  if(code.l&(1<<3)) {
    sprintf(_, characteristics[0]);
  } else {
    int i;
    catsprintf(_, NULL);
    for(i=4; i<=31; i++)
      if(code.l&(1<<i))
        catsprintf(_, "%s|", characteristics[i-3]);
  }
  return _;
}

/* 3.3.1.2.1 */
static const char* dmi_bios_characteristics_x1(u8 code, char *_) {
  static const char *characteristics[] = {
    "ACPI is supported", /* 0 */
    "USB legacy is supported",
    "AGP is supported",
    "I2O boot is supported",
    "LS-120 boot is supported",
    "ATAPI Zip drive boot is supported",
    "IEEE 1394 boot is supported",
    "Smart battery is supported" /* 7 */
  };

  int i;
  catsprintf(_, NULL);
  for(i=0; i<=7; i++)
    if(code&(1<<i))
      catsprintf(_, "%s|", characteristics[i]);
  return _;
}

/* 3.3.1.2.2 */
static const char* dmi_bios_characteristics_x2(u8 code, char *_) {
  static const char *characteristics[]={
    "BIOS boot specification is supported", /* 0 */
    "Function key-initiated network boot is supported",
    "Targeted content distribution is supported" /* 2 */
  };

  int i;
  catsprintf(_, NULL);
  for(i=0; i<=2; i++)
    if(code&(1<<i))
      catsprintf(_, "%s|", characteristics[i]);
  return _;
}

/*******************************************************************************
** 3.3.2 System Information (Type 1)
*/

const char *dmi_system_uuid(u8 *p, char *_) {
  int only0xFF=1, only0x00=1;
  int i;

  for(i=0; i<16 && (only0x00 || only0xFF); i++) {
    if(p[i]!=0x00) only0x00=0;
    if(p[i]!=0xFF) only0xFF=0;
  }

  if(only0xFF) {
    sprintf(_, "Not Present");
    return _;
  }

  if(only0x00) {
    sprintf(_, "Not Settable");
    return _;
  }

  sprintf(
    _, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
       p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]
  );

  return _;
}

/* 3.3.2.1 */
static const char *dmi_system_wake_up_type(u8 code) {
  static const char *type[]={
    "Reserved", /* 0x00 */
    "Other",
    "Unknown",
    "APM Timer",
    "Modem Ring",
    "LAN Remote",
    "Power Switch",
    "PCI PME#",
    "AC Power Restored" /* 0x08 */
  };

  if(code<=0x08) return type[code];
  return out_of_spec;
}

/*******************************************************************************
** 3.3.3 Base Board Information (Type 2)
*/

/* 3.3.3.1 */
static const char *dmi_base_board_features(u8 code, char *_) {
  static const char *features[] = {
    "Board is a hosting board", /* 0 */
    "Board requires at least one daughter board",
    "Board is removable",
    "Board is replaceable",
    "Board is hot swappable" /* 4 */
  };

  if((code&0x1F)==0) sprintf(_, "None");
  else {
    catsprintf(_, NULL);
    int i;
    for(i=0; i<=4; i++)
      if(code&(1<<i))
        catsprintf(_, "%s|", features[i]);
  }
  return _;
}

static const char *dmi_base_board_type(u8 code) {
  /* 3.3.3.2 */
  static const char *type[] = {
    "Unknown", /* 0x01 */
    "Other",
    "Server Blade",
    "Connectivity Switch",
    "System Management Module",
    "Processor Module",
    "I/O Module",
    "Memory Module",
    "Daughter Board",
    "Motherboard",
    "Processor+Memory Module",
    "Processor+I/O Module",
    "Interconnect Board" /* 0x0D */
  };

  if(code>=0x01 && code<=0x0D)
    return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_base_board_handles(u8 count, u8 *p, char *_) {
  int i;

  sprintf(_, "Contained Object Handles:%u", count);
  for(i=0; i<count; i++)
    catsprintf(_, ":0x%04X:", WORD(p+sizeof(u16)*i));
  return _;
}

/*******************************************************************************
** 3.3.4 Chassis Information (Type 3)
*/

/* 3.3.4.1 */
const char *dmi_chassis_type(u8 code) {
  static const char *type[] = {
    "Other", /* 0x01 */
    "Unknown",
    "Desktop",
    "Low Profile Desktop",
    "Pizza Box",
    "Mini Tower",
    "Tower",
    "Portable",
    "Laptop",
    "Notebook",
    "Hand Held",
    "Docking Station",
    "All In One",
    "Sub Notebook",
    "Space-saving",
    "Lunch Box",
    "Main Server Chassis", /* master.mif says System */
    "Expansion Chassis",
    "Sub Chassis",
    "Bus Expansion Chassis",
    "Peripheral Chassis",
    "RAID Chassis",
    "Rack Mount Chassis",
    "Sealed-case PC",
    "Multi-system",
    "CompactPCI",
    "AdvancedTCA" /* 0x1B */
  };

  if(code>=0x01 && code<=0x1B)
    return type[code-0x01];
  return out_of_spec;
}

PyObject *dmi_chassis_type_py(u8 code) {
  return PyString_FromString(dmi_chassis_type(code));
}

static PyObject *dmi_chassis_lock(u8 code) {
  static const char *lock[] = {
    "Not Present", /* 0x00 */
    "Present" /* 0x01 */
  };

  return PyString_FromString(lock[code]);
}

/* 3.3.4.2 */
PyObject *dmi_chassis_state(u8 code) {
  static const char *state[]={
    "Other", /* 0x01 */
    "Unknown",
    "Safe", /* master.mif says OK */
    "Warning",
    "Critical",
    "Non-recoverable" /* 0x06 */
  };

  if(code>=0x01 && code<=0x06)
    return PyString_FromString(state[code-0x01]);
  return PyString_FromString(out_of_spec);
}

/* 3.3.4.3 */
static const char *dmi_chassis_security_status(u8 code) {
  static const char *status[]={
    "Other", /* 0x01 */
    "Unknown",
    "None",
    "External Interface Locked Out",
    "External Interface Enabled" /* 0x05 */
  };

  if(code>=0x01 && code<=0x05)
    return(status[code-0x01]);
  return out_of_spec;
}

PyObject *dmi_chassis_height(u8 code) {
  if(code==0x00) return PyString_FromString("Unspecified");
  else return PyString_FromFormat("%u U", code);
}

PyObject *dmi_chassis_power_cords(u8 code) {
  if(code==0x00) return PyString_FromString("Unspecified");
  else return PyString_FromFormat("%u", code);
}

static const char *dmi_chassis_elements(u8 count, u8 len, u8 *p, char *_) {
  int i;

  sprintf(_, "Contained Elements:%u", count);
  for(i=0; i<count; i++) {
    if(len>=0x03) {
      catsprintf(_, "%s (",
        p[i*len]&0x80?
        dmi_smbios_structure_type(p[i*len]&0x7F):
        dmi_base_board_type(p[i*len]&0x7F));
      if(p[1+i*len]==p[2+i*len])
        catsprintf(_, "%u", p[1+i*len]);
      else
        catsprintf(_, "%u-%u", p[1+i*len], p[2+i*len]);
      catsprintf(_, ")");
    }
  }
  return _;
}

/*******************************************************************************
** 3.3.5 Processor Information (Type 4)
*/

static const char *dmi_processor_type(u8 code) {
  /* 3.3.5.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Central Processor",
    "Math Processor",
    "DSP Processor",
    "Video Processor" /* 0x06 */
  };

  if(code>=0x01 && code<=0x06) return type[code-0x01];
  return out_of_spec;
}

const char *dmi_processor_family(u8 code) {
  /* 3.3.5.2 */
  static const char *family[256] = {
    NULL, /* 0x00 */
    "Other",
    "Unknown",
    "8086",
    "80286",
    "80386",
    "80486",
    "8087",
    "80287",
    "80387",
    "80487",
    "Pentium",
    "Pentium Pro",
    "Pentium II",
    "Pentium MMX",
    "Celeron",
    "Pentium II Xeon",
    "Pentium III",
    "M1",
    "M2",
    NULL, /* 0x14 */
    NULL,
    NULL,
    NULL, /* 0x17 */
    "Duron",
    "K5",
    "K6",
    "K6-2",
    "K6-3",
    "Athlon",
    "AMD2900",
    "K6-2+",
    "Power PC",
    "Power PC 601",
    "Power PC 603",
    "Power PC 603+",
    "Power PC 604",
    "Power PC 620",
    "Power PC x704",
    "Power PC 750",
    NULL, /* 0x28 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,/* 0x2F */
    "Alpha",
    "Alpha 21064",
    "Alpha 21066",
    "Alpha 21164",
    "Alpha 21164PC",
    "Alpha 21164a",
    "Alpha 21264",
    "Alpha 21364",
    NULL, /* 0x38 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x3F */
    "MIPS",
    "MIPS R4000",
    "MIPS R4200",
    "MIPS R4400",
    "MIPS R4600",
    "MIPS R10000",
    NULL, /* 0x46 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x4F */
    "SPARC",
    "SuperSPARC",
    "MicroSPARC II",
    "MicroSPARC IIep",
    "UltraSPARC",
    "UltraSPARC II",
    "UltraSPARC IIi",
    "UltraSPARC III",
    "UltraSPARC IIIi",
    NULL, /* 0x59 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x5F */
    "68040",
    "68xxx",
    "68000",
    "68010",
    "68020",
    "68030",
    NULL, /* 0x66 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x6F */
    "Hobbit",
    NULL, /* 0x71 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x77 */
    "Crusoe TM5000",
    "Crusoe TM3000",
    "Efficeon TM8000",
    NULL, /* 0x7B */
    NULL,
    NULL,
    NULL,
    NULL, /* 0x7F */
    "Weitek",
    NULL, /* 0x81 */
    "Itanium",
    "Athlon 64",
    "Opteron",
    "Sempron",
    "Turion 64", 
    "Dual-Core Opteron",
    "Athlon 64 X2",
    NULL, /* 0x89 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x8F */
    "PA-RISC",
    "PA-RISC 8500",
    "PA-RISC 8000",
    "PA-RISC 7300LC",
    "PA-RISC 7200",
    "PA-RISC 7100LC",
    "PA-RISC 7100",
    NULL, /* 0x97 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0x9F */
    "V30",
    NULL, /* 0xA1 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0xAF */
    "Pentium III Xeon",
    "Pentium III Speedstep",
    "Pentium 4",
    "Xeon",
    "AS400",
    "Xeon MP",
    "Athlon XP",
    "Athlon MP",
    "Itanium 2",
    "Pentium M",
    "Celeron D",
    "Pentium D",
    "Pentium EE",
    NULL, /* 0xBD */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0xC7 */
    "IBM390",
    "G4",
    "G5",
    NULL, /* 0xCB */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* 0xF9 */
    "i860",
    "i960",
    NULL, /* 0xFC */
    NULL,
    NULL,
    NULL /* 0xFF */
    /* master.mif has values beyond that, but they can't be used for DMI */
  };

  if(family[code]!=NULL) return family[code];
  return out_of_spec;
}

static const char *dmi_processor_id(u8 type, u8 *p, const char *version, char *_) {
  /* Intel AP-485 revision 31, table 3-4 */
  static const char *flags[32]={
    "FPU (Floating-point unit on-chip)", /* 0 */
    "VME (Virtual mode extension)",
    "DE (Debugging extension)",
    "PSE (Page size extension)",
    "TSC (Time stamp counter)",
    "MSR (Model specific registers)",
    "PAE (Physical address extension)",
    "MCE (Machine check exception)",
    "CX8 (CMPXCHG8 instruction supported)",
    "APIC (On-chip APIC hardware supported)",
    NULL, /* 10 */
    "SEP (Fast system call)",
    "MTRR (Memory type range registers)",
    "PGE (Page global enable)",
    "MCA (Machine check architecture)",
    "CMOV (Conditional move instruction supported)",
    "PAT (Page attribute table)",
    "PSE-36 (36-bit page size extension)",
    "PSN (Processor serial number present and enabled)",
    "CLFSH (CLFLUSH instruction supported)",
    NULL, /* 20 */
    "DS (Debug store)",
    "ACPI (ACPI supported)",
    "MMX (MMX technology supported)",
    "FXSR (Fast floating-point save and restore)",
    "SSE (Streaming SIMD extensions)",
    "SSE2 (Streaming SIMD extensions 2)",
    "SS (Self-snoop)",
    "HTT (Hyper-threading technology)",
    "TM (Thermal monitor supported)",
    "IA64 (IA64 capabilities)",
    "PBE (Pending break enabled)" /* 31 */
  };
  /*
  ** Extra flags are now returned in the ECX register when one calls
  ** the CPUID instruction. Their meaning is explained in table 3-5, but
  ** DMI doesn't support this yet.
  */
  u32 eax, edx;
  int sig=0;

  /*
  ** This might help learn about new processors supporting the
  ** CPUID instruction or another form of identification.
  */
  sprintf(_, "ID: %02X %02X %02X %02X %02X %02X %02X %02X",
    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

  if(type==0x05) /* 80386 */ {
    u16 dx=WORD(p);
    /*
    ** 80386 have a different signature.
    */
    catsprintf(_, "Signature: Type %u, Family %u, Major Stepping %u, Minor Stepping %u",
      dx>>12, (dx>>8)&0xF, (dx>>4)&0xF, dx&0xF);
    return _;
  }

  if(type==0x06) /* 80486 */ {
    u16 dx=WORD(p);
    /*
    ** Not all 80486 CPU support the CPUID instruction, we have to find
    ** wether the one we have here does or not. Note that this trick
    ** works only because we know that 80486 must be little-endian.
    */
    if((dx&0x0F00)==0x0400
    &&((dx&0x00F0)==0x0040 || (dx&0x00F0)>=0x0070)
    &&((dx&0x000F)>=0x0003)) sig=1;
    else {
      catsprintf(_, "Signature: Type %u, Family %u, Model %u, Stepping %u",
        (dx>>12)&0x3, (dx>>8)&0xF, (dx>>4)&0xF, dx&0xF);
      return _;
    }
  } else if((type>=0x0B && type<=0x13) /* Intel, Cyrix */
    || (type>=0xB0 && type<=0xB3) /* Intel */
    || type==0xB5 /* Intel */
    || (type>=0xB9 && type<=0xBC)) /* Intel */
    sig=1;
  else if((type>=0x18 && type<=0x1D) /* AMD */
    || type==0x1F /* AMD */
    || (type>=0xB6 && type<=0xB7) /* AMD */
    || (type>=0x83 && type<=0x88)) /* AMD */
    sig=2;
  else if(type==0x01 || type==0x02) {
    /*
    ** Some X86-class CPU have family "Other" or "Unknown". In this case,
    ** we use the version string to determine if they are known to
    ** support the CPUID instruction.
    */
    if(strncmp(version, "Pentium III MMX", 15)==0) sig=1;
    else if(strncmp(version, "AMD Athlon(TM)", 14)==0 || strncmp(version, "AMD Opteron(tm)", 15)==0) sig=2;
    else return _;
  } else /* not X86-class */ return _;

  eax=DWORD(p);
  edx=DWORD(p+4);
  switch(sig) {
    case 1: /* Intel */
      catsprintf(_, "Signature: Type %u, Family %u, Model %u, Stepping %u",
        (eax>>12)&0x3, ((eax>>20)&0xFF)+((eax>>8)&0x0F),
        ((eax>>12)&0xF0)+((eax>>4)&0x0F), eax&0xF);
      break;
    case 2: /* AMD */
      catsprintf(_, "Signature: Family %u, Model %u, Stepping %u",
        ((eax>>8)&0xF)+(((eax>>8)&0xF)==0xF?(eax>>20)&0xFF:0),
        ((eax>>4)&0xF)|(((eax>>8)&0xF)==0xF?(eax>>12)&0xF0:0),
        eax&0xF);
      break;
  }

  edx=DWORD(p+4);
  catsprintf(_, "Flags:");
  if((edx&0xFFEFFBFF)==0) catsprintf(_, "None");
  else {
    int i;

    for(i=0; i<=31; i++)
      if(flags[i]!=NULL && edx&(1<<i))
        catsprintf(_, "%s", flags[i]);
  }
  return _;
}

/* 3.3.5.4 */
static const char *dmi_processor_voltage(u8 code, char *_) {
  static const char *voltage[]={
    "5.0 V", /* 0 */
    "3.3 V",
    "2.9 V" /* 2 */
  };
  int i;

  if(code&0x80) sprintf(buffer, "%.1f V", (float)(code&0x7f)/10);
  else {
    catsprintf(_, NULL);
    for(i=0; i<=2; i++)
      if(code&(1<<i))
        catsprintf(_, "%s|", voltage[i]);
    if(code==0x00)
      catsprintf(_, "Unknown");
  }
  return _;
}

const char *dmi_processor_frequency(u8 *p, char *_) {
  u16 code = WORD(p);

  if(code) catsprintf(_, "%u MHz", code);
  else catsprintf(_, "Unknown");
  return _;
}

static const char *dmi_processor_status(u8 code) {
  static const char *status[] = {
    "Unknown", /* 0x00 */
    "Enabled",
    "Disabled By User",
    "Disabled By BIOS",
    "Idle", /* 0x04 */
    "Other" /* 0x07 */
  };

  if(code<=0x04) return status[code];
  if(code==0x07) return status[0x05];
  return out_of_spec;
}

static const char *dmi_processor_upgrade(u8 code) {
  /* 3.3.5.5 */
  static const char *upgrade[]={
    "Other", /* 0x01 */
    "Unknown",
    "Daughter Board",
    "ZIF Socket",
    "Replaceable Piggy Back",
    "None",
    "LIF Socket",
    "Slot 1",
    "Slot 2",
    "370-pin Socket",
    "Slot A",
    "Slot M",
    "Socket 423",
    "Socket A (Socket 462)",
    "Socket 478",
    "Socket 754",
    "Socket 940",
    "Socket 939",
       "Socket mPGA604",
    "Socket LGA771",
    "Socket LGA775" /* 0x15 */
  };

  if(code>=0x01 && code<=0x15) return upgrade[code-0x01];
  return out_of_spec;
}

static const char *dmi_processor_cache(u16 code, const char *level, u16 ver, char *_) {
  if(code==0xFFFF) {
    if(ver>=0x0203) sprintf(_, "Not Provided");
    else sprintf(_, "No %s Cache", level);
  } else catsprintf(_, "0x%04X", code);
  return _;
}

/* 3.3.5.9 */
static const char *dmi_processor_characteristics(u16 code, char *_) {
  static const char *characteristics[]={
    "64-bit capable" /* 2 */
  };

  if((code&0x0004)==0) sprintf(_, "None");
  else {
    int i;
    catsprintf(_, NULL);
    for(i=2; i<=2; i++)
      if(code&(1<<i))
        catsprintf(_, characteristics[i-2]);
  }
  return _;
}

/*******************************************************************************
** 3.3.6 Memory Controller Information (Type 5)
*/

static const char *dmi_memory_controller_ed_method(u8 code) {
  /* 3.3.6.1 */
  static const char *method[]={
    "Other", /* 0x01 */
    "Unknown",
    "None",
    "8-bit Parity",
    "32-bit ECC",
    "64-bit ECC",
    "128-bit ECC",
    "CRC" /* 0x08 */
  };

  if(code>=0x01 && code<=0x08) return(method[code-0x01]);
  return out_of_spec;
}

/* 3.3.6.2 */
static const char *dmi_memory_controller_ec_capabilities(u8 code, char *_) {
  static const char *capabilities[]={
    "Other", /* 0 */
    "Unknown",
    "None",
    "Single-bit Error Correcting",
    "Double-bit Error Correcting",
    "Error Scrubbing" /* 5 */
  };

  if((code&0x3F)==0) sprintf(_, "None");
  else {
    int i;

    catsprintf(_, NULL);
    for(i=0; i<=5; i++)
      if(code&(1<<i))
        catsprintf(_, capabilities[i]);
  }
  return _;
}

static const char* dmi_memory_controller_interleave(u8 code) {
  /* 3.3.6.3 */
  static const char *interleave[]={
    "Other", /* 0x01 */
    "Unknown",
    "One-way Interleave",
    "Two-way Interleave",
    "Four-way Interleave",
    "Eight-way Interleave",
    "Sixteen-way Interleave" /* 0x07 */
  };

  if(code>=0x01 && code<=0x07) return(interleave[code-0x01]);
  return(out_of_spec);
}

/* 3.3.6.4 */
static const char *dmi_memory_controller_speeds(u16 code, char *_) {
  const char *speeds[]={
    "Other", /* 0 */
    "Unknown",
    "70 ns",
    "60 ns",
    "50 ns" /* 4 */
  };

  if((code&0x001F)==0) sprintf(_, "None");
  else {
    int i;

    catsprintf(_, NULL);
    for(i=0; i<=4; i++)
      if(code&(1<<i))
        catsprintf(_, speeds[i]);
  }
  return _;
}

static const char *dmi_memory_controller_slots(u8 count, u8 *p, char *_) {
  int i;

  sprintf(_, "Associated Memory Slots: %u", count);
  for(i=0; i<count; i++)
    catsprintf(_, ":0x%04X:", WORD(p+sizeof(u16)*i));
  return _;
}

/*******************************************************************************
** 3.3.7 Memory Module Information (Type 6)
*/

/* 3.3.7.1 */
static const char *dmi_memory_module_types(u16 code, char *_) {
  static const char *types[]={
    "Other", /* 0 */
    "Unknown",
    "Standard",
    "FPM",
    "EDO",
    "Parity",
    "ECC",
    "SIMM",
    "DIMM",
    "Burst EDO",
    "SDRAM" /* 10 */
  };

  if((code&0x07FF)==0) sprintf(_, "None");
  else {
    int i;

    catsprintf(_, NULL);
    for(i=0; i<=10; i++)
      if(code&(1<<i))
        catsprintf(_, "%s|", types[i]);
  }
  return _;
}

static const char *dmi_memory_module_connections(u8 code, char *_) {
  if(code==0xFF) sprintf(_, "None");
  else {
    catsprintf(_, NULL);
    if((code&0xF0)!=0xF0) catsprintf(_, ":%u:", code>>4);
    if((code&0x0F)!=0x0F) catsprintf(_, ":%u:", code&0x0F);
  }
  return _;
}

static const char *dmi_memory_module_speed(u8 code, char *_) {
  if(code==0) sprintf(_, "Unknown");
  else sprintf(_, "%u ns", code);
  return _;
}

/* 3.3.7.2 */
static const char *dmi_memory_module_size(u8 code, char *_) {
  catsprintf(_, NULL);
  switch(code&0x7F) {
    case 0x7D:
      catsprintf(_, "Not Determinable");
      break;
    case 0x7E:
      catsprintf(_, "Disabled");
      break;
    case 0x7F:
      catsprintf(_, "Not Installed");
      return _;
    default:
      catsprintf(_, "%u MB", 1<<(code&0x7F));
  }

  if(code&0x80) catsprintf(_, "(Double-bank Connection)");
  else catsprintf(_, "(Single-bank Connection)");
  return _;
}

static const char *dmi_memory_module_error(u8 code, char *_) {
  if(code&(1<<2)) sprintf(_, "See Event Log");
  else {
    catsprintf(_, NULL);
    if((code&0x03)==0) catsprintf(_, "OK");
    if(code&(1<<0)) catsprintf(_, "Uncorrectable Errors");
    if(code&(1<<1)) catsprintf(_, "Correctable Errors");
  }
  return _;
}

/*******************************************************************************
** 3.3.8 Cache Information (Type 7)
*/
static const char *dmi_cache_mode(u8 code) {
  static const char *mode[]={
    "Write Through", /* 0x00 */
    "Write Back",
    "Varies With Memory Address",
    "Unknown" /* 0x03 */
  };

  return mode[code];
}

static const char *dmi_cache_location(u8 code) {
  static const char *location[4]={
    "Internal", /* 0x00 */
    "External",
    NULL, /* 0x02 */
    "Unknown" /* 0x03 */
  };

  if(location[code]!=NULL) return location[code];
  return out_of_spec;
}

static const char *dmi_cache_size(u16 code, char *_) {
  if(code&0x8000) sprintf(_, "%u KB", (code&0x7FFF)<<6);
  else sprintf(_, "%u KB", code);
  return _;
}

/* 3.3.8.2 */
static const char *dmi_cache_types(u16 code, char *_) {
  static const char *types[] = {
    "Other", /* 0 */
    "Unknown",
    "Non-burst",
    "Burst",
    "Pipeline Burst",
    "Synchronous",
    "Asynchronous" /* 6 */
  };

  if((code&0x007F)==0) sprintf(_, "None");
  else {
    int i;

    for(i=0; i<=6; i++)
      if(code&(1<<i))
        catsprintf(_, "%s|", types[i]);
  }
  return _;
}

static const char *dmi_cache_ec_type(u8 code) {
  /* 3.3.8.3 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "None",
    "Parity",
    "Single-bit ECC",
    "Multi-bit ECC" /* 0x06 */
  };

  if(code>=0x01 && code<=0x06) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_cache_type(u8 code) {
  /* 3.3.8.4 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Instruction",
    "Data",
    "Unified" /* 0x05 */
  };

  if(code>=0x01 && code<=0x05) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_cache_associativity(u8 code) {
  /* 3.3.8.5 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Direct Mapped",
    "2-way Set-associative",
    "4-way Set-associative",
    "Fully Associative",
    "8-way Set-associative",
    "16-way Set-associative" /* 0x08 */
  };

  if(code>=0x01 && code<=0x08) return type[code-0x01];
  return out_of_spec;
}

/*******************************************************************************
** 3.3.9 Port Connector Information (Type 8)
*/

static const char *dmi_port_connector_type(u8 code) {
  /* 3.3.9.2 */
  static const char *type[] = {
    "None", /* 0x00 */
    "Centronics",
    "Mini Centronics",
    "Proprietary",
    "DB-25 male",
    "DB-25 female",
    "DB-15 male",
    "DB-15 female",
    "DB-9 male",
    "DB-9 female",
    "RJ-11",
    "RJ-45",
    "50 Pin MiniSCSI",
    "Mini DIN",
    "Micro DIN",
    "PS/2",
    "Infrared",
    "HP-HIL",
    "Access Bus (USB)",
    "SSA SCSI",
    "Circular DIN-8 male",
    "Circular DIN-8 female",
    "On Board IDE",
    "On Board Floppy",
    "9 Pin Dual Inline (pin 10 cut)",
    "25 Pin Dual Inline (pin 26 cut)",
    "50 Pin Dual Inline",
    "68 Pin Dual Inline",
    "On Board Sound Input From CD-ROM",
    "Mini Centronics Type-14",
    "Mini Centronics Type-26",
    "Mini Jack (headphones)",
    "BNC",
    "IEEE 1394",
       "SAS/SATA Plug Receptacle" /* 0x22 */
  };
  static const char *type_0xA0[]={
    "PC-98", /* 0xA0 */
    "PC-98 Hireso",
    "PC-H98",
    "PC-98 Note",
    "PC-98 Full" /* 0xA4 */
  };

  if(code<=0x22) return type[code];
  if(code>=0xA0 && code<=0xA4) return type_0xA0[code-0xA0];
  if(code==0xFF) return "Other";
  return out_of_spec;
}

static const char *dmi_port_type(u8 code) {
  /* 3.3.9.3 */
  static const char *type[] = {
    "None", /* 0x00 */
    "Parallel Port XT/AT Compatible",
    "Parallel Port PS/2",
    "Parallel Port ECP",
    "Parallel Port EPP",
    "Parallel Port ECP/EPP",
    "Serial Port XT/AT Compatible",
    "Serial Port 16450 Compatible",
    "Serial Port 16550 Compatible",
    "Serial Port 16550A Compatible",
    "SCSI Port",
    "MIDI Port",
    "Joystick Port",
    "Keyboard Port",
    "Mouse Port",
    "SSA SCSI",
    "USB",
    "Firewire (IEEE P1394)",
    "PCMCIA Type I",
    "PCMCIA Type II",
    "PCMCIA Type III",
    "Cardbus",
    "Access Bus Port",
    "SCSI II",
    "SCSI Wide",
    "PC-98",
    "PC-98 Hireso",
    "PC-H98",
    "Video Port",
    "Audio Port",
    "Modem Port",
    "Network Port",
       "SATA",
    "SAS" /* 0x21 */
  };
  static const char *type_0xA0[]={
    "8251 Compatible", /* 0xA0 */
    "8251 FIFO Compatible" /* 0xA1 */
  };

  if(code<=0x21) return type[code];
  if(code>=0xA0 && code<=0xA1) return type_0xA0[code-0xA0];
  if(code==0xFF) return "Other";
  return out_of_spec;
}

/*******************************************************************************
** 3.3.10 System Slots (Type 9)
*/

static const char *dmi_slot_type(u8 code) {
  /* 3.3.10.1 */
  static const char *type[] = {
    "Other", /* 0x01 */
    "Unknown",
    "ISA",
    "MCA",
    "EISA",
    "PCI",
    "PC Card (PCMCIA)",
    "VLB",
    "Proprietary",
    "Processor Card",
    "Proprietary Memory Card",
    "I/O Riser Card",
    "NuBus",
    "PCI-66",
    "AGP",
    "AGP 2x",
    "AGP 4x",
    "PCI-X",
    "AGP 8x" /* 0x13 */
  };
  static const char *type_0xA0[]={
    "PC-98/C20", /* 0xA0 */
    "PC-98/C24",
    "PC-98/E",
    "PC-98/Local Bus",
    "PC-98/Card",
    "PCI Express" /* 0xA5 */
  };

  if(code>=0x01 && code<=0x13) return type[code-0x01];
  if(code>=0xA0 && code<=0xA5) return type_0xA0[code-0xA0];
  return out_of_spec;
}

static const char *dmi_slot_bus_width(u8 code) {
  /* 3.3.10.2 */
  static const char *width[]={
    "", /* 0x01, "Other" */
    "", /* "Unknown" */
    "8-bit ",
    "16-bit ",
    "32-bit ",
    "64-bit ",
    "128-bit ",
    "x1 ",
    "x2 ",
    "x4 ",
    "x8 ",
    "x12 ",
    "x16 ",
    "x32 " /* 0x0E */
  };

  if(code>=0x01 && code<=0x0E) return width[code-0x01];
  return out_of_spec;
}

static const char *dmi_slot_current_usage(u8 code) {
  /* 3.3.10.3 */
  static const char *usage[]={
    "Other", /* 0x01 */
    "Unknown",
    "Available",
    "In Use" /* 0x04 */
  };

  if(code>=0x01 && code<=0x04) return usage[code-0x01];
  return out_of_spec;
}

/* 3.3.1O.4 */
static const char *dmi_slot_length(u8 code) {
  static const char *length[]={
    "Other", /* 0x01 */
    "Unknown",
    "Short",
    "Long" /* 0x04 */
  };

  if(code>=0x01 && code<=0x04)
    return length[code-0x01];
  return out_of_spec;
}

/* 3.3.10.5 */
static const char *dmi_slot_id(u8 code1, u8 code2, u8 type, char *_) {
  catsprintf(_, NULL);
  switch(type) {
    case 0x04: /* MCA */
      catsprintf(_, "ID: %u\n", code1);
      break;
    case 0x05: /* EISA */
      catsprintf(_, "ID: %u\n", code1);
      break;
    case 0x06: /* PCI */
    case 0x0E: /* PCI */
    case 0x0F: /* AGP */
    case 0x10: /* AGP */
    case 0x11: /* AGP */
    case 0x12: /* PCI-X */
    case 0x13: /* AGP */
    case 0xA5: /* PCI Express */
      catsprintf(_, "ID: %u\n", code1);
      break;
    case 0x07: /* PCMCIA */
      catsprintf(_, "ID: Adapter %u, Socket %u\n", code1, code2);
      break;
  }
  return _;
}

static const char *dmi_slot_characteristics(u8 code1, u8 code2, char *_) {
  /* 3.3.10.6 */
  static const char *characteristics1[]={
    "5.0 V is provided", /* 1 */
    "3.3 V is provided",
    "Opening is shared",
    "PC Card-16 is supported",
    "Cardbus is supported",
    "Zoom Video is supported",
    "Modem ring resume is supported" /* 7 */
  };

  /* 3.3.10.7 */
  static const char *characteristics2[]={
    "PME signal is supported", /* 0 */
    "Hot-plug devices are supported",
    "SMBus signal is supported" /* 2 */
  };

  if(code1&(1<<0)) sprintf(_, "Unknown");
  else if((code1&0xFE)==0 && (code2&0x07)==0) sprintf(_, "None");
  else {
    int i;

    catsprintf(_, NULL);
    for(i=1; i<=7; i++)
      if(code1&(1<<i))
        catsprintf(_, "%s|", characteristics1[i-1]);
    for(i=0; i<=2; i++)
      if(code2&(1<<i))
        catsprintf(_, "%s|", characteristics2[i]);
  }
  return _;
}

/*******************************************************************************
** 3.3.11 On Board Devices Information (Type 10)
*/

static const char *dmi_on_board_devices_type(u8 code) {
  /* 3.3.11.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Video",
    "SCSI Controller",
    "Ethernet",
    "Token Ring",
    "Sound",
    "PATA Controller",
    "SATA Controller",
    "SAS Controller" /* 0x0A */
  };

  if(code>=0x01 && code<=0x0A) return type[code-0x01];
  return out_of_spec;
}

static PyObject *dmi_on_board_devices(struct dmi_header *h) {
  PyObject *data = NULL;
  u8 *p = h->data+4;
  u8 count = (h->length-0x04)/2;
  int i;

  if((data = PyList_New(count))) {
    PyObject *_pydict;
    PyObject *_val;
    for(i=0; i<count; i++) {
      _pydict = PyDict_New();

      _val = PyString_FromString(dmi_on_board_devices_type(p[2*i]&0x7F));
      PyDict_SetItemString(_pydict, "Type", _val);
      Py_DECREF(_val);

      _val = p[2*i]&0x80?Py_True:Py_False;
      PyDict_SetItemString(_pydict, "Enabled", _val);
      Py_DECREF(_val);

      _val = PyString_FromString(dmi_string(h, p[2*i+1]));
      PyDict_SetItemString(_pydict, "Description", _val);
      Py_DECREF(_val);

      PyList_SET_ITEM(data, i, _pydict);
    }
  }

  return data;
}
/*******************************************************************************
 * 3.3.12 OEM Strings (Type 11)
 */

static const char *dmi_oem_strings(struct dmi_header *h, char *_) {
  u8 *p=h->data+4;
  u8 count=p[0x00];
  int i;

  catsprintf(_, NULL);
  for(i=1; i<=count; i++)
    catsprintf(_, "String %d: %s|", i, dmi_string(h, i));
  return _;
}

/*******************************************************************************
** 3.3.13 System Configuration Options (Type 12)
*/

static const char *dmi_system_configuration_options(struct dmi_header *h, char *_) {
  u8 *p=h->data+4;
  u8 count=p[0x00];
  int i;

  catsprintf(_, NULL);
  for(i=1; i<=count; i++)
    catsprintf(_, "Option %d: %s|", i, dmi_string(h, i));
  return _;
}

/*******************************************************************************
** 3.3.14 BIOS Language Information (Type 13)
*/

static const char *dmi_bios_languages(struct dmi_header *h, char *_) {
  u8 *p=h->data+4;
  u8 count=p[0x00];
  int i;

  for(i=1; i<=count; i++)
    catsprintf(_, "%s|", dmi_string(h, i));
  return _;
}

/*******************************************************************************
** 3.3.15 Group Associations (Type 14)
*/

static const char *dmi_group_associations_items(u8 count, u8 *p, char *_) {
  int i;

  for(i=0; i<count; i++) {
    catsprintf(_, "0x%04X (%s)|",
      WORD(p+3*i+1),
      dmi_smbios_structure_type(p[3*i]));
  }
  return _;
}

/*******************************************************************************
** 3.3.16 System Event Log (Type 15)
*/

static const char *dmi_event_log_method(u8 code) {
  static const char *method[]={
    "Indexed I/O, one 8-bit index port, one 8-bit data port", /* 0x00 */
    "Indexed I/O, two 8-bit index ports, one 8-bit data port",
    "Indexed I/O, one 16-bit index port, one 8-bit data port",
    "Memory-mapped physical 32-bit address",
    "General-purpose non-volatile data functions" /* 0x04 */
  };

  if(code<=0x04) return method[code];
  if(code>=0x80) return "OEM-specific";
  return out_of_spec;
}

static const char *dmi_event_log_status(u8 code, char *_) {
  static const char *valid[]={
    "Invalid", /* 0 */
    "Valid" /* 1 */
  };
  static const char *full[]={
    "Not Full", /* 0 */
    "Full" /* 1 */
  };

  sprintf(_, " %s, %s", valid[(code>>0)&1], full[(code>>1)&1]);
  return _;
}

static const char *dmi_event_log_address(u8 method, u8 *p, char *_) {
  /* 3.3.16.3 */
  switch(method) {
    case 0x00:
    case 0x01:
    case 0x02:
      catsprintf(_, " Index 0x%04X, Data 0x%04X", WORD(p), WORD(p+2));
      break;
    case 0x03:
      catsprintf(_, " 0x%08X", DWORD(p));
      break;
    case 0x04:
      catsprintf(_, " 0x%04X", WORD(p));
      break;
    default:
      catsprintf(_, " Unknown");
  }
  return _;
}

static const char *dmi_event_log_header_type(u8 code) {
  static const char *type[]={
    "No Header", /* 0x00 */
    "Type 1" /* 0x01 */
  };

  if(code<=0x01) return type[code];
  if(code>=0x80) return "OEM-specific";
  return out_of_spec;
}

static const char *dmi_event_log_descriptor_type(u8 code) {
  /* 3.3.16.6.1 */
  static const char *type[]={
    NULL, /* 0x00 */
    "Single-bit ECC memory error",
    "Multi-bit ECC memory error",
    "Parity memory error",
    "Bus timeout",
    "I/O channel block",
    "Software NMI",
    "POST memory resize",
    "POST error",
    "PCI parity error",
    "PCI system error",
    "CPU failure",
    "EISA failsafe timer timeout",
    "Correctable memory log disabled",
    "Logging disabled",
    NULL, /* 0x0F */
    "System limit exceeded",
    "Asynchronous hardware timer expired",
    "System configuration information",
    "Hard disk information",
    "System reconfigured",
    "Uncorrectable CPU-complex error",
    "Log area reset/cleared",
    "System boot" /* 0x17 */
  };

  if(code<=0x17 && type[code]!=NULL) return type[code];
  if(code>=0x80 && code<=0xFE) return "OEM-specific";
  if(code==0xFF) return "End of log";
  return out_of_spec;
}

static const char *dmi_event_log_descriptor_format(u8 code) {
  /* 3.3.16.6.2 */
  static const char *format[]={
    "None", /* 0x00 */
    "Handle",
    "Multiple-event",
    "Multiple-event handle",
    "POST results bitmap",
    "System management",
    "Multiple-event system management" /* 0x06 */
  };

  if(code<=0x06)
    return format[code];
  if(code>=0x80)
    return "OEM-specific";
  return out_of_spec;
}

static const char *dmi_event_log_descriptors(u8 count, u8 len, u8 *p, char *_) {
  /* 3.3.16.1 */
  int i;

  catsprintf(_, NULL);
  for(i=0; i<count; i++) {
    if(len>=0x02) {
      catsprintf(_, "Descriptor %u: %s\n",
        i+1, dmi_event_log_descriptor_type(p[i*len]));
      catsprintf(_, "Data Format %u: %s\n",
        i+1, dmi_event_log_descriptor_format(p[i*len+1]));
    }
  }
  return _;
}

/*******************************************************************************
** 3.3.17 Physical Memory Array (Type 16)
*/

static const char *dmi_memory_array_location(u8 code) {
  /* 3.3.17.1 */
  static const char *location[]={
    "Other", /* 0x01 */
    "Unknown",
    "System Board Or Motherboard",
    "ISA Add-on Card",
    "EISA Add-on Card",
    "PCI Add-on Card",
    "MCA Add-on Card",
    "PCMCIA Add-on Card",
    "Proprietary Add-on Card",
    "NuBus" /* 0x0A, master.mif says 16 */
  };
  static const char *location_0xA0[]={
    "PC-98/C20 Add-on Card", /* 0xA0 */
    "PC-98/C24 Add-on Card",
    "PC-98/E Add-on Card",
    "PC-98/Local Bus Add-on Card",
    "PC-98/Card Slot Add-on Card" /* 0xA4, from master.mif */
  };

  if(code>=0x01 && code<=0x0A) return location[code-0x01];
  if(code>=0xA0 && code<=0xA4) return location_0xA0[code-0xA0];
  return out_of_spec;
}

static const char *dmi_memory_array_use(u8 code) {
  /* 3.3.17.2 */
  static const char *use[]={
    "Other", /* 0x01 */
    "Unknown",
    "System Memory",
    "Video Memory",
    "Flash Memory",
    "Non-volatile RAM",
    "Cache Memory" /* 0x07 */
  };

  if(code>=0x01 && code<=0x07) return use[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_array_ec_type(u8 code) {
  /* 3.3.17.3 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "None",
    "Parity",
    "Single-bit ECC",
    "Multi-bit ECC",
    "CRC" /* 0x07 */
  };

  if(code>=0x01 && code<=0x07) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_array_capacity(u32 code, char *_) {
  if(code==0x8000000) sprintf(_, " Unknown");
  else {
    catsprintf(_, NULL);
    if((code&0x000FFFFF)==0) catsprintf(_, " %u GB", code>>20);
    else if((code&0x000003FF)==0) catsprintf(_, " %u MB", code>>10);
    else catsprintf(_, " %u kB", code);
  }
  return _;
}

static const char *dmi_memory_array_error_handle(u16 code, char *_) {
  if(code==0xFFFE) catsprintf(_, "Not Provided");
  else if(code==0xFFFF) catsprintf(_, "No Error");
  else catsprintf(_, " 0x%04X", code);
  return _;
}

/*******************************************************************************
** 3.3.18 Memory Device (Type 17)
*/

static const char *dmi_memory_device_width(u16 code, char *_) {
  /*
  ** If no memory module is present, width may be 0
  */
  if(code==0xFFFF || code==0) sprintf(_, "Unknown");
  else sprintf(_, "%u bits", code);
  return _;
}

static const char *dmi_memory_device_size(u16 code, char *_) {
  if(code==0) sprintf(_, " No Module Installed");
  else if(code==0xFFFF) sprintf(_, " Unknown");
  else {
    if(code&0x8000) sprintf(_, " %u kB", code&0x7FFF);
    else sprintf(_, " %u MB", code);
  }
  return _;
}

static const char *dmi_memory_device_form_factor(u8 code) {
  /* 3.3.18.1 */
  static const char *form_factor[]={
    "Other", /* 0x01 */
    "Unknown",
    "SIMM",
    "SIP",
    "Chip",
    "DIP",
    "ZIP",
    "Proprietary Card",
    "DIMM",
    "TSOP",
    "Row Of Chips",
    "RIMM",
    "SODIMM",
    "SRIMM",
    "FB-DIMM" /* 0x0F */
  };

  if(code>=0x01 && code<=0x0F) return form_factor[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_device_set(u8 code, char *_) {
  if(code==0) catsprintf(_, "None");
  else if(code==0xFF) catsprintf(_, "Unknown");
  else catsprintf(_, "%u", code);
  return _;
}

static const char *dmi_memory_device_type(u8 code) {
  /* 3.3.18.2 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "DRAM",
    "EDRAM",
    "VRAM",
    "SRAM",
    "RAM",
    "ROM",
    "Flash",
    "EEPROM",
    "FEPROM",
    "EPROM",
    "CDRAM",
    "3DRAM",
    "SDRAM",
    "SGRAM",
    "RDRAM",
    "DDR",
    "DDR2",
    "DDR2 FB-DIMM" /* 0x14 */
  };

  if(code>=0x01 && code<=0x14) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_device_type_detail(u16 code, char *_) {
  /* 3.3.18.3 */
  static const char *detail[]={
    "Other", /* 1 */
    "Unknown",
    "Fast-paged",
    "Static Column",
    "Pseudo-static",
    "RAMBus",
    "Synchronous",
    "CMOS",
    "EDO",
    "Window DRAM",
    "Cache DRAM",
    "Non-Volatile" /* 12 */
  };

  if((code&0x1FFE)==0) sprintf(_, "None");
  else {
    int i;

    catsprintf(_, NULL);
    for(i=1; i<=12; i++)
      if(code&(1<<i))
        catsprintf(_, " %s", detail[i-1]);
  }
  return _;
}

static const char *dmi_memory_device_speed(u16 code, char *_) {
  if(code==0) sprintf(_, "Unknown");
  else sprintf(_, "%u MHz (%.1f ns)", code, (float)1000/code);
  return _;
}

/*******************************************************************************
* 3.3.19 32-bit Memory Error Information (Type 18)
*/

static const char *dmi_memory_error_type(u8 code) {
  /* 3.3.19.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "OK",
    "Bad Read",
    "Parity Error",
    "Single-bit Error",
    "Double-bit Error",
    "Multi-bit Error",
    "Nibble Error",
    "Checksum Error",
    "CRC Error",
    "Corrected Single-bit Error",
    "Corrected Error",
    "Uncorrectable Error" /* 0x0E */
  };

  if(code>=0x01 && code<=0x0E)
    return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_error_granularity(u8 code) {
  /* 3.3.19.2 */
  static const char *granularity[]={
    "Other", /* 0x01 */
    "Unknown",
    "Device Level",
    "Memory Partition Level" /* 0x04 */
  };

  if(code>=0x01 && code<=0x04)
    return granularity[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_error_operation(u8 code) {
  /* 3.3.19.3 */
  static const char *operation[]={
    "Other", /* 0x01 */
    "Unknown",
    "Read",
    "Write",
    "Partial Write" /* 0x05 */
  };

  if(code>=0x01 && code<=0x05)
    return operation[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_error_syndrome(u32 code, char *_) {
  if(code==0x00000000) sprintf(_, " Unknown");
  else sprintf(_, " 0x%08X", code);
  return _;
}

static const char *dmi_32bit_memory_error_address(u32 code, char *_) {
  if(code==0x80000000) sprintf(_, " Unknown");
  else sprintf(_, " 0x%08X", code);
  return _;
}

/*******************************************************************************
** 3.3.20 Memory Array Mapped Address (Type 19)
*/

static const char *dmi_mapped_address_size(u32 code, char *_) {
  if(code==0)
    catsprintf(_, " Invalid");
  else if((code&0x000FFFFF)==0)
    catsprintf(_, " %u GB", code>>20);
  else if((code&0x000003FF)==0)
    catsprintf(_, " %u MB", code>>10);
  else
    catsprintf(_, " %u kB", code);
  return _;
}

/*******************************************************************************
** 3.3.21 Memory Device Mapped Address (Type 20)
*/

static const char *dmi_mapped_address_row_position(u8 code, char *_) {
  if(code==0) sprintf(_, " %s", out_of_spec);
  else if(code==0xFF) sprintf(_, " Unknown");
  else sprintf(_, " %u", code);
  return _;
}

static const char *dmi_mapped_address_interleave_position(u8 code, char *_) {
  catsprintf(_, NULL);
  if(code!=0) {
    catsprintf(_, "Interleave Position");
    if(code==0xFF) catsprintf(_, "|Unknown");
    else catsprintf(_, "|%u", code);
  }
  return _;
}

static const char *dmi_mapped_address_interleaved_data_depth(u8 code, char *_) {
  catsprintf(_, NULL);
  if(code!=0) {
    catsprintf(_, "Interleaved Data Depth");
    if(code==0xFF) catsprintf(_, "|Unknown");
    else catsprintf(_, "|%u", code);
  }
  return _;
}

/*******************************************************************************
** 3.3.22 Built-in Pointing Device (Type 21)
*/

static const char *dmi_pointing_device_type(u8 code) {
  /* 3.3.22.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Mouse",
    "Track Ball",
    "Track Point",
    "Glide Point",
    "Touch Pad",
    "Touch Screen",
    "Optical Sensor" /* 0x09 */
  };

  if(code>=0x01 && code<=0x09) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_pointing_device_interface(u8 code) {
  /* 3.3.22.2 */
  static const char *interface[]={
    "Other", /* 0x01 */
    "Unknown",
    "Serial",
    "PS/2",
    "Infrared",
    "HIP-HIL",
    "Bus Mouse",
    "ADB (Apple Desktop Bus)" /* 0x08 */
  };
  static const char *interface_0xA0[]={
    "Bus Mouse DB-9", /* 0xA0 */
    "Bus Mouse Micro DIN",
    "USB" /* 0xA2 */
  };

  if(code>=0x01 && code<=0x08) return interface[code-0x01];
  if(code>=0xA0 && code<=0xA2) return interface_0xA0[code-0xA0];
  return out_of_spec;
}

/*******************************************************************************
** 3.3.23 Portable Battery (Type 22)
*/

static const char *dmi_battery_chemistry(u8 code) {
  /* 3.3.23.1 */
  static const char *chemistry[]={
    "Other", /* 0x01 */
    "Unknown",
    "Lead Acid",
    "Nickel Cadmium",
    "Nickel Metal Hydride",
    "Lithium Ion",
    "Zinc Air",
    "Lithium Polymer" /* 0x08 */
  };

  if(code>=0x01 && code<=0x08)
    return chemistry[code-0x01];
  return out_of_spec;
}

static const char *dmi_battery_capacity(u16 code, u8 multiplier, char *_) {
  if(code==0) catsprintf(_, "Unknown");
  else catsprintf(_, "%u mWh", code*multiplier);
  return _;
}

static const char *dmi_battery_voltage(u16 code, char *_) {
  if(code==0) catsprintf(_, " Unknown");
  else catsprintf(_, " %u mV", code);
  return _;
}

static const char *dmi_battery_maximum_error(u8 code, char *_) {
  if(code==0xFF) catsprintf(_, " Unknown");
  else catsprintf(_, " %u%%", code);
  return _;
}

/*******************************************************************************
** 3.3.24 System Reset (Type 23)
*/

static const char *dmi_system_reset_boot_option(u8 code) {
  static const char *option[]={
    "Operating System", /* 0x1 */
    "System Utilities",
    "Do Not Reboot" /* 0x3 */
  };

  if(code>=0x1)
    return option[code-0x1];
  return out_of_spec;
}

static const char *dmi_system_reset_count(u16 code, char *_) {
  if(code==0xFFFF) sprintf(_, "Unknown");
  else sprintf(_, "%u", code);
  return _;
}

static const char *dmi_system_reset_timer(u16 code, char *_) {
  if(code==0xFFFF) sprintf(_, "Unknown");
  else sprintf(_, "%u min", code);
  return _;
}

/*******************************************************************************
 * 3.3.25 Hardware Security (Type 24)
 */

static const char *dmi_hardware_security_status(u8 code) {
  static const char *status[]={
    "Disabled", /* 0x00 */
    "Enabled",
    "Not Implemented",
    "Unknown" /* 0x03 */
  };

  return status[code];
}

/*******************************************************************************
** 3.3.26 System Power Controls (Type 25)
*/

static const char *dmi_power_controls_power_on(u8 *p, char *_) {
  /* 3.3.26.1 */
  catsprintf(_, NULL);
  if(dmi_bcd_range(p[0], 0x01, 0x12)) catsprintf(_, " %02X", p[0]);
  else catsprintf(_, " *");
  if(dmi_bcd_range(p[1], 0x01, 0x31)) catsprintf(_, "-%02X", p[1]);
  else catsprintf(_, "-*");
  if(dmi_bcd_range(p[2], 0x00, 0x23)) catsprintf(_, " %02X", p[2]);
  else catsprintf(_, " *");
  if(dmi_bcd_range(p[3], 0x00, 0x59)) catsprintf(_, ":%02X", p[3]);
  else catsprintf(_, ":*");
  if(dmi_bcd_range(p[4], 0x00, 0x59)) catsprintf(_, ":%02X", p[4]);
  else catsprintf(_, ":*");
  return _;
}

/*******************************************************************************
* 3.3.27 Voltage Probe (Type 26)
*/

static const char *dmi_voltage_probe_location(u8 code) {
  /* 3.3.27.1 */
  static const char *location[]={
    "Other", /* 0x01 */
    "Unknown",
    "Processor",
    "Disk",
    "Peripheral Bay",
    "System Management Module",
    "Motherboard",
    "Memory Module",
    "Processor Module",
    "Power Unit",
    "Add-in Card" /* 0x0B */
  };

  if(code>=0x01 && code<=0x0B)
    return location[code-0x01];
  return out_of_spec;
}

static const char *dmi_probe_status(u8 code) {
  /* 3.3.27.1 */
  static const char *status[]={
    "Other", /* 0x01 */
    "Unknown",
    "OK",
    "Non-critical",
    "Critical",
    "Non-recoverable" /* 0x06 */
  };

  if(code>=0x01 && code<=0x06)
    return status[code-0x01];
  return out_of_spec;
}

static const char *dmi_voltage_probe_value(u16 code, char *_) {
  if(code==0x8000) sprintf(_, "Unknown");
  else sprintf(_, "%.3f V", (float)(i16)code/1000);
  return _;
}

static const char *dmi_voltage_probe_resolution(u16 code, char *_) {
  if(code==0x8000) sprintf(_, "Unknown");
  else sprintf(_, "%.1f mV", (float)code/10);
  return _;
}

static const char *dmi_probe_accuracy(u16 code, char *_) {
  if(code==0x8000) sprintf(_, "Unknown");
  else sprintf(_, "%.2f%%", (float)code/100);
  return _;
}

/*******************************************************************************
** 3.3.28 Cooling Device (Type 27)
*/

static const char *dmi_cooling_device_type(u8 code) {
  /* 3.3.28.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Fan",
    "Centrifugal Blower",
    "Chip Fan",
    "Cabinet Fan",
    "Power Supply Fan",
    "Heat Pipe",
    "Integrated Refrigeration" /* 0x09 */
  };
  static const char *type_0x10[]={
    "Active Cooling", /* 0x10, master.mif says 32 */
    "Passive Cooling" /* 0x11, master.mif says 33 */
  };

  if(code>=0x01 && code<=0x09)
    return type[code-0x01];
  if(code>=0x10 && code<=0x11)
    return type_0x10[code-0x10];
  return out_of_spec;
}

static const char *dmi_cooling_device_speed(u16 code, char *_) {
  if(code==0x8000) sprintf(_, "Unknown Or Non-rotating");
  else sprintf(_, "%u rpm", code);
  return _;
}

/*******************************************************************************
** 3.3.29 Temperature Probe (Type 28)
*/

static const char *dmi_temperature_probe_location(u8 code) {
  /* 3.3.29.1 */
  static const char *location[]={
    "Other", /* 0x01 */
    "Unknown",
    "Processor",
    "Disk",
    "Peripheral Bay",
    "System Management Module", /* master.mif says SMB Master */
    "Motherboard",
    "Memory Module",
    "Processor Module",
    "Power Unit",
    "Add-in Card",
    "Front Panel Board",
    "Back Panel Board",
    "Power System Board",
    "Drive Back Plane" /* 0x0F */
  };

  if(code>=0x01 && code<=0x0F)
    return location[code-0x01];
  return out_of_spec;
}

static const char *dmi_temperature_probe_value(u16 code, char *_) {
  if(code==0x8000) sprintf(_, " Unknown");
  else sprintf(_, " %.1f deg C", (float)(i16)code/10);
  return _;
}

static const char *dmi_temperature_probe_resolution(u16 code, char *_) {
  if(code==0x8000) sprintf(_, " Unknown");
  else sprintf(_, " %.3f deg C", (float)code/1000);
  return _;
}

/*******************************************************************************
** 3.3.30 Electrical Current Probe (Type 29)
*/

static const char *dmi_current_probe_value(u16 code, char *_) {
  if(code==0x8000) sprintf(_, " Unknown");
  else sprintf(_, " %.3f A", (float)(i16)code/1000);
  return _;
}

static const char *dmi_current_probe_resolution(u16 code, char *_) {
  if(code==0x8000) sprintf(_, " Unknown");
  else sprintf(_, " %.1f mA", (float)code/10);
  return _;
}

/*******************************************************************************
** 3.3.33 System Boot Information (Type 32)
*/

static const char *dmi_system_boot_status(u8 code) {
  static const char *status[]={
    "No errors detected", /* 0 */
    "No bootable media",
    "Operating system failed to load",
    "Firmware-detected hardware failure",
    "Operating system-detected hardware failure",
    "User-requested boot",
    "System security violation",
    "Previously-requested image",
    "System watchdog timer expired" /* 8 */
  };

  if(code<=8) return status[code];
  if(code>=128 && code<=191) return "OEM-specific";
  if(code>=192) return "Product-specific";
  return out_of_spec;
}

/*******************************************************************************
** 3.3.34 64-bit Memory Error Information (Type 33)
*/

static const char *dmi_64bit_memory_error_address(u64 code, char *_) {
  if(code.h==0x80000000 && code.l==0x00000000) catsprintf(_, " Unknown");
  else catsprintf(_, " 0x%08X%08X", code.h, code.l);
  return _;
}

/*******************************************************************************
** 3.3.35 Management Device (Type 34)
*/

static const char *dmi_management_device_type(u8 code) {
  /* 3.3.35.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "LM75",
    "LM78",
    "LM79",
    "LM80",
    "LM81",
    "ADM9240",
    "DS1780",
    "MAX1617",
    "GL518SM",
    "W83781D",
    "HT82H791" /* 0x0D */
  };

  if(code>=0x01 && code<=0x0D)
    return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_management_device_address_type(u8 code) {
  /* 3.3.35.2 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "I/O Port",
    "Memory",
    "SMBus" /* 0x05 */
  };

  if(code>=0x01 && code<=0x05)
    return type[code-0x01];
  return out_of_spec;
}

/*
** 3.3.38 Memory Channel (Type 37)
*/

static const char *dmi_memory_channel_type(u8 code) {
  /* 3.3.38.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "RamBus",
    "SyncLink" /* 0x04 */
  };

  if(code>=0x01 && code<=0x04)
    return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_memory_channel_devices(u8 count, u8 *p, char *_) {
  int i;

  catsprintf(_, NULL);
  for(i=1; i<=count; i++) {
    catsprintf(_, "Device %u Load: %u", i, p[3*i]);
    if(!(opt.flags & FLAG_QUIET))
      catsprintf(_, "|Device %u Handle: 0x%04X", i, WORD(p+3*i+1));
  }
  return _;
}

/*******************************************************************************
** 3.3.39 IPMI Device Information (Type 38)
*/

static const char *dmi_ipmi_interface_type(u8 code) {
  /* 3.3.39.1 and IPMI 2.0, appendix C1, table C1-2 */
  static const char *type[]={
    "Unknown", /* 0x00 */
    "KCS (Keyboard Control Style)",
    "SMIC (Server Management Interface Chip)",
    "BT (Block Transfer)",
    "SSIF (SMBus System Interface)" /* 0x04 */
  };

  if(code<=0x04)
    return type[code];
  return out_of_spec;
}

static const char *dmi_ipmi_base_address(u8 type, u8 *p, u8 lsb, char *_) {
  if(type==0x04) /* SSIF */ {
    catsprintf(_, "0x%02X (SMBus)", (*p)>>1);
  }
  else {
    u64 address=QWORD(p);
    catsprintf(_, "0x%08X%08X (%s)", address.h, (address.l&~1)|lsb,
      address.l&1?"I/O":"Memory-mapped");
  }
  return _;
}

static const char *dmi_ipmi_register_spacing(u8 code) {
  /* IPMI 2.0, appendix C1, table C1-1 */
  static const char *spacing[]={
    "Successive Byte Boundaries", /* 0x00 */
    "32-bit Boundaries",
    "16-byte Boundaries" /* 0x02 */
  };

  if(code<=0x02) return spacing[code];
  return out_of_spec;
}

/*******************************************************************************
** 3.3.40 System Power Supply (Type 39)
*/

static const char *dmi_power_supply_power(u16 code, char *_) {
  if(code==0x8000) sprintf(_, "Unknown");
  else sprintf(_, "%.3f W", (float)code/1000);
  return _;
}

static const char *dmi_power_supply_type(u8 code) {
  /* 3.3.40.1 */
  static const char *type[]={
    "Other", /* 0x01 */
    "Unknown",
    "Linear",
    "Switching",
    "Battery",
    "UPS",
    "Converter",
    "Regulator" /* 0x08 */
  };

  if(code>=0x01 && code<=0x08) return type[code-0x01];
  return out_of_spec;
}

static const char *dmi_power_supply_status(u8 code) {
  /* 3.3.40.1 */
  static const char *status[]={
    "Other", /* 0x01 */
    "Unknown",
    "OK",
    "Non-critical",
    "Critical" /* 0x05 */
  };

  if(code>=0x01 && code<=0x05) return status[code-0x01];
  return out_of_spec;
}

static const char *dmi_power_supply_range_switching(u8 code) {
  /* 3.3.40.1 */
  static const char *switching[]={
    "Other", /* 0x01 */
    "Unknown",
    "Manual",
    "Auto-switch",
    "Wide Range",
    "N/A" /* 0x06 */
  };

  if(code>=0x01 && code<=0x06) return switching[code-0x01];
  return out_of_spec;
}

/*******************************************************************************
** Main
*/

void dmi_decode(struct dmi_header *h, u16 ver, PyObject* pydata) {

  u8 *data = h->data;

  //. 0xF1 --> 0xF100
  int minor = h->type<<8;
  char _[2048]; bzero(_, 2048);
  int NEW_METHOD = 0;
  dmi_codes_major *dmiMajor = (dmi_codes_major *)&dmiCodesMajor[map_maj[h->type]];

  PyObject *pylist = PyDict_New();
  PyDict_SetItemString(pylist, "id", PyString_FromString(dmiMajor->id));
  PyDict_SetItemString(pylist, "desc", PyString_FromString(dmiMajor->desc));
  PyObject *caseData;

  /* TODO: DMI types 37 and 39 are untested */

  switch(h->type) {
    case 0: /* 3.3.1 BIOS Information */

      dmiAppendObject(++minor, "BIOS Information", NULL);

      if(h->length<0x12) break;

      dmiAppendObject(++minor, "Vendor",       dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Version",      dmi_string(h, data[0x05]));
      dmiAppendObject(++minor, "Release Date", dmi_string(h, data[0x08]));

      /*
      * On IA-64, the BIOS base address will read 0 because
      * there is no BIOS. Skip the base address and the
      * runtime size in this case.
      */

      if(WORD(data+0x06)!=0) {
        dmiAppendObject(++minor, "Address",      "0x%04X0", WORD(data+0x06));
        dmiAppendObject(++minor, "Runtime Size", dmi_bios_runtime_size((0x10000-WORD(data+0x06))<<4, _));
      }

      dmiAppendObject(++minor, "ROM Size", "%u kB", (data[0x09]+1)<<6);
      dmiAppendObject(++minor, "Characteristics", dmi_bios_characteristics(QWORD(data+0x0A), _));

      if(h->length<0x13) break;
      dmiAppendObject(++minor, "Characteristics x1", dmi_bios_characteristics_x1(data[0x12], _));

      if(h->length<0x14) break;
      dmiAppendObject(++minor, "Characteristics x2", dmi_bios_characteristics_x2(data[0x13], _));

      if(h->length<0x18) break;
      if(data[0x14]!=0xFF && data[0x15]!=0xFF)
        dmiAppendObject(++minor, "BIOS Revision", "%u.%u", data[0x14], data[0x15]);
      if(data[0x16]!=0xFF && data[0x17]!=0xFF)
        dmiAppendObject(++minor, "Firmware Revision", "%u.%u", data[0x16], data[0x17]);

      break;

    case 1: /* 3.3.2 System Information */
      dmiAppendObject(++minor, "System Information", NULL);
      if(h->length<0x08) break;
      dmiAppendObject(++minor, "Manufacturer",  dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Product Name",  dmi_string(h, data[0x05]));
      dmiAppendObject(++minor, "Version",       dmi_string(h, data[0x06]));
      dmiAppendObject(++minor, "Serial Number", dmi_string(h, data[0x07]));
      if(h->length<0x19) break;
      dmiAppendObject(++minor, "UUID",          dmi_system_uuid(data+0x08, _));
      dmiAppendObject(++minor, "Wake-up Type",  dmi_system_wake_up_type(data[0x18]));
      if(h->length<0x1B) break;
      dmiAppendObject(++minor, "SKU Number",    dmi_string(h, data[0x19]));
      dmiAppendObject(++minor, "Family",        dmi_string(h, data[0x1A]));
      break;

    case 2: /* 3.3.3 Base Board Information */
      dmiAppendObject(++minor, "Base Board Information", NULL);
      if(h->length<0x08) break;
      dmiAppendObject(++minor, "Manufacturer",  dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Product Name",  dmi_string(h, data[0x05]));
      dmiAppendObject(++minor, "Version",       dmi_string(h, data[0x06]));
      dmiAppendObject(++minor, "Serial Number", dmi_string(h, data[0x07]));
      if(h->length<0x0F) break;
      dmiAppendObject(++minor, "Asset Tag",           dmi_string(h, data[0x08]));
      dmiAppendObject(++minor, "Features",            dmi_base_board_features(data[0x09], _));
      dmiAppendObject(++minor, "Location In Chassis", dmi_string(h, data[0x0A]));
      if(!(opt.flags & FLAG_QUIET))
        dmiAppendObject(++minor, "Chassis Handle", "0x%04X", WORD(data+0x0B));
      dmiAppendObject(++minor, "Type", dmi_base_board_type(data[0x0D]));
      if(h->length<0x0F+data[0x0E]*sizeof(u16)) break;
      if(!(opt.flags & FLAG_QUIET))
        dmiAppendObject(++minor, ">>Type", dmi_base_board_handles(data[0x0E], data+0x0F, _));
      break;

    case 3: /* 3.3.4 Chassis Information */
      NEW_METHOD = 1;
      caseData = PyDict_New();

      PyObject *_val;

      if(h->length<0x09) break;
      _val = dmi_string_py(h, data[0x04]);
      PyDict_SetItemString(caseData, "Manufacturer", _val);
      Py_DECREF(_val);

      _val = dmi_chassis_type_py(data[0x05]&0x7F);
      PyDict_SetItemString(caseData, "Type", _val);
      Py_DECREF(_val);

      _val = dmi_chassis_lock(data[0x05]>>7);
      PyDict_SetItemString(caseData, "Lock", _val);
      Py_DECREF(_val);

      _val = dmi_string_py(h, data[0x06]);
      PyDict_SetItemString(caseData, "Version", _val);
      Py_DECREF(_val);

      _val = dmi_string_py(h, data[0x07]);
      PyDict_SetItemString(caseData, "Serial Number", _val);
      Py_DECREF(_val);

      _val = dmi_string_py(h, data[0x08]);
      PyDict_SetItemString(caseData, "Asset Tag", _val);
      Py_DECREF(_val);

      if(h->length<0x0D) break;
      _val = dmi_chassis_state(data[0x09]);
      PyDict_SetItemString(caseData, "Boot-Up State", _val);
      Py_DECREF(_val);

      _val = dmi_string_py(h, data[0x09]);
      PyDict_SetItemString(caseData, "", _val);
      Py_DECREF(_val);

      _val = dmi_chassis_state(data[0x0A]);
      PyDict_SetItemString(caseData, "Power Supply State", _val);
      Py_DECREF(_val);

      _val = dmi_chassis_state(data[0x0B]);
      PyDict_SetItemString(caseData, "Thermal State", _val);
      Py_DECREF(_val);

      _val = PyString_FromString(dmi_chassis_security_status(data[0x0C]));
      PyDict_SetItemString(caseData, "Security Status", _val);
      Py_DECREF(_val);

      if(h->length<0x11) break;
      _val = PyString_FromFormat("0x%08X", DWORD(data+0x0D));
      PyDict_SetItemString(caseData, "OEM Information", _val);
      Py_DECREF(_val);

      if(h->length<0x15) break;
      _val = dmi_chassis_height(data[0x11]);
      PyDict_SetItemString(caseData, "Height", _val);
      Py_DECREF(_val);

      _val = dmi_chassis_power_cords(data[0x12]);
      PyDict_SetItemString(caseData, "Number Of Power Cords", _val);
      Py_DECREF(_val);

      //. FIXME: Clean this block - Elements is not quite right, also 
      //. FIXME: dmi_chassis_elements should return PyObject when we know
      //. FIXME: what the hell it is doing.
      if(h->length<0x15+data[0x13]*data[0x14]) break;
      _val = PyString_FromString(dmi_chassis_elements(data[0x13], data[0x14], data+0x15, _));
      PyDict_SetItemString(caseData, "Elements", _val);
      Py_DECREF(_val);

      break;


    case 4: /* 3.3.5 Processor Information */
      dmiAppendObject(++minor, "Processor Information", NULL);
      if(h->length<0x1A) break;
      dmiAppendObject(++minor, "Socket Designation", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Type",               dmi_processor_type(data[0x05]));
      dmiAppendObject(++minor, "Family",             dmi_processor_family(data[0x06]));
      dmiAppendObject(++minor, "Manufacturer",       dmi_string(h, data[0x07]));
      dmiAppendObject(++minor, ">>Manufacturer",     dmi_processor_id(data[0x06], data+8, dmi_string(h, data[0x10]), _));
      dmiAppendObject(++minor, "Version",            dmi_string(h, data[0x10]));
      dmiAppendObject(++minor, "Voltage",            dmi_processor_voltage(data[0x11], _));
      dmiAppendObject(++minor, "External Clock",     dmi_processor_frequency(data+0x12, _));
      dmiAppendObject(++minor, "Max Speed",          dmi_processor_frequency(data+0x14, _));
      dmiAppendObject(++minor, "Current Speed",      dmi_processor_frequency(data+0x16, _));
      if(data[0x18]&(1<<6))
        dmiAppendObject(++minor, "Status", "Populated:%s", dmi_processor_status(data[0x18]&0x07));
      else
        dmiAppendObject(++minor, "Status", "Unpopulated");
      dmiAppendObject(++minor, "Upgrade",            dmi_processor_upgrade(data[0x19]));
      if(h->length<0x20) break;
      if(!(opt.flags & FLAG_QUIET)) {
        dmiAppendObject(++minor, "L1 Cache Handle",  dmi_processor_cache(WORD(data+0x1A), "L1", ver, _));
        dmiAppendObject(++minor, "L2 Cache Handle",  dmi_processor_cache(WORD(data+0x1C), "L2", ver, _));
        dmiAppendObject(++minor, "L3 Cache Handle",  dmi_processor_cache(WORD(data+0x1E), "L3", ver, _));
      }
      if(h->length<0x23) break;
      dmiAppendObject(++minor, "Serial Number",      dmi_string(h, data[0x20]));
      dmiAppendObject(++minor, "Asset Tag",          dmi_string(h, data[0x21]));
      dmiAppendObject(++minor, "Part Number",        dmi_string(h, data[0x22]));
      if(h->length<0x28) break;
      if(data[0x23]!=0)
        dmiAppendObject(++minor, "Core Count", "%u", data[0x23]);
      if(data[0x24]!=0)
        dmiAppendObject(++minor, "Core Enabled", "%u", data[0x24]);
      if(data[0x25]!=0)
        dmiAppendObject(++minor, "Thread Count", "%u", data[0x25]);
      dmiAppendObject(++minor, "Characteristics", dmi_processor_characteristics(WORD(data+0x26), _));
      break;

    case 5: /* 3.3.6 Memory Controller Information */
      dmiAppendObject(++minor, "Memory Controller Information", NULL);
      if(h->length<0x0F) break;
      dmiAppendObject(++minor, "Error Detecting Method",          dmi_memory_controller_ed_method(data[0x04]));
      dmiAppendObject(++minor, "Error Correcting Capabilities",   dmi_memory_controller_ec_capabilities(data[0x05], _));
      dmiAppendObject(++minor, "Supported Interleave",            dmi_memory_controller_interleave(data[0x06]));
      dmiAppendObject(++minor, "Current Interleave",              dmi_memory_controller_interleave(data[0x07]));
      dmiAppendObject(++minor, "Maximum Memory Module Size",      "%u MB", 1<<data[0x08]);
      dmiAppendObject(++minor, "Maximum Total Memory Size",       "%u MB", data[0x0E]*(1<<data[0x08]));
      dmiAppendObject(++minor, "Supported Speeds",                dmi_memory_controller_speeds(WORD(data+0x09), _));
      dmiAppendObject(++minor, "Supported Memory Types",          dmi_memory_module_types(WORD(data+0x0B), _));
      dmiAppendObject(++minor, "Memory Module Voltage",           dmi_processor_voltage(data[0x0D], _));
      if(h->length<0x0F+data[0x0E]*sizeof(u16)) break;
      dmiAppendObject(++minor, "Sluts", dmi_memory_controller_slots(data[0x0E], data+0x0F, _));
      if(h->length<0x10+data[0x0E]*sizeof(u16)) break;
      dmiAppendObject(++minor, "Enabled Error Correcting Capabilities", dmi_memory_controller_ec_capabilities(data[0x0F+data[0x0E]*sizeof(u16)], _));
      break;

    case 6: /* 3.3.7 Memory Module Information */
      dmiAppendObject(++minor, "Memory Module Information", NULL);
      if(h->length<0x0C) break;
      dmiAppendObject(++minor, "Socket Designation",  dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Bank Connections",    dmi_memory_module_connections(data[0x05], _));
      dmiAppendObject(++minor, "Current Speed",       dmi_memory_module_speed(data[0x06], _));
      dmiAppendObject(++minor, "Type",                dmi_memory_module_types(WORD(data+0x07), _));
      dmiAppendObject(++minor, "Installed Size",      dmi_memory_module_size(data[0x09], _));
      dmiAppendObject(++minor, "Enabled Size",        dmi_memory_module_size(data[0x0A], _));
      dmiAppendObject(++minor, "Error Status",        dmi_memory_module_error(data[0x0B], _));
      break;

    case 7: /* 3.3.8 Cache Information */
      dmiAppendObject(++minor, "Cache Information", NULL);
      if(h->length<0x0F) break;
      dmiAppendObject(++minor, "Socket Designation", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Configuration", "%s, %s, Level %u",
        WORD(data+0x05)&0x0080?"Enabled":"Disabled",
        WORD(data+0x05)&0x0008?"Socketed":"Not Socketed",
        (WORD(data+0x05)&0x0007)+1);
      dmiAppendObject(++minor, "Operational Mode", dmi_cache_mode((WORD(data+0x05)>>8)&0x0003));
      dmiAppendObject(++minor, "Location", dmi_cache_location((WORD(data+0x05)>>5)&0x0003));
      dmiAppendObject(++minor, "Installed Size", dmi_cache_size(WORD(data+0x09), _));
      dmiAppendObject(++minor, "Maximum Size", dmi_cache_size(WORD(data+0x07), _));
      dmiAppendObject(++minor, "Supported SRAM Types", dmi_cache_types(WORD(data+0x0B), _));
      dmiAppendObject(++minor, "Installed SRAM Type", dmi_cache_types(WORD(data+0x0D), _));
      if(h->length<0x13) break;
      dmiAppendObject(++minor, "Speed", dmi_memory_module_speed(data[0x0F], _));
      dmiAppendObject(++minor, "Error Correction Type", dmi_cache_ec_type(data[0x10]));
      dmiAppendObject(++minor, "System Type", dmi_cache_type(data[0x11]));
      dmiAppendObject(++minor, "Associativity", dmi_cache_associativity(data[0x12]));
      break;

    case 8: /* 3.3.9 Port Connector Information */
      dmiAppendObject(++minor, "Port Connector Information", NULL);
      if(h->length<0x09) break;
      dmiAppendObject(++minor, "Internal Reference Designator", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Internal Connector Type", dmi_port_connector_type(data[0x05]));
      dmiAppendObject(++minor, "External Reference Designator", dmi_string(h, data[0x06]));
      dmiAppendObject(++minor, "External Connector Type", dmi_port_connector_type(data[0x07]));
      dmiAppendObject(++minor, "Port Type", dmi_port_type(data[0x08]));
      break;

    case 9: /* 3.3.10 System Slots */
      dmiAppendObject(++minor, "System Slot Information", NULL);
      if(h->length<0x0C) break;
      dmiAppendObject(++minor, "Designation", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Type", "%s%s", dmi_slot_bus_width(data[0x06]), dmi_slot_type(data[0x05]));
      dmiAppendObject(++minor, "Current Usage", dmi_slot_current_usage(data[0x07]));
      dmiAppendObject(++minor, "Length", "%s:<%s>", dmi_slot_length(data[0x08]), dmi_slot_id(data[0x09], data[0x0A], data[0x05], _));
      dmiAppendObject(++minor, "Characteristics", (h->length<0x0D)?dmi_slot_characteristics(data[0x0B], 0x00, _):dmi_slot_characteristics(data[0x0B], data[0x0C], _));
      break;

    case 10: /* 3.3.11 On Board Devices Information */
      NEW_METHOD = 1;
      caseData = dmi_on_board_devices(h);

      break;

    case 11: /* 3.3.12 OEM Strings */
      dmiAppendObject(++minor, "OEM Strings", NULL);
      if(h->length<0x05) break;
      dmiAppendObject(++minor, ">>>", dmi_oem_strings(h, _));
      break;

    case 12: /* 3.3.13 System Configuration Options */
      dmiAppendObject(++minor, "System Configuration Options", NULL);
      if(h->length<0x05) break;
      dmiAppendObject(++minor, ">>>", dmi_system_configuration_options(h, _));
      break;

    case 13: /* 3.3.14 BIOS Language Information */
      dmiAppendObject(++minor, "BIOS Language Information", NULL);
      if(h->length<0x16) break;
      dmiAppendObject(++minor, "Installable Languages", "%u", data[0x04]);
      dmiAppendObject(++minor, ">>>", dmi_bios_languages(h, _));
      dmiAppendObject(++minor, "Currently Installed Language", dmi_string(h, data[0x15]));
      break;

    case 14: /* 3.3.15 Group Associations */
      dmiAppendObject(++minor, "Group Associations", NULL);
      if(h->length<0x05) break;
      dmiAppendObject(++minor, "Name", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Items", "%u", (h->length-0x05)/3);
      dmiAppendObject(++minor, ">>>", dmi_group_associations_items((h->length-0x05)/3, data+0x05, _));
      break;

    case 15: /* 3.3.16 System Event Log */
      dmiAppendObject(++minor, "System Event Log", NULL);
      if(h->length<0x14) break;
      dmiAppendObject(++minor, "Area Length", "%u bytes", WORD(data+0x04));
      dmiAppendObject(++minor, "Header Start Offset", "0x%04X", WORD(data+0x06));
      if(WORD(data+0x08)-WORD(data+0x06))
        dmiAppendObject(++minor, "Header Length", "%u byte%s", WORD(data+0x08)-WORD(data+0x06), WORD(data+0x08)-WORD(data+0x06)>1?"s":"");
      dmiAppendObject(++minor, "Data Start Offset", "0x%04X", WORD(data+0x08));
      dmiAppendObject(++minor, "Access Method", "%s", dmi_event_log_method(data[0x0A]));
      dmiAppendObject(++minor, "Access Address", "%s", dmi_event_log_address(data[0x0A], data+0x10, _));
      dmiAppendObject(++minor, "Status", "%s", dmi_event_log_status(data[0x0B], _));
      dmiAppendObject(++minor, "Change Token", "0x%08X", DWORD(data+0x0C));
      if(h->length<0x17) break;
      dmiAppendObject(++minor, "Header Format", "%s", dmi_event_log_header_type(data[0x14]));
      dmiAppendObject(++minor, "Supported Log Type Descriptors", "%u", data[0x15]);
      if(h->length<0x17+data[0x15]*data[0x16]) break;
      dmi_event_log_descriptors(data[0x15], data[0x16], data+0x17, _);
      break;

    case 16: /* 3.3.17 Physical Memory Array */
      dmiAppendObject(++minor, "Physical Memory Array", NULL);
      if(h->length<0x0F) break;
      dmiAppendObject(++minor, "Location", "%s", dmi_memory_array_location(data[0x04]));
      dmiAppendObject(++minor, "Use", "%s", dmi_memory_array_use(data[0x05]));
      dmiAppendObject(++minor, "Error Correction Type", "%s", dmi_memory_array_ec_type(data[0x06]));
      dmiAppendObject(++minor, "Maximum Capacity", dmi_memory_array_capacity(DWORD(data+0x07), _));
      if(!(opt.flags & FLAG_QUIET))
        dmiAppendObject(++minor, "Error Information Handle", dmi_memory_array_error_handle(WORD(data+0x0B), _));
      dmiAppendObject(++minor, "Number Of Devices", "%u", WORD(data+0x0D));
      break;


    case 17: /* 3.3.18 Memory Device */
      dmiAppendObject(++minor, "Memory Device", NULL);
      if(h->length<0x15) break;
      if(!(opt.flags & FLAG_QUIET)) {
        dmiAppendObject(++minor, "Array Handle", "0x%04X", WORD(data+0x04));
        dmiAppendObject(++minor, "Error Information Handle", dmi_memory_array_error_handle(WORD(data+0x06), _));
      }
      dmiAppendObject(++minor, "Total Width", dmi_memory_device_width(WORD(data+0x08), _));
      dmiAppendObject(++minor, "Data Width", dmi_memory_device_width(WORD(data+0x0A), _));
      dmiAppendObject(++minor, "Size", dmi_memory_device_size(WORD(data+0x0C), _));
      dmiAppendObject(++minor, "Form Factor", "%s", dmi_memory_device_form_factor(data[0x0E]));
      dmiAppendObject(++minor, "Set", dmi_memory_device_set(data[0x0F], _));
      dmiAppendObject(++minor, "Locator", "%s", dmi_string(h, data[0x10]));
      dmiAppendObject(++minor, "Bank Locator", "%s", dmi_string(h, data[0x11]));
      dmiAppendObject(++minor, "Type", "%s", dmi_memory_device_type(data[0x12]));
      dmiAppendObject(++minor, "Type Detail", "%s", dmi_memory_device_type_detail(WORD(data+0x13), _));
      if(h->length<0x17) break;
      dmiAppendObject(++minor, "Speed", "%s", dmi_memory_device_speed(WORD(data+0x15), _));
      if(h->length<0x1B) break;
      dmiAppendObject(++minor, "Manufacturer", "%s", dmi_string(h, data[0x17]));
      dmiAppendObject(++minor, "Serial Number", "%s", dmi_string(h, data[0x18]));
      dmiAppendObject(++minor, "Asset Tag", "%s", dmi_string(h, data[0x19]));
      dmiAppendObject(++minor, "\tPart Number: %s\n",
        dmi_string(h, data[0x1A]));
      break;

    case 18: /* 3.3.19 32-bit Memory Error Information */
      dmiAppendObject(++minor, "32-bit Memory Error Information", NULL);
      if(h->length<0x17) break;
      dmiAppendObject(++minor, "Type", "%s", dmi_memory_error_type(data[0x04]));
      dmiAppendObject(++minor, "Granularity", "%s", dmi_memory_error_granularity(data[0x05]));
      dmiAppendObject(++minor, "Operation", "%s", dmi_memory_error_operation(data[0x06]));
      dmiAppendObject(++minor, "Vendor Syndrome", "%s", dmi_memory_error_syndrome(DWORD(data+0x07), _));
      dmiAppendObject(++minor, "Memory Array Address", "%s", dmi_32bit_memory_error_address(DWORD(data+0x0B), _));
      dmiAppendObject(++minor, "Device Address", "%s", dmi_32bit_memory_error_address(DWORD(data+0x0F), _));
      dmiAppendObject(++minor, "Resolution", dmi_32bit_memory_error_address(DWORD(data+0x13), _));
      break;

    case 19: /* 3.3.20 Memory Array Mapped Address */
      dmiAppendObject(++minor, "Memory Array Mapped Address", NULL);
      if(h->length<0x0F) break;
      dmiAppendObject(++minor, "Starting Address", "0x%08X%03X", DWORD(data+0x04)>>2, (DWORD(data+0x04)&0x3)<<10);
      dmiAppendObject(++minor, "Ending Address", "0x%08X%03X", DWORD(data+0x08)>>2, ((DWORD(data+0x08)&0x3)<<10)+0x3FF);
      dmiAppendObject(++minor, "Range Size", "%s", dmi_mapped_address_size(DWORD(data+0x08)-DWORD(data+0x04)+1, _));
      if(!(opt.flags & FLAG_QUIET))
        dmiAppendObject(++minor, "Physical Array Handle", "0x%04X", WORD(data+0x0C));
      dmiAppendObject(++minor, "Partition Width", "%u", data[0x0F]);
      break;

    case 20: /* 3.3.21 Memory Device Mapped Address */
      dmiAppendObject(++minor, "Memory Device Mapped Address", NULL);
      if(h->length<0x13) break;
      dmiAppendObject(++minor, "Starting Address", "0x%08X%03X", DWORD(data+0x04)>>2, (DWORD(data+0x04)&0x3)<<10);
      dmiAppendObject(++minor, "Ending Address", "0x%08X%03X", DWORD(data+0x08)>>2, ((DWORD(data+0x08)&0x3)<<10)+0x3FF);
      dmiAppendObject(++minor, "Range Size", dmi_mapped_address_size(DWORD(data+0x08)-DWORD(data+0x04)+1, _));
      if(!(opt.flags & FLAG_QUIET)) {
        dmiAppendObject(++minor, "Physical Device Handle", "0x%04X", WORD(data+0x0C));
        dmiAppendObject(++minor, "Memory Array Mapped Address Handle", "0x%04X", WORD(data+0x0E));
      }
      dmiAppendObject(++minor, "Partition Row Position", dmi_mapped_address_row_position(data[0x10], _));
      dmiAppendObject(++minor, ">>>", dmi_mapped_address_interleave_position(data[0x11], _));
      dmiAppendObject(++minor, ">>>", dmi_mapped_address_interleaved_data_depth(data[0x12], _));
      break;

    case 21: /* 3.3.22 Built-in Pointing Device */
      dmiAppendObject(++minor, "Built-in Pointing Device", NULL);
      if(h->length<0x07) break;
      dmiAppendObject(++minor, "Type",      "%s", dmi_pointing_device_type(data[0x04]));
      dmiAppendObject(++minor, "Interface", "%s", dmi_pointing_device_interface(data[0x05]));
      dmiAppendObject(++minor, "Buttons",   "%u", data[0x06]);
      break;

    case 22: /* 3.3.23 Portable Battery */
      dmiAppendObject(++minor, "Portable Battery", NULL);
      if(h->length<0x10) break;
      dmiAppendObject(++minor, "Location", "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Manufacturer", "%s", dmi_string(h, data[0x05]));
      if(data[0x06] || h->length<0x1A)
        dmiAppendObject(++minor, "Manufacture Date", "%s", dmi_string(h, data[0x06]));
      if(data[0x07] || h->length<0x1A)
        dmiAppendObject(++minor, "Serial Number", "%s", dmi_string(h, data[0x07]));
      dmiAppendObject(++minor, "Name", "%s", dmi_string(h, data[0x08]));
      if(data[0x09]!=0x02 || h->length<0x1A)
        dmiAppendObject(++minor, "Chemistry", "%s", dmi_battery_chemistry(data[0x09]));
      dmiAppendObject(++minor, "Design Capacity", (h->length<0x1A)?dmi_battery_capacity(WORD(data+0x0A), 1, _):dmi_battery_capacity(WORD(data+0x0A), data[0x15], _));
      dmiAppendObject(++minor, "Design Voltage", dmi_battery_voltage(WORD(data+0x0C), _));
      dmiAppendObject(++minor, "SBDS Version", "%s", dmi_string(h, data[0x0E]));
      dmiAppendObject(++minor, "Maximum Error", dmi_battery_maximum_error(data[0x0F], _));
      if(h->length<0x1A) break;
      if(data[0x07]==0)
        dmiAppendObject(++minor, "SBDS Serial Number", "%04X", WORD(data+0x10));
      if(data[0x06]==0)
        dmiAppendObject(++minor, "SBDS Manufacture Date", "%u-%02u-%02u", 1980+(WORD(data+0x12)>>9), (WORD(data+0x12)>>5)&0x0F, WORD(data+0x12)&0x1F);
      if(data[0x09]==0x02)
        dmiAppendObject(++minor, "SBDS Chemistry", "%s", dmi_string(h, data[0x14]));
      dmiAppendObject(++minor, "OEM-specific Information", "0x%08X", DWORD(data+0x16));
      break;

    case 23: /* 3.3.24 System Reset */
      dmiAppendObject(++minor, "System Reset", NULL);
      if(h->length<0x0D) break;
      dmiAppendObject(++minor, "Status", "%s", data[0x04]&(1<<0)?"Enabled":"Disabled");
      dmiAppendObject(++minor, "Watchdog Timer", "%s", data[0x04]&(1<<5)?"Present":"Not Present");
      if(!(data[0x04]&(1<<5))) break;
      dmiAppendObject(++minor, "Boot Option", "%s", dmi_system_reset_boot_option((data[0x04]>>1)&0x3));
      dmiAppendObject(++minor, "Boot Option On Limit", "%s", dmi_system_reset_boot_option((data[0x04]>>3)&0x3));
      dmiAppendObject(++minor, "Reset Count", "%s", dmi_system_reset_count(WORD(data+0x05), _));
      dmiAppendObject(++minor, "Reset Limit", "%s", dmi_system_reset_count(WORD(data+0x07), _));
      dmiAppendObject(++minor, "Timer Interval", dmi_system_reset_timer(WORD(data+0x09), _));
      dmiAppendObject(++minor, "Timeout", dmi_system_reset_timer(WORD(data+0x0B), _));
      break;

    case 24: /* 3.3.25 Hardware Security */
      dmiAppendObject(++minor, "Hardware Security", NULL);
      if(h->length<0x05) break;
      dmiAppendObject(++minor, "Power-On Password Status", "%s", dmi_hardware_security_status(data[0x04]>>6));
      dmiAppendObject(++minor, "Keyboard Password Status", "%s", dmi_hardware_security_status((data[0x04]>>4)&0x3));
      dmiAppendObject(++minor, "Administrator Password Status", "%s", dmi_hardware_security_status((data[0x04]>>2)&0x3));
      dmiAppendObject(++minor, "Front Panel Reset Status", "%s", dmi_hardware_security_status(data[0x04]&0x3));
      break;

    case 25: /* 3.3.26 System Power Controls */
      dmiAppendObject(++minor, "System Power Controls", NULL);
      if(h->length<0x09) break;
      dmiAppendObject(++minor, "Next Scheduled Power-on", dmi_power_controls_power_on(data+0x04, _));
      break;

    case 26: /* 3.3.27 Voltage Probe */
      dmiAppendObject(++minor, "Voltage Probe", NULL);
      if(h->length<0x14) break;
      dmiAppendObject(++minor, "Description", "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Location", "%s", dmi_voltage_probe_location(data[0x05]&0x1f));
      dmiAppendObject(++minor, "Status", "%s", dmi_probe_status(data[0x05]>>5));
      dmiAppendObject(++minor, "Maximum Value", dmi_voltage_probe_value(WORD(data+0x06), _));
      dmiAppendObject(++minor, "Minimum Value", dmi_voltage_probe_value(WORD(data+0x08), _));
      dmiAppendObject(++minor, "Resolution", dmi_voltage_probe_resolution(WORD(data+0x0A), _));
      dmiAppendObject(++minor, "Tolerance", dmi_voltage_probe_value(WORD(data+0x0C), _));
      dmiAppendObject(++minor, "Accuracy", dmi_probe_accuracy(WORD(data+0x0E), _));
      dmiAppendObject(++minor, "OEM-specific Information", "0x%08X", DWORD(data+0x10));
      if(h->length<0x16) break;
      dmiAppendObject(++minor, "Nominal Value", dmi_voltage_probe_value(WORD(data+0x14), _));
      break;

    case 27: /* 3.3.28 Cooling Device */
      dmiAppendObject(++minor, "Cooling Device", NULL);
      if(h->length<0x0C) break;
      if(!(opt.flags & FLAG_QUIET) && WORD(data+0x04)!=0xFFFF)
        dmiAppendObject(++minor, "Temperature Probe Handle", "0x%04X", WORD(data+0x04));
      dmiAppendObject(++minor, "Type", "%s", dmi_cooling_device_type(data[0x06]&0x1f));
      dmiAppendObject(++minor, "Status", "%s", dmi_probe_status(data[0x06]>>5));
      if(data[0x07]!=0x00)
        dmiAppendObject(++minor, "Cooling Unit Group", "%u", data[0x07]);
      dmiAppendObject(++minor, "OEM-specific Information", "0x%08X", DWORD(data+0x08));
      if(h->length<0x0E) break;
      dmiAppendObject(++minor, "Nominal Speed", dmi_cooling_device_speed(WORD(data+0x0C), _));
      break;

    case 28: /* 3.3.29 Temperature Probe */
      dmiAppendObject(++minor, "Temperature Probe", NULL);
      if(h->length<0x14) break;
      dmiAppendObject(++minor, "Description", "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Location", "%s", dmi_temperature_probe_location(data[0x05]&0x1F));
      dmiAppendObject(++minor, "Status", "%s", dmi_probe_status(data[0x05]>>5));
      dmiAppendObject(++minor, "Maximum Value", dmi_temperature_probe_value(WORD(data+0x06), _));
      dmiAppendObject(++minor, "Minimum Value", dmi_temperature_probe_value(WORD(data+0x08), _));
      dmiAppendObject(++minor, "Resolution", dmi_temperature_probe_resolution(WORD(data+0x0A), _));
      dmiAppendObject(++minor, "Tolerance", dmi_temperature_probe_value(WORD(data+0x0C), _));
      dmiAppendObject(++minor, "Accuracy", dmi_probe_accuracy(WORD(data+0x0E), _));
      dmiAppendObject(++minor, "OEM-specific Information", "0x%08X", DWORD(data+0x10));
      if(h->length<0x16) break;
      dmiAppendObject(++minor, "Nominal Value", dmi_temperature_probe_value(WORD(data+0x14), _));
      break;

    case 29: /* 3.3.30 Electrical Current Probe */
      dmiAppendObject(++minor, "Electrical Current Probe", NULL);
      if(h->length<0x14) break;
      dmiAppendObject(++minor, "Description", "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Location", "%s", dmi_voltage_probe_location(data[5]&0x1F));
      dmiAppendObject(++minor, "Status", "%s", dmi_probe_status(data[0x05]>>5));
      dmiAppendObject(++minor, "Maximum Value", dmi_current_probe_value(WORD(data+0x06), _));
      dmiAppendObject(++minor, "Minimum Value", dmi_current_probe_value(WORD(data+0x08), _));
      dmiAppendObject(++minor, "Resolution", dmi_current_probe_resolution(WORD(data+0x0A), _));
      dmiAppendObject(++minor, "Tolerance", dmi_current_probe_value(WORD(data+0x0C), _));
      dmiAppendObject(++minor, "Accuracy", dmi_probe_accuracy(WORD(data+0x0E), _));
      dmiAppendObject(++minor, "OEM-specific Information", "0x%08X", DWORD(data+0x10));
      if(h->length<0x16) break;
      dmiAppendObject(++minor, "Nominal Value", dmi_current_probe_value(WORD(data+0x14), _));
      break;

    case 30: /* 3.3.31 Out-of-band Remote Access */
      dmiAppendObject(++minor, "Out-of-band Remote Access", NULL);
      if(h->length<0x06) break;
      dmiAppendObject(++minor, "Manufacturer Name", "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Inbound Connection", "%s", data[0x05]&(1<<0)?"Enabled":"Disabled");
      dmiAppendObject(++minor, "Outbound Connection", "%s", data[0x05]&(1<<1)?"Enabled":"Disabled");
      break;

    case 31: /* 3.3.32 Boot Integrity Services Entry Point */
      dmiAppendObject(++minor, "Boot Integrity Services Entry Point", NULL);
      break;

    case 32: /* 3.3.33 System Boot Information */
      dmiAppendObject(++minor, "System Boot Information", NULL);
      if(h->length<0x0B) break;
      dmiAppendObject(++minor, "Status", "%s", dmi_system_boot_status(data[0x0A]));
      break;

    case 33: /* 3.3.34 64-bit Memory Error Information */
      if(h->length<0x1F) break;
      dmiAppendObject(++minor, "64-bit Memory Error Information", NULL);
      dmiAppendObject(++minor, "Type", "%s", dmi_memory_error_type(data[0x04]));
      dmiAppendObject(++minor, "Granularity", "%s", dmi_memory_error_granularity(data[0x05]));
      dmiAppendObject(++minor, "Operation", "%s", dmi_memory_error_operation(data[0x06]));
      dmiAppendObject(++minor, "Vendor Syndrome", "%s", dmi_memory_error_syndrome(DWORD(data+0x07), _));
      dmiAppendObject(++minor, "Memory Array Address", "%s", dmi_64bit_memory_error_address(QWORD(data+0x0B), _));
      dmiAppendObject(++minor, "Device Address", "%s", dmi_64bit_memory_error_address(QWORD(data+0x13), _));
      dmiAppendObject(++minor, "Resolution", dmi_32bit_memory_error_address(DWORD(data+0x1B), _));
      break;

    case 34: /* 3.3.35 Management Device */
      dmiAppendObject(++minor, "Management Device", NULL);
      if(h->length<0x0B) break;
      dmiAppendObject(++minor, "Description",   "%s", dmi_string(h, data[0x04]));
      dmiAppendObject(++minor, "Type",          "%s", dmi_management_device_type(data[0x05]));
      dmiAppendObject(++minor, "Address",       "0x%08X", DWORD(data+0x06));
      dmiAppendObject(++minor, "Address Type",  "%s", dmi_management_device_address_type(data[0x0A]));
      break;

    case 35: /* 3.3.36 Management Device Component */
      dmiAppendObject(++minor, "Management Device Component", NULL);
      if(h->length<0x0B) break;
      dmiAppendObject(++minor, "Description", "%s", dmi_string(h, data[0x04]));
      if(!(opt.flags & FLAG_QUIET)) {
        dmiAppendObject(++minor, "Management Device Handle", "0x%04X", WORD(data+0x05));
        dmiAppendObject(++minor, "Component Handle", "0x%04X", WORD(data+0x07));
        if(WORD(data+0x09)!=0xFFFF) dmiAppendObject(++minor, "Threshold Handle", "0x%04X", WORD(data+0x09));
      }
      break;

    case 36: /* 3.3.37 Management Device Threshold Data */
      dmiAppendObject(++minor, "Management Device Threshold Data", NULL);
      if(h->length<0x10) break;
      if(WORD(data+0x04)!=0x8000) dmiAppendObject(++minor, "Lower Non-critical Threshold",    "%d", (i16)WORD(data+0x04));
      if(WORD(data+0x06)!=0x8000) dmiAppendObject(++minor, "Upper Non-critical Threshold",    "%d", (i16)WORD(data+0x06));
      if(WORD(data+0x08)!=0x8000) dmiAppendObject(++minor, "Lower Critical Threshold",        "%d", (i16)WORD(data+0x08));
      if(WORD(data+0x0A)!=0x8000) dmiAppendObject(++minor, "Upper Critical Threshold",        "%d", (i16)WORD(data+0x0A));
      if(WORD(data+0x0C)!=0x8000) dmiAppendObject(++minor, "Lower Non-recoverable Threshold", "%d", (i16)WORD(data+0x0C));
      if(WORD(data+0x0E)!=0x8000) dmiAppendObject(++minor, "Upper Non-recoverable Threshold", "%d", (i16)WORD(data+0x0E));
      break;

    case 37: /* 3.3.38 Memory Channel */
      dmiAppendObject(++minor, "Memory Channel", NULL);
      if(h->length<0x07) break;
      dmiAppendObject(++minor, "Type", "%s", dmi_memory_channel_type(data[0x04]));
      dmiAppendObject(++minor, "Maximal Load", "%u", data[0x05]);
      dmiAppendObject(++minor, "Devices", "%u", data[0x06]);
      if(h->length<0x07+3*data[0x06]) break;
      dmiAppendObject(++minor, ">>>", dmi_memory_channel_devices(data[0x06], data+0x07, _));
      break;

    case 38: /* 3.3.39 IPMI Device Information */
      /*
       * We use the word "Version" instead of "Revision", conforming to
       * the IPMI specification.
       */
      dmiAppendObject(++minor, "IPMI Device Information", NULL);
      if(h->length<0x10) break;
      dmiAppendObject(++minor, "Interface Type", "%s", dmi_ipmi_interface_type(data[0x04]));
      dmiAppendObject(++minor, "Specification Version", "%u.%u", data[0x05]>>4, data[0x05]&0x0F);
      dmiAppendObject(++minor, "I2C Slave Address", "0x%02x", data[0x06]>>1);
      if(data[0x07]!=0xFF)
        dmiAppendObject(++minor, "NV Storage Device Address", "%u", data[0x07]);
      else
        dmiAppendObject(++minor, "NV Storage Device: Not Present", NULL);
      dmiAppendObject(++minor, "Base Address", "%s", dmi_ipmi_base_address(data[0x04], data+0x08, h->length<0x12?0:(data[0x10]>>5)&1, _));
      if(h->length<0x12) break;
      if(data[0x04]!=0x04) {
        dmiAppendObject(++minor, "Register Spacing", "%s", dmi_ipmi_register_spacing(data[0x10]>>6));
        if(data[0x10]&(1<<3)) {
          dmiAppendObject(++minor, "Interrupt Polarity", "%s", data[0x10]&(1<<1)?"Active High":"Active Low");
          dmiAppendObject(++minor, "Interrupt Trigger Mode", "%s", data[0x10]&(1<<0)?"Level":"Edge");
        }
      }
      if(data[0x11]!=0x00) {
        dmiAppendObject(++minor, "Interrupt Number", "%x", data[0x11]);
      }
      break;

    case 39: /* 3.3.40 System Power Supply */
      dmiAppendObject(++minor, "System Power Supply", NULL);
      if(h->length<0x10) break;
      if(data[0x04]!=0x00)
        dmiAppendObject(++minor, "Power Unit Group", "%u", data[0x04]);
      dmiAppendObject(++minor, "Location",           "%s", dmi_string(h, data[0x05]));
      dmiAppendObject(++minor, "Name",               "%s", dmi_string(h, data[0x06]));
      dmiAppendObject(++minor, "Manufacturer",       "%s", dmi_string(h, data[0x07]));
      dmiAppendObject(++minor, "Serial Numberr",     "%s", dmi_string(h, data[0x08]));
      dmiAppendObject(++minor, "Asset Tag",          "%s", dmi_string(h, data[0x09]));
      dmiAppendObject(++minor, "Model Part Number",  "%s", dmi_string(h, data[0x0A]));
      dmiAppendObject(++minor, "Revision",           "%s", dmi_string(h, data[0x0B]));
      dmiAppendObject(++minor, "Max Power Capacity", "%s", dmi_power_supply_power(WORD(data+0x0C), _));
      if(WORD(data+0x0E)&(1<<1))
        dmiAppendObject(++minor, "Status", "Present %s", dmi_power_supply_status((WORD(data+0x0E)>>7)&0x07));
      else
        dmiAppendObject(++minor, "Status", "Not Present");
      dmiAppendObject(++minor, "Type",               "%s", dmi_power_supply_type((WORD(data+0x0E)>>10)&0x0F), _);
      dmiAppendObject(++minor, "Input Voltage Range Switching", "%s", dmi_power_supply_range_switching((WORD(data+0x0E)>>3)&0x0F));
      dmiAppendObject(++minor, "Plugged", "%s", WORD(data+0x0E)&(1<<2)?"No":"Yes");
      dmiAppendObject(++minor, "Hot Replaceable", "%s", WORD(data+0x0E)&(1<<0)?"Yes":"No");
      if(h->length<0x16) break;
      if(!(opt.flags & FLAG_QUIET)) {
        if(WORD(data+0x10)!=0xFFFF)
          dmiAppendObject(++minor, "Input Voltage Probe Handle", "0x%04X", WORD(data+0x10));
        if(WORD(data+0x12)!=0xFFFF)
          dmiAppendObject(++minor, "Cooling Device Handle", "0x%04X", WORD(data+0x12));
        if(WORD(data+0x14)!=0xFFFF)
          dmiAppendObject(++minor, "Input Current Probe Handle", "0x%04X", WORD(data+0x14));
      }
      break;

    case 126: /* 3.3.41 Inactive */
      catsprintf(_, "Inactive", NULL);
      break;

    case 127: /* 3.3.42 End Of Table */
      catsprintf(_, "End Of Table", NULL);
      break;

    default:
      //. TODO...
      if(dmi_decode_oem(h)) break;
      if(opt.flags & FLAG_QUIET) return;
      catsprintf(_, "%s Type", h->type>=128?"OEM-specific":"Unknown");
      catsprintf(_, "%s", dmi_dump(h, _));
  }

  //. All the magic of python dict additions happens here...
  if(!NEW_METHOD)
    dmiAppendData(pydata, ++minor);
  else {
    PyObject *_key = PyInt_FromLong(h->type);
    PyDict_SetItem(pydata, _key, caseData);
    Py_DECREF(_key);
  }

}

void to_dmi_header(struct dmi_header *h, u8 *data) {
  h->type=data[0];
  h->length=data[1];
  h->handle=WORD(data+2);
  h->data=data;
}

static void dmi_table(u32 base, u16 len, u16 num, u16 ver, const char *devmem, PyObject *pydata) {
  u8 *buf;
  u8 *data;
  int i=0;

  if(!(opt.flags & FLAG_QUIET)) {
    if(opt.type==NULL) {
      //catsprintf(_, "%u structures occupying %u bytes. <--> Table at 0x%08X.", num, len, base);
      dmiSetItem(pydata, "dmi_table_size", "%u structures occupying %u bytes", num, len);
      dmiSetItem(pydata, "dmi_table_base", "Table at 0x%08X", base);
    }
  }

  if((buf=mem_chunk(base, len, devmem))==NULL) {
#ifndef USE_MMAP
    fprintf(stderr, "Table is unreachable, sorry. Try compiling dmidecode with -DUSE_MMAP.");
#endif
    return;
  }

  data=buf;
  while(i<num && data+4<=buf+len) /* 4 is the length of an SMBIOS structure header */ {
    u8 *next;
    struct dmi_header h;
    int display;

    to_dmi_header(&h, data);
    display=((opt.type==NULL || opt.type[h.type])
      && !((opt.flags & FLAG_QUIET) && (h.type>39 && h.type<=127))
      && !opt.string);

    /*
    ** If a short entry is found (less than 4 bytes), not only it
    ** is invalid, but we cannot reliably locate the next entry.
    ** Better stop at this point, and let the user know his/her
    ** table is broken.
    */
    if(h.length<4) {
      fprintf(stderr, "Invalid entry length (%u). DMI table is broken! Stop.", (unsigned int)h.length);
      opt.flags |= FLAG_QUIET;
      break;
    }

    /* In quiet mode, stop decoding at end of table marker */
    //if((opt.flags & FLAG_QUIET) && h.type==127)
    //  break;

    //if(display && !(opt.flags & FLAG_QUIET)) {
    char hid[7];
    sprintf(hid, "0x%04X", h.handle);
    PyObject *hDict = PyDict_New();
    dmiSetItem(hDict, "dmi_type", "%d", h.type);
    dmiSetItem(hDict, "dmi_size", "%d", h.length);
    //catsprintf(_, "Handle 0x%04X, DMI type %d, %d bytes", h.handle, h.type, h.length);
    //}

    /* assign vendor for vendor-specific decodes later */
    if(h.type==0 && h.length>=5)
      dmi_set_vendor(dmi_string(&h, data[0x04]));

    /* look for the next handle */
    next=data+h.length;
    while(next-buf+1<len && (next[0]!=0 || next[1]!=0))
      next++;

    next+=2;

    if(display) {
      if(next-buf<=len) {
        if(opt.flags & FLAG_DUMP) {
          char _[512];
          dmi_dump(&h, _);
          dmiSetItem(hDict, "lookup", _);
        } else dmi_decode(&h, ver, pydata);
      } else if(!(opt.flags & FLAG_QUIET))
        fprintf(stderr, "<TRUNCATED>");
    } else if(opt.string!=NULL
         && opt.string->type==h.type
         && opt.string->offset<h.length) {
      if(opt.string->lookup!=NULL) {
        char _[512];
        strcpy(_, opt.string->lookup(data[opt.string->offset]));
        dmiSetItem(hDict, "lookup", _);
      } else if(opt.string->print!=NULL) {
        char _[512];
        opt.string->print(data+opt.string->offset, _);
        dmiSetItem(hDict, "print", _);
      } else {
        dmiSetItem(hDict, "lookup", dmi_string(&h, data[opt.string->offset]));
        //catsprintf(_, "%s\n", dmi_string(&h, data[opt.string->offset]));
      }
    }

    //. TODO: PyDict_SetItem(pydata, PyString_FromString(hid), hDict);

    data=next;
    i++;
  }

  if(!(opt.flags & FLAG_QUIET)) {
    if(i!=num)
      fprintf(stderr, "Wrong DMI structures count: %d announced, only %d decoded.\n", num, i);
    if(data-buf!=len)
      fprintf(stderr, "Wrong DMI structures length: %d bytes announced, structures occupy %d bytes.\n",
        len, (unsigned int)(data-buf));
  }

  free(buf);
}

int smbios_decode(u8 *buf, const char *devmem, PyObject* pydata) {
  if(checksum(buf, buf[0x05]) && memcmp(buf+0x10, "_DMI_", 5)==0 && checksum(buf+0x10, 0x0F)) {
    if(pydata == NULL) return 1;
    if(!(opt.flags & FLAG_QUIET))
      dmiSetItem(pydata, "detected", "SMBIOS  %u.%u present.", buf[0x06], buf[0x07]);
    dmi_table(DWORD(buf+0x18), WORD(buf+0x16), WORD(buf+0x1C), (buf[0x06]<<8)+buf[0x07], devmem, pydata);
    //. XXX dmiSetItem(pydata, "table", dmi_string(&h, data[opt.string->offset]));
    return 1;
  }

  return 0;
}

int legacy_decode(u8 *buf, const char *devmem, PyObject* pydata) {
  if(checksum(buf, 0x0F)) {
    if(pydata == NULL) return 1;
    if(!(opt.flags & FLAG_QUIET))
      dmiSetItem(pydata, "detected", "Legacy DMI %u.%u present.", buf[0x0E]>>4, buf[0x0E]&0x0F);
    dmi_table(DWORD(buf+0x08), WORD(buf+0x06), WORD(buf+0x0C), ((buf[0x0E]&0xF0)<<4)+(buf[0x0E]&0x0F), devmem, pydata);
    return 1;
  }

  return 0;
}

/*******************************************************************************
** Probe for EFI interface
*/
#define EFI_NOT_FOUND   (-1)
#define EFI_NO_SMBIOS   (-2)
int address_from_efi(size_t *address, char *_) {
  FILE *efi_systab;
  const char *filename;
  char linebuf[64];
  int ret;

  bzero(_, strlen(_));

  *address = 0; /* Prevent compiler warning */

  /*
  ** Linux <= 2.6.6: /proc/efi/systab
  ** Linux >= 2.6.7: /sys/firmware/efi/systab
  */
  if((efi_systab=fopen(filename="/sys/firmware/efi/systab", "r"))==NULL
  && (efi_systab=fopen(filename="/proc/efi/systab", "r"))==NULL) {
    /* No EFI interface, fallback to memory scan */
    return EFI_NOT_FOUND;
  }
  ret=EFI_NO_SMBIOS;
  while((fgets(linebuf, sizeof(linebuf)-1, efi_systab))!=NULL) {
    char *addrp=strchr(linebuf, '=');
    *(addrp++)='\0';
    if(strcmp(linebuf, "SMBIOS")==0) {
      *address=strtoul(addrp, NULL, 0);
      if(!(opt.flags & FLAG_QUIET)) {
        sprintf(_, "0x%08lx", (unsigned long)*address);
        //printf("# SMBIOS entry point at 0x%08lx\n", (unsigned long)*address);
      }
      ret=0;
      break;
    }
  }
  if(fclose(efi_systab)!=0)
    perror(filename);

  if(ret==EFI_NO_SMBIOS) {
    //fprintf(stderr, "%s: SMBIOS entry point missing\n", filename);
    sprintf(_, "missing");
  }
  return ret;
}

int submain(int argc, char * const argv[])
{
	int ret=0;                  /* Returned value */
	int found=0;
	size_t fp;
	int efi;
	u8 *buf;

  char _[2048]; bzero(_, 2048);

	if(sizeof(u8)!=1 || sizeof(u16)!=2 || sizeof(u32)!=4 || '\0'!=0)
	{
		fprintf(stderr, "%s: compiler incompatibility\n", argv[0]);
		exit(255);
	}

	/* Set default option values */
	//. opt.devmem=DEFAULT_MEM_DEV;
	//. opt.flags=0;

	if(parse_command_line(argc, argv)<0)
	{
		ret=2;
		goto exit_free;
	}

	if(opt.flags & FLAG_HELP)
	{
		print_help();
		goto exit_free;
	}

	if(opt.flags & FLAG_VERSION)
	{
		sprintf(_, "%s\n", VERSION);
		goto exit_free;
	}
	
	if(!(opt.flags & FLAG_QUIET))
		sprintf(_, "# dmidecode %s\n", VERSION);
	
	/* First try EFI (ia64, Intel-based Mac) */
	efi=address_from_efi(&fp, _);
	switch(efi)
	{
		case EFI_NOT_FOUND:
			goto memory_scan;
		case EFI_NO_SMBIOS:
			ret=1;
			goto exit_free;
	}

	if((buf=mem_chunk(fp, 0x20, opt.devmem))==NULL)
	{
		ret=1;
		goto exit_free;
	}
	
	if(smbios_decode(buf, opt.devmem, NULL))
		found++;
	goto done;

memory_scan:
	/* Fallback to memory scan (x86, x86_64) */
	if((buf=mem_chunk(0xF0000, 0x10000, opt.devmem))==NULL)
	{
		ret=1;
		goto exit_free;
	}
	
	for(fp=0; fp<=0xFFF0; fp+=16)
	{
		if(memcmp(buf+fp, "_SM_", 4)==0 && fp<=0xFFE0)
		{
			if(smbios_decode(buf+fp, opt.devmem, NULL))
				found++;
			fp+=16;
		}
		else if(memcmp(buf+fp, "_DMI_", 5)==0)
		{
			if (legacy_decode(buf+fp, opt.devmem, NULL))
				found++;
		}
	}
	
done:
	free(buf);
	
	if(!found && !(opt.flags & FLAG_QUIET))
		catsprintf(buffer, "# No SMBIOS nor DMI entry point found, sorry.\n");

exit_free:
	//. free(opt.type);

	return ret;
}
