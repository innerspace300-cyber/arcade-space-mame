// license:BSD-3-Clause
// konami_retrodepth.h - RetroDepth layer export (SPATIAL EMULATOR) shared by
// the Konami boards that mix three K052109 tile layers with sprites through
// a K053251 (Sunset Riders, TMNT 2, Lightning Fighters, Bells & Whistles,
// The Simpsons...).
//
// Back to front: background colour, sprites behind every tile layer, tile
// layer (back), sprites, tile layer (middle), sprites, tile layer (front,
// usually the text), sprites in front of everything. Each sprite goes in
// the slot its K053251 priority mask puts it in (see konami_rd_filter_sprite),
// drawn whole, so it shows behind the tiles in front of it when the layers
// are spread apart.
//
// Sprite shadows darken what's under them: they're exported as translucent
// black in their slot (highlights as translucent white).

#ifndef MAME_KONAMI_KONAMI_RETRODEPTH_H
#define MAME_KONAMI_KONAMI_RETRODEPTH_H

#pragma once

#include "emupal.h"
#include "screen.h"
#include "../../emu/retrodepth.h"

#include <vector>

// In a sprite callback, after the priority mask is worked out: while
// slot >= 0, sprites outside that depth slot (0 = behind every tile layer
// ... 3 = in front of them all) get a mask that hides them everywhere; they
// still block the sprites drawn after them, as they do on screen.
inline void konami_rd_filter_sprite(int slot, int &priority_mask)
{
	if (slot < 0) return;
	const int mine = priority_mask == 0 ? 3 : priority_mask == 0xf0 ? 2 : priority_mask == 0xfc ? 1 : 0;
	priority_mask = mine == slot ? 0 : ~0;
}

// draw_tile(i, bitmap): draws the i-th tile layer from the back into bitmap.
// draw_sprites(slot, bitmap): draws the sprites with the filter set to slot.
// Call after the real frame is drawn (and after any palette change that
// affects how it's shown); exports once the last strip is in.
template <typename DrawTile, typename DrawSprites>
void konami_rd_export(screen_device &screen, const rectangle &cliprect, device_palette_interface &palette, int bg_pen,
		DrawTile &&draw_tile, DrawSprites &&draw_sprites)
{
	static constexpr u16 TRANSP = 0xffff;
	static constexpr u16 NO_SPRITE = 0; // sprite pens are colour * 16 + 1..15, never 0
	static bitmap_ind16 s_tiles[3], s_sprites[4];

	const int bw = screen.width(), bh = screen.height();
	if (s_tiles[0].width() != bw || s_tiles[0].height() != bh)
	{
		for (auto &b : s_tiles) b.allocate(bw, bh);
		for (auto &b : s_sprites) b.allocate(bw, bh);
	}

	for (int i = 0; i < 3; i++)
	{
		s_tiles[i].fill(TRANSP, cliprect);
		draw_tile(i, s_tiles[i]);
	}
	for (int slot = 0; slot < 4; slot++)
	{
		s_sprites[slot].fill(NO_SPRITE, cliprect);
		screen.priority().fill(0, cliprect);
		draw_sprites(slot, s_sprites[slot]);
	}

	const rectangle &vis = screen.visible_area();
	if (cliprect.max_y < vis.max_y)
		return;

	const rgb_t *colors = palette.palette()->entry_list_adjusted();
	const int n_pens = (int)palette.palette()->num_colors();
	const int entries = (int)palette.entries();
	const u8 shadow_alpha = u8(255.0 * (1.0 - PALETTE_DEFAULT_SHADOW_FACTOR) + 0.5);
	const u8 highlight_alpha = u8(255.0 * (PALETTE_DEFAULT_HIGHLIGHT_FACTOR - 1.0) / PALETTE_DEFAULT_HIGHLIGHT_FACTOR + 0.5);
	const int w = vis.width(), h = vis.height();
	static std::vector<uint32_t> packed;
	static std::vector<uint16_t> owners;
	packed.resize(w * h);
	owners.resize(w * h);

	auto write_layer = [&](uint32_t z_order, const char *name, bitmap_ind16 *bmp, u16 empty) {
		for (int row = 0; row < h; ++row)
		{
			for (int col = 0; col < w; ++col)
			{
				const int idx = row * w + col;
				const u16 pen = bmp ? bmp->pix(vis.min_y + row, vis.min_x + col) : u16(bg_pen);
				if (bmp && pen == empty)
				{
					packed[idx] = 0x00000001u; // transparent sentinel
					owners[idx] = RD_OWNER_NONE;
				}
				else if (bmp && empty == NO_SPRITE && pen == NO_SPRITE + entries)
				{
					packed[idx] = uint32_t(shadow_alpha) << 24; // shadow: translucent black
					owners[idx] = RD_OWNER_NONE;
				}
				else if (bmp && empty == NO_SPRITE && pen == NO_SPRITE + 2 * entries)
				{
					packed[idx] = (uint32_t(highlight_alpha) << 24) | 0xffffffu; // highlight
					owners[idx] = RD_OWNER_NONE;
				}
				else
				{
					packed[idx] = (pen < n_pens) ? (uint32_t)colors[pen] | 0xff000000u : 0xff000000u;
					owners[idx] = (pen % entries) / 16;
				}
			}
		}
		retrodepth_write_layer(z_order, name, packed.data(), owners.data(), w, h);
	};

	write_layer(0, "background", nullptr, TRANSP);
	write_layer(1, "sprites_back", &s_sprites[0], NO_SPRITE);
	write_layer(2, "tiles_back", &s_tiles[0], TRANSP);
	write_layer(3, "sprites_1", &s_sprites[1], NO_SPRITE);
	write_layer(4, "tiles_mid", &s_tiles[1], TRANSP);
	write_layer(5, "sprites_2", &s_sprites[2], NO_SPRITE);
	write_layer(6, "tiles_front", &s_tiles[2], TRANSP);
	write_layer(7, "sprites_front", &s_sprites[3], NO_SPRITE);
	retrodepth_commit();
}

#endif // MAME_KONAMI_KONAMI_RETRODEPTH_H
