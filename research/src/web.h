#pragma once
#include <Arduino.h>

void web_begin();                 // FUN_42039ee8: registers the routes and starts the server
void web_loop();                  // server.handleClient(), called from loop()
extern volatile bool g_restart_requested;   // set by /restart (DAT_3fc9b850)
