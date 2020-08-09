/* Copyright (C) StrawberryHacker */

#include "types.h"
#include "syscall.h"
#include "hardware.h"
#include "print.h"

void p(const char* data) {
    while (*data) {
        u8 status;
        do {
            status = syscall_print_get_status();
        } while (!status);

        syscall_print_byte(*data);
        data++;
    }
}

int main(void) {
    
    /* 
     * ====================================================================
     * Put your application code here. This function may return, the
     * scheduler will take care of it. No startup code is needed.
     * Please read the docmentation before implementing apps. 
     * ====================================================================
     */
    //p("hellsdlfkjsdflksjdflkjf sdflkjsdflksjdf lsdkj sdflkkjsdflskj =n");
    print("hellsdlfkjsdflksjdflkjf sdflkjsdflksjdf lsdkj sdflkkjsdflskj =n %4h\n", 0xcafecafe);
    while (1);
    return 1;
 }
