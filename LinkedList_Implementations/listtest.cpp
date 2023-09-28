#include <iostream>
#include "harrisList.h"
int compareInt(int x, int y){
    return x - y; 
}
int main(int argc, char **argv){
    HarrisList<int, compareInt> list;
    list.printList();
    list.insert(5);
    list.insert(3);
    list.printList();
    list.remove(3);
    list.printList();
    return 0;
}