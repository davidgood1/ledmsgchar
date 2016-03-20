/**
 * @file   testledmsgchar.c
 * @author David Good
 * @date   20-03-2016
 * @version 0.1
 * @brief  A Linux user space program that communicates with the ledmsgchar LKM.
 * It passes strings to the LKM representing buffers. For this example to work
 * the device must be called /dev/ledmsgchar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define NUM_ROWS       8
#define NUM_ROW_BYTES 18
#define STRING_LENGTH ((NUM_ROWS * NUM_ROW_BYTES * 2) + 1)

static void fill_row(unsigned int row, char *str) {
    unsigned int r, i;

    for (r = 0; r < NUM_ROWS; ++r) {
        for (i = 0; i < NUM_ROW_BYTES; ++i) {
            if (r == row) {
                memcpy(str, "FF", 2);
            } else {
                memcpy(str, "00", 2);
            }
            str += 2;
        }
    }
    *str = 0;
}

static void fill_cols(unsigned int col, char *str) {
    unsigned int r, i;

    for (r = 0; r < NUM_ROWS; ++r) {
        for (i = 0; i < NUM_ROW_BYTES; ++i) {
            sprintf(str, "%02X", (unsigned int)(1 << col));
            str += 2;
        }
    }
    *str = 0;
}

int main() {
    int ret, fd;
    char *dev_name = "/dev/ledmsgchar";
    unsigned int pattern = 0;

    char hexStr[((NUM_ROWS * NUM_ROW_BYTES * 2) + 1)];

    printf("Opening device %s...\n", dev_name);
    fd = open(dev_name, O_RDWR);        // Open the device with read/write access
    if (fd < 0) {
        perror("Failed to open the device...");
        return errno;
    }

    while (1) {
        enum patterns {
            ROW_0, ROW_1, ROW_2, ROW_3, ROW_4, ROW_5, ROW_6, ROW_7,
            COL_0, COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7,
        };

        if (pattern <= ROW_7) {
            fill_row(pattern, hexStr);
            /* printf("row %d: %s\n", pattern, hexStr); */
        } else if (pattern <= COL_7) {
            fill_cols(pattern - 8, hexStr);
            /* printf("col %d: %s\n", pattern - 8, hexStr); */
        } else {
            printf("invalid pattern %d\n", pattern);
        }

        /* printf("before switch pattern = %d\n", pattern); */
        /* switch (pattern) { */
        /* ROW_0: */
        /*     printf("pattern = %d\n", pattern); */
        /*     break; */
        /* ROW_1: */
        /*     printf("pattern = %d\n", pattern); */
        /*     break; */
        /* ROW_2: */
        /*     printf("pattern = %d\n", pattern); */
        /* ROW_3: */
        /*     printf("pattern = %d\n", pattern); */
        /* ROW_4: */
        /*     printf("pattern = %d\n", pattern); */
        /* ROW_5: */
        /*     printf("pattern = %d\n", pattern); */
        /* ROW_6: */
        /*     printf("pattern = %d\n", pattern); */
        /* ROW_7: */
        /*     printf("pattern = %d\n", pattern); */
        /*     /\* fill_row(pattern, hexStr); *\/ */
        /*     break; */
        /* COL_0: */
        /* COL_1: */
        /* COL_2: */
        /* COL_3: */
        /* COL_4: */
        /* COL_5: */
        /* COL_6: */
        /* COL_7: */
        /*     /\* fill_cols(pattern - 8, hexStr); *\/ */
        /*     break; */
        /* default: */
        /*     printf("Hit Default! pattern = %d\n", pattern); */
        /*     pattern = ROW_0; */
        /*     printf("pattern = %d\n", pattern); */
        /* } */
        printf("Pattern %d: %s\n", pattern, hexStr);
        ret = write(fd, hexStr, strlen(hexStr));
        if (ret < 0) {
            perror("Failed to write to the device.");
            return errno;
        }
        sleep(1);
        if (pattern < COL_7) {
            ++pattern;
        } else {
            pattern = ROW_0;
        }
        /* if (pattern < 15) { */
        /*     ++pattern; */
        /* } else { */
        /*     pattern = 0; */
        /* } */
    }
    /* printf("Press ENTER to read back from the device...\n"); */
    /* getchar(); */

    /* printf("Reading from the device...\n"); */
    /* ret = read(fd, receive, BUFFER_LENGTH);        // Read the response from the LKM */
    /* if (ret < 0){ */
    /*     perror("Failed to read the message from the device."); */
    /*     return errno; */
    /* } */
    /* printf("The received message is: [%s]\n", receive); */
    /* printf("End of the program\n"); */
    /* return 0; */
}
