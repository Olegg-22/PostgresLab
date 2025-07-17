#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/lwlock.h"
#include "storage/latch.h"
#include "storage/shmem.h"
#include "utils/guc.h"
#include "utils/elog.h"
#include "utils/memutils.h"
#include "pgstat.h"

PG_MODULE_MAGIC;
#define CAPACITY 100

typedef struct
{
    int hour;
    int minute;
    int second;
    int absolute_sec;
} Time;

typedef struct
{
    bool is_temp_event;
    char string_time[256];
    char message[256];
    Time time;
} MySharedData;

typedef struct
{
    int size;
    MySharedData *data[CAPACITY];
    LWLock lock;
} Heap;

PG_FUNCTION_INFO_V1(my_schedule);

void my_schedule_main(Datum main_arg);

static void my_shmem_request(void);

void sort_heap_by_absolute_time(void);

static Heap *shared_heap = NULL;
static shmem_request_hook_type prev_shmem_request_hook = NULL;
static volatile sig_atomic_t got_sigterm = false;

static void swap(MySharedData **a, MySharedData **b)
{
    MySharedData *tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heapify_up(Heap *heap, int index)
{
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap->data[index]->time.absolute_sec < heap->data[parent]->time.absolute_sec) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void heapify_down(Heap *heap, int index)
{
    while (1) {
        int left = 2 * index + 1;
        int right = 2 * index + 2;
        int smallest = index;

        if (left < heap->size && heap->data[left]->time.absolute_sec < heap->data[smallest]->time.absolute_sec)
            smallest = left;
        if (right < heap->size && heap->data[right]->time.absolute_sec < heap->data[smallest]->time.absolute_sec)
            smallest = right;

        if (smallest != index) {
            swap(&heap->data[index], &heap->data[smallest]);
            index = smallest;
        } else {
            break;
        }
    }
}

static void heap_insert(Heap *heap, MySharedData *item)
{
    if (heap->size >= CAPACITY) {
        elog(ERROR, "Heap overflow");
        return;
    }
    heap->data[heap->size] = item;
    heapify_up(heap, heap->size);
    heap->size++;
}

static MySharedData *heap_pop(Heap *heap)
{
    MySharedData *min;
    if (heap->size == 0)
        return NULL;

    min = heap->data[0];
    heap->data[0] = heap->data[heap->size - 1];
    heap->size--;
    heapify_down(heap, 0);

    return min;
}

static void
my_sigterm_handler(SIGNAL_ARGS)
{
    int save_errno = errno;
    got_sigterm = true;
    errno = save_errno;
}

static void
my_shmem_request(void)
{
    if (prev_shmem_request_hook)
        prev_shmem_request_hook();

    RequestAddinShmemSpace(sizeof(Heap));

    for (int i = 0; i < 100; i++)
        RequestAddinShmemSpace(sizeof(MySharedData));
}

static void
InitializeSharedMemory(void)
{
    bool found;

    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

//    mySharedData = (MySharedData *) ShmemInitStruct("MySharedData", sizeof(MySharedData), &found);
    shared_heap = (Heap *) ShmemInitStruct("Heap", sizeof(Heap), &found);
    if (!found) {
        shared_heap->size = 0;
        LWLockInitialize(&shared_heap->lock, LWLockNewTrancheId());

        for (int i = 0; i < CAPACITY; i++) {
            bool item_found;
            char namebuf[32];
            snprintf(namebuf, sizeof(namebuf), "MySharedData%d", i);
            shared_heap->data[i] = (MySharedData *) ShmemInitStruct(namebuf, sizeof(MySharedData), &item_found);

            if (!item_found) {
                memset(shared_heap->data[i], 0, sizeof(MySharedData));
                shared_heap->data[i]->message[0] = '\0';
            }
        }
    }

    LWLockRelease(AddinShmemInitLock);
}


void
_PG_init(void)
{
    BackgroundWorker *worker;

    prev_shmem_request_hook = shmem_request_hook;
    shmem_request_hook = my_shmem_request;

    worker = (BackgroundWorker *) palloc0(sizeof(BackgroundWorker));
    snprintf(worker->bgw_name, BGW_MAXLEN, "bgworker_example");
    snprintf(worker->bgw_type, BGW_MAXLEN, "bgworker_example");
    worker->bgw_flags = BGWORKER_SHMEM_ACCESS;
    worker->bgw_start_time = BgWorkerStart_ConsistentState;
    worker->bgw_restart_time = BGW_NEVER_RESTART;
    snprintf(worker->bgw_library_name, MAXPGPATH, "my_schedule");
    snprintf(worker->bgw_function_name, BGW_MAXLEN, "my_schedule_main");
    worker->bgw_main_arg = (Datum) 0;
    worker->bgw_notify_pid = 0;

    RegisterBackgroundWorker(worker);
}

Datum
my_schedule(PG_FUNCTION_ARGS)
{
    text *string_status_event = PG_ARGISNULL(0) ? NULL : PG_GETARG_TEXT_P(0);
    text *string_time = PG_ARGISNULL(1) ? NULL : PG_GETARG_TEXT_P(1);
    text *string_message = PG_ARGISNULL(2) ? NULL : PG_GETARG_TEXT_P(2);
    bool data_is_correct = true;
    char buf[9];
    char buf_stat[5];

    if (!shared_heap) {
        bool found;
        shared_heap = (Heap *) ShmemInitStruct("Heap", sizeof(Heap), &found);

        if (!found)
            elog(ERROR, "Shared memory region 'Heap' not found (not initialized?)");
    }

    if (string_status_event) {
        int len = Min(VARSIZE_ANY_EXHDR(string_status_event), sizeof(buf_stat) - 1);
        memcpy(buf_stat, VARDATA_ANY(string_status_event), len);
        buf_stat[len] = '\0';

        if (strcmp(buf_stat, "temp") != 0 && strcmp(buf_stat, "absl") != 0) {
            data_is_correct = false;
            ereport(ERROR,
                    (errmsg("first parameter must be 'temp' or 'absl'")));
        }
    } else {
        data_is_correct = false;
    }
    if (string_time) {
        int hour, minute, second;
        int len = Min(VARSIZE_ANY_EXHDR(string_time), sizeof(buf) - 1);
        memcpy(buf, VARDATA_ANY(string_time), len);
        buf[len] = '\0';

        if (sscanf(buf, "%2d:%2d:%2d", &hour, &minute, &second) != 3 ||
            hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59) {
            data_is_correct = false;
            ereport(ERROR,
                    (errmsg("second parameter must be in format HH:MM:SS (00-23:00-59:00-59)")));
        }
    } else {
        data_is_correct = false;
    }


    if (data_is_correct) {
        int len;
        int hour, minute, second;
        Time event_time;

        LWLockAcquire(&shared_heap->lock, LW_EXCLUSIVE);

        shared_heap->data[shared_heap->size]->is_temp_event = strcmp(buf_stat, "temp") == 0 ? true : false;
        memcpy(shared_heap->data[shared_heap->size]->string_time, buf, sizeof(buf) - 1);

        len = Min(VARSIZE_ANY_EXHDR(string_message),
                  sizeof(shared_heap->data[shared_heap->size]->message) - 1);
        memcpy(shared_heap->data[shared_heap->size]->message, VARDATA_ANY(string_message), len);
        shared_heap->data[shared_heap->size]->message[len] = '\0';

        sscanf(buf, "%2d:%2d:%2d", &hour, &minute, &second);

        if (strcmp(buf_stat, "temp") == 0) {
            pg_time_t now = (pg_time_t) time(NULL);
            struct tm *now_tm = localtime(&now);

            now_tm->tm_hour += hour;
            now_tm->tm_min += minute;
            now_tm->tm_sec += second;

            mktime(now_tm);

            event_time.hour = now_tm->tm_hour;
            event_time.minute = now_tm->tm_min;
            event_time.second = now_tm->tm_sec;
            event_time.absolute_sec = now_tm->tm_hour * 3600 + now_tm->tm_min * 60 + now_tm->tm_sec;
        } else {
            event_time.hour = hour;
            event_time.minute = minute;
            event_time.second = second;
            event_time.absolute_sec = hour * 3600 + minute * 60 + second;
        }
        shared_heap->data[shared_heap->size]->time = event_time;

        heap_insert(shared_heap, shared_heap->data[shared_heap->size]);

        LWLockRelease(&shared_heap->lock);
    }

    PG_RETURN_VOID();
}


PGDLLEXPORT void
my_schedule_main(Datum main_arg)
{
    BackgroundWorkerUnblockSignals();
    pqsignal(SIGTERM, my_sigterm_handler);

    InitializeSharedMemory();

    while (!got_sigterm) {
        pg_time_t now;
        struct tm *now_tm;
        int now_sec;

        if (got_sigterm)
            break;

        now = (pg_time_t) time(NULL);
        now_tm = localtime(&now);
        now_sec = now_tm->tm_hour * 3600 + now_tm->tm_min * 60 + now_tm->tm_sec;

        LWLockAcquire(&shared_heap->lock, LW_EXCLUSIVE);

        if (shared_heap && shared_heap->size) {
            if (shared_heap->data[0]->time.absolute_sec <= now_sec) {
                elog(LOG, "AAAAAAAAAAAAAAAAAAAAA%s", shared_heap->data[0]->message);
                if (shared_heap->data[0]->is_temp_event) {
                    int hour, minute, second;
                    sscanf(shared_heap->data[0]->string_time, "%2d:%2d:%2d", &hour, &minute, &second);

                    now_tm->tm_hour += hour;
                    now_tm->tm_min += minute;
                    now_tm->tm_sec += second;

                    mktime(now_tm);

                    shared_heap->data[0]->time.hour = now_tm->tm_hour;
                    shared_heap->data[0]->time.minute = now_tm->tm_min;
                    shared_heap->data[0]->time.second = now_tm->tm_sec;
                    shared_heap->data[0]->time.absolute_sec =
                            now_tm->tm_hour * 3600 + now_tm->tm_min * 60 + now_tm->tm_sec;

                    heapify_down(shared_heap, 0);
                } else {
                    heap_pop(shared_heap);
                }
            }
        }

        LWLockRelease(&shared_heap->lock);

        pg_usleep(500000);
    }
}
