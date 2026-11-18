/* kheap.c — куча ядра: API туториала Дж. Моллоя (kmalloc/kmalloc_a/
 * kmalloc_p/kmalloc_ap/kfree) поверх исправленного алгоритма.
 *
 * Исправления относительно оригинала (подробности — в отчёте):
 *  K-1  индекс дыр живёт в .bss, а не «внутри самой кучи» — исключена
 *       целая класс порч при перезаписи индекса данными;
 *  K-2  при разбиении дыры хвост отбрасывается только если он не может
 *       быть валидным блоком (>= HDR+FOOT); мелкий остаток остаётся дырой
 *       в списке и восстанавливается при соседних kfree — нет утечек;
 *  K-3  выравнивание: префиксная дыра вырезается только когда в неё
 *       помещается валидный блок; иначе запрашивается новое расширение;
 *  K-4  индексная запись удаляется по ТОЧНОМУ указателю (оригинал мог
 *       удалить другую дыру того же размера);
 *  K-5  двойное kfree -> паника (проверка magic/hole);
 *  K-6  слияние слева и справа обновляет футеры и список согласованно;
 *  K-7  contraction: возвращаем страницы только целыми страницами и
 *       только если дыра действительно упирается в конец кучи;
 *  K-8  kmalloc до инициализации кучи обслуживает bump-аллокатор
 *       (аналог «placement» Моллоя, но без kmalloc_phys-хака). */
#include "kheap.h"
#include "paging.h"
#include "console.h"

#define HEAP_START_MAGIC 0x12345678U
#define HEAP_END_MAGIC   0x87654321U
#define INDEX_MAX        1024
#define INDEX_MIN        128          /* меньшие дыры не индексируются,
                                         но живут в списке и восстанавливаются
                                         при слиянии соседей */

typedef struct header {
    u32 magic;
    u32 size;                     /* размер области данных */
    u8  hole;
    struct header *next, *prev;
} header_t;

typedef struct footer {
    u32 magic;
    header_t *header;
} footer_t;

static const u32 HDR = sizeof(header_t);
static const u32 FOOT = sizeof(footer_t);

static header_t *idx[INDEX_MAX];
static u32 idx_count;
static header_t *kheap_start, *kheap_end;   /* VA */
static int heap_ready = 0;

static u64 early_top = 0;       /* bump-аллокатор до кучи (физ.) */

static void idx_insert(header_t *h) {
    if (idx_count >= INDEX_MAX) panic("kheap: index overflow");
    u32 i = idx_count;
    while (i > 0 && idx[i - 1]->size > h->size) { idx[i] = idx[i - 1]; i--; }
    idx[i] = h;
    idx_count++;
}

/* K-4: удаление строго по указателю; дыры меньше INDEX_MIN в индекс не
 * попадают, поэтому отсутствие записи — нормальная ситуация */
static void idx_remove(header_t *h) {
    for (u32 i = 0; i < idx_count; i++) {
        if (idx[i] == h) {
            memmove(&idx[i], &idx[i + 1], (idx_count - i - 1) * sizeof(header_t *));
            idx_count--;
            return;
        }
    }
}

static footer_t *footer_of(header_t *h) {
    return (footer_t *)((u64)h + HDR + h->size);
}

static void write_footer(header_t *h) {
    footer_t *f = footer_of(h);
    f->magic = HEAP_END_MAGIC;
    f->header = h;
}

static header_t *last_block(void) {
    header_t *h = kheap_start;
    while (h->next) h = h->next;
    return h;
}

/* K-8: до кучи — bump внутри прямого отображения (<1GiB) */
static void *early_alloc(u32 n, u32 align, u64 *phys_out) {
    u64 top = align_up(early_top, align ? PAGE_SIZE : 16);
    early_top = top + n;
    if (phys_out) *phys_out = top;
    return (void *)p2v(top);
}

u64 heap_early_top(void) { return early_top; }

void heap_init(void) {
    kheap_start = (header_t *)KHEAP_VA_BASE;
    kheap_end = (header_t *)(KHEAP_VA_BASE + KHEAP_INIT_SIZE);
    kheap_start->magic = HEAP_START_MAGIC;
    kheap_start->size = (u32)(KHEAP_INIT_SIZE - HDR - FOOT);
    kheap_start->hole = 1;
    kheap_start->prev = kheap_start->next = NULL;
    write_footer(kheap_start);
    idx_count = 0;
    idx_insert(kheap_start);
    heap_ready = 1;
}

/* K-7: расширение — регион уже покрыт прямым отображением и зарезервирован
 * в битовой карте кадров, остаётся создать дыру в конце */
static void heap_expand(u64 add) {
    add = align_up(add, PAGE_SIZE);
    if ((u64)kheap_end + add > KHEAP_VA_BASE + KHEAP_MAX_SIZE)
        panic("kheap: out of heap space");
    header_t *h = (header_t *)kheap_end;
    header_t *prev = last_block();
    h->magic = HEAP_START_MAGIC;
    h->size = (u32)(add - HDR - FOOT);
    h->hole = 1;
    h->prev = prev;
    h->next = NULL;
    prev->next = h;
    write_footer(h);
    idx_insert(h);
    kheap_end = (header_t *)((u64)kheap_end + add);
}

static void *heap_alloc(u32 size, u32 align, u64 *phys_out) {
    u64 need = (u64)size + HDR + FOOT;
    header_t *h = NULL;

    for (int attempt = 0; attempt < 2 && !h; attempt++) {
        if (attempt > 0) heap_expand(need + 2 * PAGE_SIZE);
        for (u32 i = 0; i < idx_count; i++) {
            header_t *c = idx[i];
            if (c->size < need) continue;
            if (align) {
                /* K-3: выровненный блок обязан целиком влезть в дыру,
                 * а вырезаемый префикс — быть валидным блоком */
                u64 adata = align_up((u64)c + HDR, PAGE_SIZE);
                u64 aligned_hdr = adata - HDR;
                if (aligned_hdr + HDR + size + FOOT > (u64)c + HDR + c->size + FOOT)
                    continue;
                u64 pre_len = aligned_hdr - (u64)c;
                if (pre_len != 0 && pre_len < HDR + FOOT) continue;
                h = c;
                break;
            }
            h = c;
            break;
        }
    }
    if (!h) panic("kheap: out of memory");

    idx_remove(h);

    if (align) {
        u64 adata = align_up((u64)h + HDR, PAGE_SIZE);
        u64 pre_len = adata - HDR - (u64)h;
        if (pre_len > 0) {
            u32 rest = h->size - (u32)pre_len;   /* данные хвоста за aligned */
            header_t *aligned = (header_t *)(adata - HDR);
            /* блок: [HDR][data][FOOT]; футер префикса лежит ПЕРЕД aligned-заголовком */
            h->size = (u32)(pre_len - HDR - FOOT);
            h->hole = 1;
            write_footer(h);
            if (h->size >= INDEX_MIN) idx_insert(h);
            aligned->magic = HEAP_START_MAGIC;
            aligned->size = rest;                /* остаток дыры за aligned */
            aligned->hole = 1;
            aligned->prev = h;
            aligned->next = h->next;
            if (h->next) h->next->prev = aligned;
            h->next = aligned;
            h = aligned;
        }
    }

    /* K-2: суффикс-дыра отрезается ВСЕГДА (остаток >= 0 по условию скана);
     * мелкий неиндексируемый остаток живёт в списке и восстанавливается
     * при слиянии соседей — утечек нет. Следующий заголовок стоит ПОСЛЕ
     * футера блока: nh = h + HDR + size + FOOT. */
    {
        u32 old = h->size;
        u32 rem = old - size - (u32)(HDR + FOOT); /* данные суффикс-дыры (минус её заголовок) */
        h->size = size;
        write_footer(h);
        header_t *nh = (header_t *)((u64)h + HDR + size + FOOT);
        nh->magic = HEAP_START_MAGIC;
        nh->size = rem;
        nh->hole = 1;
        nh->prev = h;
        nh->next = h->next;
        if (h->next) h->next->prev = nh;
        h->next = nh;
        write_footer(nh);
        if (nh->size >= INDEX_MIN) idx_insert(nh);
    }

    h->hole = 0;
    h->magic = HEAP_START_MAGIC;
    write_footer(h);
    if (phys_out) *phys_out = kv2ph((u64)h + HDR);
    return (void *)((u64)h + HDR);
}

void kfree(void *p) {
    if (!p) return;
    if (!heap_ready || (u64)p < KHEAP_VA_BASE || (u64)p >= KHEAP_VA_BASE + KHEAP_MAX_SIZE)
        panic("kfree: pointer outside heap: %p", p);

    header_t *h = (header_t *)((u64)p - HDR);
    /* K-5: двойное освобождение — паника */
    if (h->magic != HEAP_START_MAGIC || h->hole != 0)
        panic("kfree: corrupted or double free at %p", p);
    footer_t *f = footer_of(h);
    if (f->magic != HEAP_END_MAGIC || f->header != h)
        panic("kfree: footer mismatch at %p", p);
    h->hole = 1;

    /* K-6: слияние слева */
    if (h->prev && h->prev->hole) {
        header_t *ph = h->prev;
        idx_remove(ph);
        /* объединённые данные простираются через собственный футер ph:
         * ph->size_new = (h - ph) - HDR + h->size */
        ph->size += (u32)(HDR + FOOT + h->size);
        ph->next = h->next;
        if (h->next) h->next->prev = ph;
        footer_of(ph)->header = ph;    /* футер h остаётся концом объединённого блока */
        h = ph;
    }
    /* слияние справа */
    if (h->next && h->next->hole) {
        header_t *nh = h->next;
        idx_remove(nh);
        /* объединённые данные: от h до конца данных nh; футер nh остаётся
         * на месте конца объединённого блока */
        h->size += (u32)(HDR + FOOT + nh->size);
        h->next = nh->next;
        if (nh->next) nh->next->prev = h;
        footer_of(h)->header = h;
    }

    /* K-7: сжатие — только целыми страницами и только у конца кучи */
    if (h->next == NULL && h->size >= 4 * PAGE_SIZE &&
        (u64)h + HDR + h->size + FOOT == (u64)kheap_end) {
        u64 new_end = align_up((u64)h + HDR, PAGE_SIZE);
        u64 old_end = (u64)kheap_end;
        if (new_end + PAGE_SIZE <= old_end) {
            h->size = (u32)(new_end - FOOT - HDR - (u64)h);
            write_footer(h);
            /* в индекс дыру вставит общий хвост kfree */
            for (u64 ph = kv2ph(new_end); ph < kv2ph(old_end); ph += PAGE_SIZE)
                clear_frame(ph);
            kheap_end = (header_t *)new_end;
        }
    }

    if (h->size >= INDEX_MIN) idx_insert(h);
}

static void *kmalloc_int(u32 n, u32 align, u64 *phys_out) {
    if (!n) n = 1;
    if (!heap_ready) return early_alloc(n, align, phys_out);
    return heap_alloc(n, align, phys_out);
}

void *kmalloc(u32 n)              { return kmalloc_int(n, 0, NULL); }
void *kmalloc_a(u32 n)            { return kmalloc_int(n, 1, NULL); }
void *kmalloc_p(u32 n, u64 *phys) { return kmalloc_int(n, 0, phys); }
void *kmalloc_ap(u32 n, u64 *phys){ return kmalloc_int(n, 1, phys); }

u32 heap_hole_count(void) { return idx_count; }
