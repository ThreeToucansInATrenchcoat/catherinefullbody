#pragma once

#include <SDL2/SDL_events.h>

void catherine_linux_overlay_startup();
bool catherine_linux_overlay_event(const SDL_Event& event);
void catherine_linux_overlay_point(int x, int y, int width, int height);
