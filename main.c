#include "liblab/include/compfuncs.h"
#include "liblab/include/trialfuncs.h"

#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

typedef struct {
    int status;
    double result;
} computation_result;

void print_computation_result(computation_result* result) {
    if(result->status == COMPFUNC_SUCCESS) {
        printf("Operation succeeded with result %f\n", result->result);
    } else {
        printf("Operation terminated with status %s\n", symbolic_status(result->status));
    }
}

void populate_pollfds(struct pollfd *fdtab, int first_pipe[2], int second_pipe[2]) {
    fdtab[0].fd = first_pipe[0];
    fdtab[0].events = POLLIN;
    fdtab[0].revents = 0;

    fdtab[1].fd = second_pipe[0];
    fdtab[1].events = POLLIN;
    fdtab[1].revents = 0;

    fdtab[2].fd = first_pipe[1];
    fdtab[2].events = POLLOUT;
    fdtab[2].revents = 0;

    fdtab[3].fd = second_pipe[1];
    fdtab[3].events = POLLOUT;
    fdtab[3].revents = 0;
}

int compfunc_executor(struct pollfd *fdtab, compfunc_status_t(*op)(int, double*), int index, int x) {
    while(true) {
        int retpol = poll(fdtab + index + 2, 1, -1);

        if(retpol > 0) {
            computation_result result = {0, 0};
            result.status = op(x, &result.result);
            write(fdtab[index + 2].fd, &result, sizeof(computation_result));

            if(result.status == COMPFUNC_SUCCESS) {
                return 0;
            }

            if(result.status == COMPFUNC_HARD_FAIL) {
                return -1;
            }
        }
    }
}

int handle_data_from_worker_process(struct pollfd *fdtab, computation_result *op, int index) {
    read(fdtab[index].fd, &op[index], sizeof(computation_result));
    print_computation_result(&op[index]);
    
    if(op[index].status == COMPFUNC_HARD_FAIL) {
        printf("Hard-fail in process #%d. Terminating...\n", index + 1);
        return -1;
    }

    if(op[index].status == COMPFUNC_SOFT_FAIL) {
        puts("Soft-fail. Continuing");
        return 0;
    }

    return 0;
}

int main() {
    double x = 0;

    printf("Please enter the value for x: ");
    scanf(" %lf", &x);

    int pipes[4] = {};
    pipe(pipes);
    pipe(pipes + 2);

    struct pollfd fdtab[4] = {};
    populate_pollfds(fdtab, pipes, pipes + 2);

    computation_result op[2] = {};
    op[0].status = op[1].status = COMPFUNC_SOFT_FAIL; // Main loop will immediately terminate if instead set to COMPFUNC_SUCCESS

    int first_pid = fork();

    if(first_pid == 0) {
        compfunc_executor(fdtab, &trial_g_fmul, 0, x);
        return 0;
    }

    int second_pid = fork();
    
    if(second_pid == 0) {
        compfunc_executor(fdtab, &trial_f_fmul, 1, x);
        return 0;
    }

    while(true) {
        if(op[0].status == COMPFUNC_SUCCESS && op[1].status == COMPFUNC_SUCCESS) break;

        int retpol = poll(fdtab, 2, 5000);

        if(retpol > 0) {
            if(fdtab[0].revents & POLLIN) {
                if(handle_data_from_worker_process(fdtab, op, 0))
                    return 0;

                continue;
            }

            if(fdtab[1].revents & POLLIN) {
                if(handle_data_from_worker_process(fdtab, op, 1))
                    return 0;

                continue;
            }

            puts("Unexpected polling result. This is an implementation bug.");
            break;
        }

        if(retpol == 0) {
            puts("Poll timed out. Do you want to cancel the computation? [y/n]");
            char ch = 0;
            scanf("%*[^\n]"); // Should fix trailing whitespaces
            scanf(" %c", &ch);
            if(ch == 'n') {
                continue;
            }

            if(ch != 'y') {
                puts("Unexpected input. Halting...");
            }

            return 0;
        }

        if(retpol < 0) {
            puts("Internal poll error. This is an implementation bug.");
            return 0;
        }
    }

    double result = op[0].result + op[1].result;
    printf("The result of trial_g_fmul(%lf) + trial_f_fmul(%lf) = %lf\n", x, x, result);
}

