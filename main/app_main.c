#include <stdio.h>

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"
#include "menu.h"

void app_main(void)
{
    storage_init();
    display_init();
    input_init();
    buzzer_init();

    menu_run();
}