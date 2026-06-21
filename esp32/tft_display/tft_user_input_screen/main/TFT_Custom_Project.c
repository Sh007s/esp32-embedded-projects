#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void app_main(void)
{
    display_init();

    char buffer[20];
    int index = 0;
    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000)); 
    printf("\n");
    printf("====================================\n");
    printf(" RGB565 Color Tester\n");
    printf(" Enter value (0 - 65535)\n");
    printf(" Press ENTER to display color\n");
    printf("====================================\n");

    while (1)
    {
        int ch = getchar();

        if (ch == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (ch == '\r' || ch == '\n')
        {
            buffer[index] = '\0';

            if (index > 0)
            {
                uint16_t color = (uint16_t)atoi(buffer);

                printf("\n");
                printf("Color = %u (0x%04X)\n",
                       color,
                       color);

                display_fill(color);
            }

            index = 0;

            printf("\nEnter Color: ");
            fflush(stdout);
        }
        else
        {
            if (index < (sizeof(buffer) - 1))
            {
                buffer[index++] = (char)ch;

                putchar(ch);
                fflush(stdout);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}