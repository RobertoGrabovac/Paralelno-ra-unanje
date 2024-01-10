#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <limits.h>

typedef struct synchronize_structure {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int counter;
    int limit;
} synchronize_type;

synchronize_type *synchronize_all_vars;

void synchronize(synchronize_type *synchronize_vars) {
    pthread_mutex_lock(&synchronize_vars->mutex);
    synchronize_vars->counter++;
    if (synchronize_vars->counter == synchronize_vars->limit) {
        synchronize_vars->counter = 0;
        pthread_cond_broadcast(&synchronize_vars->cond);
    }
    else {
        pthread_cond_wait(&synchronize_vars->cond,
        &synchronize_vars->mutex);
    }
    pthread_mutex_unlock(&synchronize_vars->mutex);
}

typedef void(*working_function_t)(int, void*);
working_function_t function;

static void* argument;

struct thread_data {
    int rank;
};

struct thread_data **wd;

int stopped = 0;

static void *waiting_thread_function(void *arg) {
    struct thread_data *thread_data = (struct thread_data*) arg;
    const int rank = thread_data->rank;

    for (;;) {
        synchronize(synchronize_all_vars);
        if (stopped)
            break;
        else {
            function(rank, argument);
            synchronize(synchronize_all_vars);
        }
    }
    return NULL;
}

typedef struct parallel_search_structure {
    int num_threads; // total number of threads (including the main thread)
    int n; // number of elements in array x
    double y; // element whose place in the array x we are looking for
    double *x; // array x pointer
    int *lim; // array with two elements pointer
    int *c; // array c pointer
    int *q; // array q pointer
    int *found; // pointer on the variable which signals that solution has been found
    int *res_i; // pointer on the variable which stores solution
} parallel_search_type;

parallel_search_type *parallel_search_arg;

int p = 5; // number of working threads --> does not include the main thread with rank = 0

void parallel_search(int rank, void *arg) {
    parallel_search_type* parallel_search_arg = (struct parallel_search_structure*) arg;
    int num_threads = parallel_search_arg->num_threads;
    int n = parallel_search_arg->n;
    double y = parallel_search_arg->y;
    double *x = parallel_search_arg->x;
    int *l = parallel_search_arg->lim + 0;
    int *r = parallel_search_arg->lim + 1;
    int *c = parallel_search_arg->c;
    int *q = parallel_search_arg->q;
    int *found = parallel_search_arg->found;
    int *res_i = parallel_search_arg->res_i;

    if (rank == 0) {
        *l = 0, *r = n;
        c[0] = y <= x[0] ? 1 : 0; c[num_threads + 1] = 1;
        *found = 0;
    }
    synchronize(synchronize_all_vars);

    // special case: if c[0] == 1 <=> y < x[0] or y == x[0] <==> *res_i = -1 or *res_i = 0 
    if (c[0] == 1 && rank == 0) {
        *found = 1;
        *res_i = y == x[0] ? 0 : -1;
    } else if (rank == 0 && y >= x[n - 1]) {
        *found = 1;
        *res_i = n - 1;
    }
    synchronize(synchronize_all_vars);
    if (*found)
        return;

    int k = -1;
    int index = -1;
    
    while (*r - *l > num_threads && *found == 0) {
        int size = floor((*r - *l) / (num_threads + 1)); 
        if (rank == 0)
            q[0] = *l, q[num_threads + 1] = *r;
        synchronize(synchronize_all_vars);
        q[rank + 1] = *l + (rank + 1) * size;

        k = q[rank + 1];
        if (y == x[k]) {
            index = k;
            *found = 1;
            *res_i = index;  
        } else if (*found == 0) {
            if (y > x[k])
                c[rank + 1] = 0;
            else 
                c[rank + 1] = 1;
        }
        synchronize(synchronize_all_vars);
        if (c[rank + 1] < c[rank + 2] && *found == 0){
            *l = k;
            *r = q[rank + 2];
        }

        if (rank == 0 && c[0] < c[1] && *found == 0) {
            *l = q[0];
            *r = q[1];
        }
        synchronize(synchronize_all_vars);
    }
    
    if (rank + 1 <= *r - *l && *found == 0) {
        if (y == x[*l + rank + 1]) {
            index = *l + rank + 1;
            *found = 1;
            *res_i = index; 
        }
        if (y > x[*l + rank + 1])
            c[rank + 1] = 0;
        else
            c[rank + 1] = 1;

        synchronize(synchronize_all_vars);
        if (c[rank] < c[rank + 1] && *found == 0) {
            index = *l + rank;
            *found = 1;
            *res_i = index; 
        }
    } else 
        synchronize(synchronize_all_vars);
    
    return;
}

int main (void) {
    int max_num_threads = p + 1;
    int i;

    synchronize_all_vars = (synchronize_type*) malloc(sizeof(synchronize_type));
    pthread_mutex_init(&synchronize_all_vars->mutex, 0);
    pthread_cond_init(&synchronize_all_vars->cond, 0);
    synchronize_all_vars->limit = max_num_threads;
    synchronize_all_vars->counter = 0;

    parallel_search_type *parallel_search_arg = (parallel_search_type*) malloc(sizeof(parallel_search_type));

    pthread_t *threads = (pthread_t*) malloc(p*sizeof(pthread_t));
    wd = (struct thread_data**) malloc(p*sizeof(struct thread_data*));
    for ( i = 0; i < p; i++ )
        wd[i] = (struct thread_data*) malloc(sizeof(struct thread_data*));

    stopped = 0;

    for (i = 0; i < p; i++) {
        wd[i]->rank = i+1; // main_thread->rank = 0
        pthread_create(&threads[i], 0, waiting_thread_function, (void*) wd[i]);
    }

    // creating data
    int num_threads = max_num_threads;
    int n = 15;
    double y = 19;
    double *x = (double*)malloc((n) * sizeof(double));
    for (i = 0; i < n; i++)
        x[i] = 2 * (i + 1);
    //x[n] = INT_MAX;
    int *lim = (int*)malloc(2 * sizeof(int));
    //lim[0] = 0, lim[1] = n; // lim[0] <--> left side; lim[1] <--> right side
    int *c = (int*)malloc((num_threads + 2) * sizeof(int));
    //c[0] = y <= x[0] ? 1 : 0; c[num_threads + 1] = 1;
    int *q = (int*)malloc((num_threads + 2) * sizeof(int));
    int *found = (int*)malloc(sizeof(int));
    //*found = 0;
    int *res_i = (int*)malloc(sizeof(int));

    // defining parrallel_search_arg structure 
    parallel_search_arg->num_threads = num_threads;
    parallel_search_arg->n = n;
    parallel_search_arg->y = y;
    parallel_search_arg->x = x;
    parallel_search_arg->lim = lim;
    parallel_search_arg->c = c;
    parallel_search_arg->q = q;
    parallel_search_arg->found = found;
    parallel_search_arg->res_i = res_i;

    function = parallel_search;
    argument = parallel_search_arg;

    // awake working threads
    synchronize(synchronize_all_vars);

    // main thread is also working thread with rank = 0
    parallel_search(0, argument);
    
    // synchronising parallel search  
    synchronize(synchronize_all_vars);

    // signal to working threads that they should stop working 
    stopped = 1;
    synchronize(synchronize_all_vars);
    
    for ( i = 0; i < p; i++ ) 
        pthread_join(threads[i], 0);
    
    printf("Index of %f: %d\n", y, *res_i);
    
    for ( i = 0; i < p; i++ )
        free(wd[i]);
    free(wd);
    free(threads);
    free(parallel_search_arg);
    free(synchronize_all_vars);

    free(x);
    free(lim);
    free(c);
    free(q);
    free(found);
    free(res_i);

    return 0;
}