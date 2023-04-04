#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(void){
    // int num_procs = 3;
    // int deadline_value[3] = {18, 24, 21};
    // int exectime[3] = {5, 6, 4};

    // int parent_pid = getpid();

    // // Set the scheduling policy to EDF
    // deadline(parent_pid, 19);
    // exec_time(parent_pid, 4);
    // sched_policy(parent_pid, 0);

    // for(int i = 0; i < num_procs; i++)
    // {
    //     int cid = fork();
    //     if (cid != 0)
    //     {
    //         // Set the scheduling policy to EDF
    //         deadline(cid, deadline_value[i]);
    //         exec_time(cid, exectime[i]);
    //         sched_policy(cid, 0);
    //     }
    //     else
    //     {
    //         /*The XV6 kills the process if th exec time is completed*/
    //         while(1) {
                
    //         }
    //     }
    // }

    // while(1) {

    // }


    // int num_procs = 4;
    // int deadline_value[4] = {16, 14, 18, 21};
    // int exectime[4] = {4, 9, 6, 8};

    // int parent_pid = getpid();

    // // Set the scheduling policy to EDF
    // deadline(parent_pid, 23);
    // exec_time(parent_pid, 5);
    // sched_policy(parent_pid, 0);

    // for(int i = 0; i < num_procs; i++)
    // {
    //     int cid = fork();
    //     if (cid != 0)
    //     {
    //         // Set the scheduling policy to EDF
    //         deadline(cid, deadline_value[i]);
    //         exec_time(cid, exectime[i]);
    //         sched_policy(cid, 0);
    //     }
    //     else
    //     {
    //         /*The XV6 kills the process if th exec time is completed*/
    //         while(1) {
                
    //         }
    //     }
    // }

    // while(1) {

    // }

    int num_procs = 4;
    int deadline_value[4] = {20, 20, 24, 52};
    int exectime[4] = {8, 4, 5, 4};

    int parent_pid = getpid();

    // Set the scheduling policy to EDF
    deadline(parent_pid, 19);
    exec_time(parent_pid, 6);
    sched_policy(parent_pid, 0);

    for(int i = 0; i < num_procs; i++)
    {
        int cid = fork();
        if (cid != 0)
        {
            // Set the scheduling policy to EDF
            deadline(cid, deadline_value[i]);
            exec_time(cid, exectime[i]);
            sched_policy(cid, 0);
        }
        else
        {
            /*The XV6 kills the process if th exec time is completed*/
            while(1) {
                
            }
        }
    }

    while(1) {

    }
}