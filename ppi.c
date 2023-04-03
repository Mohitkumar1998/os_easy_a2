#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(void){
  printinfo();
  sched_policy(3, 1);
  exit();
}