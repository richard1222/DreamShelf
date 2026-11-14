// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "catalog.h"
#include "launcher/font_text.h"
void draw_theme_ui(SDL_Renderer *renderer, A4Font *font, int source, unsigned page,
 const std::vector<Theme> &themes, int selected, const std::string &message,
 bool busy, unsigned received_bytes);
