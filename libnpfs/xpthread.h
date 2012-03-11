/* pthread wrappers */
#define xpthread_mutex_lock(a) do { \
    int pthread_mutex_lock_result = pthread_mutex_lock(a); \
    NP_ASSERT (pthread_mutex_lock_result == 0); \
} while (0)
#define xpthread_mutex_unlock(a) do { \
    int pthread_mutex_unlock_result = pthread_mutex_unlock(a); \
    NP_ASSERT (pthread_mutex_unlock_result == 0); \
} while (0)
#define xpthread_cond_wait(a,b) do { \
    int pthread_cond_wait_result = pthread_cond_wait(a,b); \
    NP_ASSERT (pthread_cond_wait_result == 0); \
} while (0)
#define xpthread_cond_timedwait(a,b,c) do { \
    int pthread_cond_timedwait_result = pthread_cond_timedwait(a,b,c); \
    NP_ASSERT (pthread_cond_timedwait_result == 0); \
} while (0)
#define xpthread_cond_broadcast(a) do { \
    int pthread_cond_broadcast_result = pthread_cond_broadcast(a); \
    NP_ASSERT (pthread_cond_broadcast_result == 0); \
} while (0)
#define xpthread_cond_signal(a) do { \
    int pthread_cond_signal_result = pthread_cond_signal(a); \
    NP_ASSERT (pthread_cond_signal_result == 0); \
} while (0)
