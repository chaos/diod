typedef PVOID pthread_attr_t;
typedef PVOID pthread_mutexattr_t;
typedef PVOID pthread_condattr_t;
typedef struct {
	INT init;
	HANDLE handle;
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }

typedef struct {
	UINT waiters;
	INT broadcast;
	pthread_mutex_t lock;
	HANDLE queue;
	HANDLE done;
} pthread_cond_t;

typedef struct {
	INT detached;
	HANDLE handle;
	void *(*run)(void *);
	void *arg;
	PVOID retval;
} *pthread_t;

typedef struct {
	UINT init;
} pthread_once_t;
#define PTHREAD_ONCE_INIT  {0}

typedef struct {
	DWORD tlsidx;
	void (*destroy)(void*);
} pthread_key_t;

int pthread_mutex_init(pthread_mutex_t *mux, const pthread_mutexattr_t *dummy);
int pthread_mutex_destroy(pthread_mutex_t *mux);
int pthread_mutex_lock(pthread_mutex_t *mux);
int pthread_mutex_unlock(pthread_mutex_t *mux);
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *dummy);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mux);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_create(pthread_t *thr, const pthread_attr_t *dummy, void *(*run)(void*), void *arg);
int pthread_join(pthread_t thr, void **retval);
int pthread_detach(pthread_t dummy);
int pthread_once(pthread_once_t *once, void (*func)(void));
int pthread_key_create(pthread_key_t *key, void (*destroy)(void*));
void * pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *val);
pthread_t pthread_self(void);

