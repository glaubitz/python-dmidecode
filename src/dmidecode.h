/*
 * This file is part of the dmidecode project.
 *
 *   (C) 2005-2007 Jean Delvare <khali@linux-fr.org>
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
 */
#include <Python.h>

struct dmi_header {
  u8 type;
  u8 length;
  u16 handle;
  u8 *data;
};

PyObject *dmi_dump(struct dmi_header *h);
PyObject* dmi_decode(struct dmi_header *h, u16 ver);
int address_from_efi(size_t *address);
void to_dmi_header(struct dmi_header *h, u8 *data);
int smbios_decode_set_version(u8 *buf, const char *devmem, PyObject** pydata);
int smbios_decode(u8 *buf, const char *devmem, PyObject* pydata);
int legacy_decode_set_version(u8 *buf, const char *devmem, PyObject** pydata);
int legacy_decode(u8 *buf, const char *devmem, PyObject* pydata);

const char *dmi_string(const struct dmi_header *dm, u8 s);
const char *dmi_system_uuid(u8 *p);
PyObject *dmi_system_uuid_py(const u8 *p, u16 ver);
const char *dmi_chassis_type(u8 code);
int dmi_processor_frequency(const u8 *p);

int dump(const char *dumpfile);
int dumpling(u8 *buf, const char *devmem, u8 mode);