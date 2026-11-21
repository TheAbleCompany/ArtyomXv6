/* initrd.c — RAM-диск из туториала Моллоя, с исправлениями:
 *  I-1  проверяются magic образа, границы каждого файла и сам размер
 *       (оригинал доверял заголовку как есть);
 *  I-2  read возвращает 0 за концом файла вместо чтения чужой памяти;
 *  I-3  readdir/finddir возвращают корректные узлы, а не «висячие» ссылки. */
#include "initrd.h"
#include "kheap.h"
#include "console.h"

static u64 image_base;
static u32 image_size __attribute__((unused));
static u32 nfiles;
static initrd_file_header_t *file_headers;
static fs_node_t *fs_nodes;

static u32 initrd_read(fs_node_t *node, u64 off, u32 len, u8 *buf) {
    if (node->inode >= nfiles) return 0;
    initrd_file_header_t *fh = &file_headers[node->inode];
    /* I-2: обрезаем за концом файла */
    if (off >= node->size) return 0;
    if (off + len > node->size) len = (u32)(node->size - off);
    u64 src = image_base + fh->offset + off;
    memcpy(buf, (const void *)src, len);
    return len;
}

static dirent_t *initrd_readdir(fs_node_t *node, u32 idx) {
    (void)node;
    if (idx >= nfiles) return NULL;
    static dirent_t de;
    de.ino = idx;
    memset(de.name, 0, sizeof(de.name));
    for (u32 i = 0; i < sizeof(de.name) - 1 && fs_nodes[idx].name[i]; i++)
        de.name[i] = fs_nodes[idx].name[i];
    return &de;
}

static fs_node_t *initrd_finddir(fs_node_t *node, const char *name) {
    (void)node;
    for (u32 i = 0; i < nfiles; i++)
        if (strcmp(fs_nodes[i].name, name) == 0)
            return &fs_nodes[i];
    return NULL;
}

fs_node_t *initrd_mount(u64 base_va, u32 size) {
    if (size < sizeof(initrd_header_t))
        panic("initrd.c",48,"initrd: image too small");
    initrd_header_t *h = (initrd_header_t *)base_va;
    if (h->magic != INITRD_MAGIC)
        panic("initrd.c",51,"initrd: bad magic 0x%x", h->magic);
    image_base = base_va;
    image_size = size;
    nfiles = h->nfiles;
    if (nfiles == 0 || nfiles > 64)
        panic("initrd.c",56,"initrd: bad file count %u", nfiles);

    file_headers = (initrd_file_header_t *)(base_va + sizeof(initrd_header_t));
    /* I-1: границы каждого файла */
    for (u32 i = 0; i < nfiles; i++) {
        u64 end = (u64)file_headers[i].offset + file_headers[i].length;
        if (end > size)
            panic("initrd.c",63,"initrd: file %u out of bounds", i);
    }

    fs_nodes = kmalloc(nfiles * sizeof(fs_node_t));
    for (u32 i = 0; i < nfiles; i++) {
        fs_node_t *n = &fs_nodes[i];
        memset(n, 0, sizeof(fs_node_t));
        u32 j = 0;
        for (; j < 63 && file_headers[i].name[j]; j++) n->name[j] = file_headers[i].name[j];
        n->name[j] = 0;
        n->flags = FS_FILE;
        n->inode = i;
        n->size = file_headers[i].length;
        n->read = initrd_read;
    }

    fs_node_t *root = kmalloc(sizeof(fs_node_t));
    memset(root, 0, sizeof(fs_node_t));
    root->name[0] = '/';
    root->flags = FS_DIRECTORY;
    root->readdir = initrd_readdir;
    root->finddir = initrd_finddir;
    return root;
}
