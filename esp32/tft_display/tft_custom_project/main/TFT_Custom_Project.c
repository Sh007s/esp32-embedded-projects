#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#if 0

aaavoid app_main(void)
{
    display_init();
    
 //   display_gradient(); 
 display_fill(TFT_BLACK);
 vTaskDelay(pdMS_TO_TICKS(2000));
 char input[20];

    while (1)
    {
        printf("\nEnter color value (0 - 65535): ");

        if (fgets(input, sizeof(input), stdin) != NULL)
        {
            uint16_t color = (uint16_t)atoi(input);

            printf("Displaying color: %u (0x%04X)\n",
                   color,
                   color);

            display_fill(color);
        }
    }
   // while (1)
    {
     #if 0
        display_fill(TFT_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLUE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_CYAN);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_MAGENTA);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        display_fill(0x0001); // Very dark blue
        vTaskDelay(pdMS_TO_TICKS(2000));

        #endif
        

     #if 0 
     display_fill_rect(0,   0,   120, 80, TFT_RED);
     vTaskDelay(pdMS_TO_TICKS(50));

     display_fill_rect(120, 0,   120, 80, TFT_GREEN);
     vTaskDelay(pdMS_TO_TICKS(50));
     
     display_fill_rect(0,   80,  120, 80, TFT_BLUE);
     vTaskDelay(pdMS_TO_TICKS(50));

     display_fill_rect(120, 80,  120, 80, TFT_YELLOW);
     vTaskDelay(pdMS_TO_TICKS(50));

     display_fill_rect(0,   160, 120, 80, TFT_CYAN);
     vTaskDelay(pdMS_TO_TICKS(50));
     
     display_fill_rect(120, 160, 120, 80, TFT_MAGENTA);
     vTaskDelay(pdMS_TO_TICKS(50));

     display_fill_rect(0,   240, 120, 80, TFT_WHITE);
     vTaskDelay(pdMS_TO_TICKS(50));

     display_fill_rect(120, 240, 120, 80, TFT_BLACK);
     vTaskDelay(pdMS_TO_TICKS(50));
     
     #endif
//  display_fill(TFT_BLACK);

#if 0
while (1)
{
    for (uint16_t color = 0; color < 65535; color++)
    {
        uint8_t r = (color >> 11) & 0x1F;
        uint8_t g = (color >> 5)  & 0x3F;
        uint8_t b = color & 0x1F;

        display_fill(color);

        printf(
            "Dec=%5u  Hex=0x%04X  R=%2u G=%2u B=%2u\n",
            color,
            color,
            r,
            g,
            b);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

    }
}

#endif
/*
void app_main(void)
{
    display_init();
    display_fill(TFT_BLACK);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    char input[20];
    char again = 'Y';

    do
    {
        printf("\nEnter RGB565 color (0-65535): ");
        fflush(stdout);

        fgets(input, sizeof(input), stdin);

        uint16_t color = (uint16_t)atoi(input);

        printf("Displaying color: %u (0x%04X)\n",
               color,
               color);

        display_fill(color);

        printf("\nTry another color? (Y/N): ");
        fflush(stdout);
        again = getchar();

        while (getchar() != '\n'); // clear input buffer

    } while (again == 'Y' || again == 'y');

    printf("\nColor test finished.\n");
}
    */
/*
#include <stdio.h>
#include <stdlib.h>

#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "display.h"

void app_main(void)
{
    display_init();


esp_console_config_t console_config = {
    .max_cmdline_args = 8,
    .max_cmdline_length = 64,
};

ESP_ERROR_CHECK(esp_console_init(&console_config));

    display_fill(TFT_BLACK);
    
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  //  char input[20];

char buffer[20];
int index = 0;

while (1)
{
    int ch = getchar();

    if (ch == '\n' || ch == '\r')
    {
        buffer[index] = '\0';

        uint16_t color = atoi(buffer);

        printf("Color=%u\n", color);

        display_fill(color);

        index = 0;
    }
    else if (index < sizeof(buffer) - 1)
    {
        buffer[index++] = ch;
    }
}
}

*/

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