
#include <iostream>
#include <random>
#include <tuple>
#include <chrono>

#include "CASBasedSkipList.h"

using namespace std;

int main()
{
  CASBasedSkipList n = CASBasedSkipList(1, 0, 100000);
  for(int i = 1;    ;i++){
    int step = 0;
    int scan = 0;
    printf("Enter 0 for bottom level traversal\n");
    printf("Enter 1 for insert\n");
    printf("Enter 2 for delete\n");
    printf("Enter 3 for searching\n");
    scan = scanf("%d",&step);
    if(step == 0) {
      n.listTraversal();
      n.valueTraversal();
    }
    else if(step == 1){
      int a, b;
      printf("\nProvide key and value for insert\n");
      scan = scanf("%d %d",&a , &b);
      bool s = n.insertOrUpdate(0,a,b);
      if(s) printf("\nSuccess\n");
    }else if (step == 2){
      int a;
      printf("\nProvide key to delete\n");
      scan = scanf("%d",&a);
      bool s=n.erase(0,a);
      if(s) printf("\nSUCCESS\n");
    }else if (step == 3){
      int a, val = 0 ;
      printf("\nProvide key to Search\n");
      scan = scanf("%d",&a);
      val = n.contains(0,a);
      if(val>0) printf("\nSUCCESS\n");
    }
  }
  return 0;
}