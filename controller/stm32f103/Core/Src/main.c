#include "main.h"
#include "controller_app.h"

int main(void)
{
    Board_Init();
    ControllerApp_Init();
    for (;;) ControllerApp_Poll();
}

void Error_Handler(void)
{
    for (;;) { }
}
