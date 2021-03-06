/*
 * Copyright (c) 2014-2015 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <asm/byteordering.h>

#include "manifest.h"

#include <phabos/utils.h>

// XXX phabos porting
#define gb_debug(x...)
#define gb_info(x...)
#define gb_error(x...)
#define ALIGN(x) (x + 3) & ~3

struct greybus {
    struct list_head cports;
    struct greybus_driver *drv;
};

struct greybus g_greybus = {
    .cports = LIST_INIT(g_greybus.cports),
};

static unsigned char all_modules_manifest[] = {
  0xb4, 0x00, 0x00, 0x01, 0x08, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x00,
  0x14, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x50, 0x72, 0x6f, 0x6a, 0x65, 0x63,
  0x74, 0x20, 0x41, 0x72, 0x61, 0x00, 0x00, 0x00, 0x14, 0x00, 0x02, 0x00,
  0x0b, 0x02, 0x41, 0x6c, 0x6c, 0x20, 0x4d, 0x6f, 0x64, 0x75, 0x6c, 0x65,
  0x73, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x08, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00,
  0x03, 0x00, 0x01, 0x02, 0x08, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x03,
  0x08, 0x00, 0x04, 0x00, 0x05, 0x00, 0x01, 0x04, 0x08, 0x00, 0x04, 0x00,
  0x06, 0x00, 0x01, 0x06, 0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x01, 0x07,
  0x08, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x08, 0x08, 0x00, 0x04, 0x00,
  0x09, 0x00, 0x01, 0x09, 0x08, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x01, 0x0a,
  0x08, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x01, 0x0b, 0x08, 0x00, 0x04, 0x00,
  0x0c, 0x00, 0x01, 0x10, 0x08, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x01, 0x11,
  0x08, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x01, 0x12, 0x08, 0x00, 0x04, 0x00,
  0x0f, 0x00, 0x01, 0x13, 0x08, 0x00, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00
};

struct manifest_file manifest_files[] = { {2, &all_modules_manifest[0]}, }; // FIXME port
static int g_device_id;

void foreach_manifest(manifest_handler handler)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(manifest_files); i++)
        handler(manifest_files[i].bin, manifest_files[i].id, i);
}

static void *alloc_cport(void)
{
    struct gb_cport *gb_cport;

    gb_cport = malloc(sizeof(struct gb_cport));
    if (!gb_cport)
        return NULL;

    list_add(&g_greybus.cports, &gb_cport->list);
    return gb_cport;
}

static void free_cport(int cportid)
{
    struct gb_cport *gb_cport;

    list_foreach_safe(&g_greybus.cports, iter) {
        gb_cport = list_entry(iter, struct gb_cport, list);
        if (gb_cport->id == cportid) {
            list_del(iter);
            free(gb_cport);
        }
    }
}

/*
 * Validate the given descriptor.  Its reported size must fit within
 * the number of bytes reamining, and it must have a recognized
 * type.  Check that the reported size is at least as big as what
 * we expect to see.  (It could be bigger, perhaps for a new version
 * of the format.)
 *
 * Returns the number of bytes consumed by the descriptor, or a
 * negative errno.
 */
static int identify_descriptor(struct greybus_descriptor *desc, size_t size,
                               int release)
{
    struct greybus_descriptor_header *desc_header = &desc->header;
    size_t expected_size;
    size_t desc_size;
    struct gb_cport *cport;

    if (size < sizeof(*desc_header)) {
        gb_error("manifest too small\n");
        return -EINVAL;         /* Must at least have header */
    }

    desc_size = (int)le16toh(desc_header->size);
    if ((size_t) desc_size > size) {
        gb_error("descriptor too big\n");
        return -EINVAL;
    }

    /* Descriptor needs to at least have a header */
    expected_size = sizeof(*desc_header);

    switch (desc_header->type) {
    case GREYBUS_TYPE_STRING:
        expected_size += sizeof(struct greybus_descriptor_string);
        expected_size += desc->string.length;

        /* String descriptors are padded to 4 byte boundaries */
        expected_size = ALIGN(expected_size);
        break;
    case GREYBUS_TYPE_INTERFACE:
        expected_size += sizeof(struct greybus_descriptor_interface);
        break;
    case GREYBUS_TYPE_BUNDLE:
        expected_size += sizeof(struct greybus_descriptor_bundle);
        break;
    case GREYBUS_TYPE_CPORT:
        expected_size += sizeof(struct greybus_descriptor_cport);
        if (desc_size >= expected_size) {
            if (!release) {
                cport = alloc_cport();
                cport->id = desc->cport.id;
                cport->protocol = desc->cport.protocol_id;
                cport->device_id = g_device_id;
                gb_debug("cport_id = %d\n", cport->id);
            } else {
                free_cport(desc->cport.id);
            }
        }
        break;
    case GREYBUS_TYPE_INVALID:
    default:
        gb_error("invalid descriptor type (%hhu)\n", desc_header->type);
        return -EINVAL;
    }

    if (desc_size < expected_size) {
        gb_error("%d: descriptor too small (%zu < %zu)\n",
                 desc_header->type, desc_size, expected_size);
        return -EINVAL;
    }

    /* Descriptor bigger than what we expect */
    if (desc_size > expected_size) {
        gb_error("%d descriptor size mismatch (want %zu got %zu)\n",
                 desc_header->type, expected_size, desc_size);
    }

    return desc_size;
}

bool _manifest_parse(void *data, size_t size, int release)
{
    struct greybus_manifest *manifest = data;
    struct greybus_manifest_header *header = &manifest->header;
    struct greybus_descriptor *desc;
    uint16_t manifest_size;

    if (!release) {
        /* we have to have at _least_ the manifest header */
        if (size <= sizeof(manifest->header)) {
            gb_error("short manifest (%zu)\n", size);
            return false;
        }

        /* Make sure the size is right */
        manifest_size = le16toh(header->size);
        if (manifest_size != size) {
            gb_error("manifest size mismatch %zu != %hu\n", size,
                     manifest_size);
            return false;
        }

        /* Validate major/minor number */
        if (header->version_major > GREYBUS_VERSION_MAJOR) {
            gb_error("manifest version too new (%hhu.%hhu > %hhu.%hhu)\n",
                     header->version_major, header->version_minor,
                     GREYBUS_VERSION_MAJOR, GREYBUS_VERSION_MINOR);
            return false;
        }
    }

    /* OK, find all the descriptors */
    desc = (struct greybus_descriptor *)(header + 1);
    size -= sizeof(*header);
    while (size) {
        int desc_size;

        desc_size = identify_descriptor(desc, size, release);
        if (desc_size <= 0)
            return false;
        desc = (struct greybus_descriptor *)((char *)desc + desc_size);
        size -= desc_size;
    }

    return true;
}

/*
 * Parse a buffer containing a interface manifest.
 *
 * If we find anything wrong with the content/format of the buffer
 * we reject it.
 *
 * The first requirement is that the manifest's version is
 * one we can parse.
 *
 * We make an initial pass through the buffer and identify all of
 * the descriptors it contains, keeping track for each its type
 * and the location size of its data in the buffer.
 *
 * Returns true if parsing was successful, false otherwise.
 */
bool manifest_parse(void *data, size_t size)
{
    return _manifest_parse(data, size, 0);
}

bool manifest_release(void *data, size_t size)
{
    return _manifest_parse(data, size, 1);
}

static int get_interface_id(char *fname)
{
    char *iid_str;
    int iid = 0;
    char tmp[256];

    strcpy(tmp, fname);
    iid_str = strtok(tmp, "-");
    if (!strncmp(iid_str, "IID", 3))
        iid = strtol(fname + 4, NULL, 0);

    return iid;
}

void *get_manifest_blob(void)
{
    return manifest_files[0].bin;
}

void parse_manifest_blob(void *manifest)
{
    struct greybus_manifest_header *mh = manifest;

    manifest_parse(mh, le16toh(mh->size));
}

void release_manifest_blob(void *manifest)
{
    struct greybus_manifest_header *mh = manifest;

    manifest_release(mh, le16toh(mh->size));
}

void enable_manifest(char *name, void *manifest, int device_id)
{
    if (!manifest) {
        manifest = get_manifest_blob();
    }

    if (manifest) {
        g_device_id = device_id;
        parse_manifest_blob(manifest);
        int iid = get_interface_id(name);
        if (iid > 0) {
            gb_info("%s interface inserted\n", name);
        } else {
            gb_error("invalid interface ID, no hotplug plug event sent\n");
        }
    } else {
        gb_error("missing manifest blob, no hotplug event sent\n");
    }
}

void disable_manifest(char *name, void *priv, int device_id)
{
    void *manifest;

    manifest = get_manifest_blob();
    if (manifest) {
        g_device_id = device_id;
        release_manifest_blob(manifest);
    }
}

struct list_head *get_manifest_cports(void)
{
    return &g_greybus.cports;
}

int get_manifest_size(void)
{
    struct greybus_manifest_header *mh = get_manifest_blob();

    return mh ? le16_to_cpu(mh->size) : 0;
}
