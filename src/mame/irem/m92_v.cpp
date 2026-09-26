// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
/*****************************************************************************

    Irem M92 video hardware, Bryan McPhail, mish@tendril.co.uk

    Brief Overview:

        3 scrolling playfields, 512 by 512.
        Each playfield can enable rowscroll, change shape (to 1024 by 512),
        be enabled/disabled and change position in VRAM.

        Tiles can have several priority values:
            0 = standard
            1 = Top 8 pens appear over sprites (split tilemap)
            2 = Whole tile appears over sprites
            3 = ?  Seems to be the whole tile is over sprites (as 2).

        Sprites have 2 priority values:
            0 = standard
            1 = Sprite appears over all tiles, including high priority pf1

        Raster interrupts can be triggered at any line of the screen redraw,
        typically used in games like R-Type Leo to multiplex the top playfield.

*****************************************************************************

    Master Control registers:

        Word 0: Playfield 1 control
            Bit  0x40:  1 = Rowscroll enable, 0 = disable
            Bit  0x10:  0 = Playfield enable, 1 = disable
            Bit  0x04:  0 = 512 x 512 playfield, 1 = 1024 x 512 playfield
            Bits 0x03:  Playfield location in VRAM (0, 0x4000, 0x8000, 0xc000)
        Word 1: Playfield 2 control (as above)
        Word 2: Playfield 3 control (as above)
        Word 3: Raster IRQ position.

    The raster IRQ position is offset by 128+8 from the first visible line,
    suggesting there are 8 lines before the first visible one.

*****************************************************************************/

#include "emu.h"
#include "m92.h"
#include "../../emu/retrodepth.h"

/*****************************************************************************/

void m92_state::sprite_dma(int amount)
{
	u16 src_objbank = m_videocontrol << 7 & 0x400;
	u16 *source = &m_spriteram[src_objbank];
	u16 dest_offs = 0;

	const bool sort = BIT(m_dmacontrol[2], 0);
	const bool skip_layer7 = !BIT(m_dmacontrol[2], 3);

	// copy sprites, optionally sort by layer
	for (int layer = 0; layer < 8; layer++)
	{
		for (int offs = 0, numcols = 0; offs < 0x400; offs += 4 * numcols)
		{
			int curlayer = (source[offs] >> 13) & 7;
			numcols = 1 << ((source[offs] >> 11) & 3);

			if ((!sort || (sort && layer == curlayer)) && !(curlayer == 7 && skip_layer7))
			{
				for (int i = 0; i < 4; i++)
					m_spriteram_buffer[dest_offs + i] = source[offs + i];

				dest_offs += 4 * numcols;
				if (dest_offs >= amount)
					return;
			}
		}

		if (layer == 6 && skip_layer7)
			return;
	}
}

TIMER_CALLBACK_MEMBER(m92_state::dma_done)
{
	m_dma_busy = 1;
	m_upd71059c->ir1_w(1);
}

void m92_state::dmacontrol_w(offs_t offset, u16 data, u16 mem_mask)
{
	/*
	offset 0: sprite list size (negative)

	offset 1: enable direct memory access
	          bit 0: sprites (0xf8000-0xf87ff)
	          bit 1: palette (0xf8800-0xf8fff)

	offset 2: DMA transfer mode
	          bit 0: sort sprites by layer
	          bit 1: split sprites based on Y position (unused?)
	          bit 3: 1 = copy all layers, 0 = skip layer 7
	          bits 8-10: destination palette bank

	offset 3: ? (always 0)
	offset 4: start DMA transfer
	offset 5: ?
	*/
	COMBINE_DATA(&m_dmacontrol[offset]);
	//logerror("%s: dmacontrol_w %08x %08x\n",m_maincpu->pc(),offset,data);

	// the data written doesn't matter, confirmed by several games
	if (offset == 4)
	{
		int clocks = 0x400;

		// palette
		int src_palbank = m_videocontrol << 6 & 0xc00;
		int dest_palbank = m_dmacontrol[2] << 2 & 0x1c00;

		for (int i = 0; i < 0x400; i++)
			m_palette->write16(i | dest_palbank, m_paletteram[i | src_palbank]);

		// sprites
		for (int i = 0; i < 0x400; i++)
			m_spriteram_buffer[i] = 0;

		if (m_dmacontrol[0] & 0xff)
		{
			int amount = (0x100 - (m_dmacontrol[0] & 0xff)) * 4;
			clocks += amount;
			sprite_dma(amount);
		}

		// assume it can transfer 1 word per 2*pixel clock
		m_dma_timer->adjust(m_screen->pixel_period() / 2 * clocks);

		m_dma_busy = 0;
		m_upd71059c->ir1_w(0);
	}
}

void m92_state::videocontrol_w(offs_t offset, u16 data, u16 mem_mask)
{
	/*
	    Many games write:
	        0x2000
	        0x201b in alternate frames.

	    It appears that the only games requiring the palette bank logic
	    are Major Title 2, Ninja Baseball Bat Man, Dream Soccer '94
	    and Gun Force 2.
	*/

	/*
	    fedc ba98 7654 3210
	    xx.. .... .... ....   palette high bits
	    ..x. .... .... ....   enable OBJ priority
	    ...x .... .... ....   enable CPU access to palette?
	    .... x... .... ....   unused?
	    .... .xxx .... ....   direct palette bank
	    .... .... x... ....   disable sprites
	    .... .... .x.. ....   ?
	    .... .... ..xx ....   DMA palette bank
	    .... .... .... x...   DMA OBJ bank
	    .... .... .... .xx.   CPU palette bank
	    .... .... .... ...x   CPU OBJ bank
	*/
	if ((m_videocontrol ^ data) & mem_mask & 0xc000)
		machine().tilemap().mark_all_dirty();

	COMBINE_DATA(&m_videocontrol);
	//logerror("%s: videocontrol_w %d = %02x\n",m_maincpu->pc(),offset,data);
}

u16 m92_state::spriteram_r(offs_t offset)
{
	if (BIT(m_dmacontrol[1], 0))
		return m_spriteram_buffer[offset];
	else
		return m_spriteram[offset | (m_videocontrol << 10 & 0x400)];
}

void m92_state::spriteram_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (BIT(m_dmacontrol[1], 0))
		COMBINE_DATA(&m_spriteram_buffer[offset]);
	else
		COMBINE_DATA(&m_spriteram[offset | (m_videocontrol << 10 & 0x400)]);
}

u16 m92_state::paletteram_r(offs_t offset)
{
	if (BIT(m_dmacontrol[1], 1))
		return m_palette->read16(offset | (m_videocontrol << 2 & 0x1c00));
	else
		return m_paletteram[offset | (m_videocontrol << 9 & 0xc00)];
}

void m92_state::paletteram_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (BIT(m_dmacontrol[1], 1))
		m_palette->write16(offset | (m_videocontrol << 2 & 0x1c00), data, mem_mask);
	else
		COMBINE_DATA(&m_paletteram[offset | (m_videocontrol << 9 & 0xc00)]);
}

/*****************************************************************************/

TILE_GET_INFO_MEMBER(m92_state::get_pf_tile_info)
{
	M92_pf_layer_info *layer = (M92_pf_layer_info *)tilemap.user_data();
	tile_index = 2 * tile_index + layer->vram_base;

	int attrib = m_vram_data[tile_index + 1];
	int tile = m_vram_data[tile_index] + ((attrib & 0x8000) << 1);

	tileinfo.set(0,
			tile,
			(attrib & 0x7f) | (m_videocontrol >> 7 & 0x180),
			TILE_FLIPYX(attrib >> 9));

	if (attrib & 0x100)
		tileinfo.group = 2;
	else if (attrib & 0x80)
		tileinfo.group = 1;
	else
		tileinfo.group = 0;
}

/*****************************************************************************/

void m92_state::vram_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_vram_data[offset]);

	for (int laynum = 0; laynum < 3; laynum++)
	{
		if ((offset & 0x6000) == m_pf_layer[laynum].vram_base)
		{
			m_pf_layer[laynum].tmap->mark_tile_dirty((offset & 0x1fff) / 2);
			m_pf_layer[laynum].wide_tmap->mark_tile_dirty((offset & 0x3fff) / 2);
		}
		if ((offset & 0x6000) == m_pf_layer[laynum].vram_base + 0x2000)
			m_pf_layer[laynum].wide_tmap->mark_tile_dirty((offset & 0x3fff) / 2);
	}
}

/*****************************************************************************/

void m92_state::master_control_w(offs_t offset, u16 data, u16 mem_mask)
{
	u16 old = m_pf_master_control[offset];
	M92_pf_layer_info *layer;

	COMBINE_DATA(&m_pf_master_control[offset]);

	switch (offset)
	{
		case 0: /* Playfield 1 (top layer) */
		case 1: /* Playfield 2 (middle layer) */
		case 2: /* Playfield 3 (bottom layer) */
			layer = &m_pf_layer[offset];

			/* update VRAM base (bits 0-1) */
			layer->vram_base = (m_pf_master_control[offset] & 3) * 0x2000;

			/* update size (bit 2) */
			if (m_pf_master_control[offset] & 0x04)
			{
				layer->tmap->enable(false);
				layer->wide_tmap->enable((~m_pf_master_control[offset] >> 4) & 1);
			}
			else
			{
				layer->tmap->enable((~m_pf_master_control[offset] >> 4) & 1);
				layer->wide_tmap->enable(false);
			}

			/* mark everything dirty of the VRAM base or size changes */
			if ((old ^ m_pf_master_control[offset]) & 0x07)
			{
				layer->tmap->mark_all_dirty();
				layer->wide_tmap->mark_all_dirty();
			}
			break;

		case 3:
			m_raster_irq_position = (m_pf_master_control[3] & 0x1ff) - 128;
			m_upd71059c->ir2_w(0);
			break;
	}
}

/*****************************************************************************/

void m92_state::video_start()
{
	m_dma_timer = timer_alloc(FUNC(m92_state::dma_done), this);

	memset(m_pf_master_control, 0, sizeof(m_pf_master_control));
	memset(&m_pf_layer, 0, sizeof(m_pf_layer));

	for (int laynum = 0; laynum < 3; laynum++)
	{
		M92_pf_layer_info *layer = &m_pf_layer[laynum];

		/* allocate two tilemaps per layer, one normal, one wide */
		layer->tmap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(m92_state::get_pf_tile_info)), TILEMAP_SCAN_ROWS, 8,8, 64,64);
		layer->wide_tmap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(m92_state::get_pf_tile_info)), TILEMAP_SCAN_ROWS, 8,8, 128,64);

		/* set the user data for each one to point to the layer */
		layer->tmap->set_user_data(&m_pf_layer[laynum]);
		layer->wide_tmap->set_user_data(&m_pf_layer[laynum]);

		/* set scroll offsets */
		layer->tmap->set_scrolldx(2 * laynum, -2 * laynum + 8);
		layer->tmap->set_scrolldy(-128, -128);
		layer->wide_tmap->set_scrolldx(2 * laynum - 256, -2 * laynum + 8 - 256);
		layer->wide_tmap->set_scrolldy(-128, -128);

		/* layer group 0 - totally transparent in front half */
		layer->tmap->set_transmask(0, 0xffff, (laynum == 2) ? 0x0000 : 0x0001);
		layer->wide_tmap->set_transmask(0, 0xffff, (laynum == 2) ? 0x0000 : 0x0001);

		/* layer group 1 - pens 0-7 transparent in front half */
		layer->tmap->set_transmask(1, 0x00ff, (laynum == 2) ? 0xff00 : 0xff01);
		layer->wide_tmap->set_transmask(1, 0x00ff, (laynum == 2) ? 0xff00 : 0xff01);

		/* layer group 2 - pen 0 transparent in front half */
		layer->tmap->set_transmask(2, 0x0001, (laynum == 2) ? 0xfffe : 0xffff);
		layer->wide_tmap->set_transmask(2, 0x0001, (laynum == 2) ? 0xfffe : 0xffff);

		save_item(NAME(layer->vram_base), laynum);
		save_item(NAME(layer->control), laynum);
	}

	m_paletteram_buffer.resize(m_palette->entries());
	m_palette->basemem().set(m_paletteram_buffer, ENDIANNESS_LITTLE, 2);

	save_item(NAME(m_pf_master_control));
	save_item(NAME(m_videocontrol));
	save_item(NAME(m_raster_irq_position));
	save_item(NAME(m_dma_busy));
	save_item(NAME(m_paletteram_buffer));
}

void ppan_state::video_start()
{
	m92_state::video_start();

	for (int laynum = 0; laynum < 3; laynum++)
	{
		M92_pf_layer_info *layer = &m_pf_layer[laynum];

		/* set scroll offsets */
		layer->tmap->set_scrolldx(2 * laynum + 11, -2 * laynum + 11);
		layer->tmap->set_scrolldy(-8, -8);
		layer->wide_tmap->set_scrolldx(2 * laynum - 256 + 11, -2 * laynum + 11 - 256);
		layer->wide_tmap->set_scrolldy(-8, -8);
	}
}

/*****************************************************************************/

void m92_state::draw_sprites(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	/*
	offset 0: lllccrry yyyyyyyy - layer (handled at sprite_dma), columns, rows, y
	offset 1: cccccccc cccccccc - code
	offset 2: ......yx cccccccc - flip y, flip x, color (high bit can also mean priority)
	offset 3: .......x xxxxxxxx - x
	*/
	u16 *source = m_spriteram_buffer;

	const bool pri_enabled = BIT(m_videocontrol, 13);
	const u8 cmask = pri_enabled ? 0x7f : 0xff;
	const u16 chigh = BIT(m_videocontrol, 15) ? 0x100 : 0;

	for (int offs = 0; offs < 0x400; )
	{
		int x = source[offs+3] & 0x1ff;
		int y = source[offs+0] & 0x1ff;
		int code = source[offs+1];
		int color = (source[offs+2] & cmask) | chigh;
		int pri = pri_enabled ? ((~source[offs+2] >> 6) & 2) : 2;
		int flipx = (source[offs+2] >> 8) & 1;
		int flipy = (source[offs+2] >> 9) & 1;
		int numcols = 1 << ((source[offs+0] >> 11) & 3);
		int numrows = 1 << ((source[offs+0] >> 9) & 3);

		offs += 4 * numcols;

		if (m_rd_sprite_pri >= 0 && pri != m_rd_sprite_pri)
			continue;

		x = (x - 16) & 0x1ff;
		y = 384 - 16 - y;

		if (flipx) x += 16 * (numcols - 1);

		for (int col = 0; col < numcols; col++)
		{
			int s_ptr = 8 * col;
			if (!flipy) s_ptr += numrows - 1;

			for (int row = 0; row < numrows; row++)
			{
				if (flip_screen())
				{
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, !flipx, !flipy,
							464 - x, 240 - (y - row * 16),
							screen.priority(), pri, 0);

					// wrap around x
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, !flipx, !flipy,
							464 - x + 512, 240 - (y - row * 16),
							screen.priority(), pri, 0);
				}
				else
				{
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, flipx, flipy,
							x, y - row * 16,
							screen.priority(), pri, 0);

					// wrap around x
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, flipx, flipy,
							x - 512, y - row * 16,
							screen.priority(), pri, 0);
				}
				if (flipy) s_ptr++;
				else s_ptr--;
			}
			if (flipx) x -= 16;
			else x += 16;
		}
	}
}

void ppan_state::draw_sprites(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	u16 *source = m_spriteram; // sprite buffer control is never triggered
	const int amount = (source[0] & 0xff) * 4;

	for (int offs = 4; offs <= amount; )
	{
		int x = source[offs+3] & 0x1ff;
		int y = source[offs+0] & 0x1ff;
		int code = source[offs+1];
		int color = source[offs+2] & 0x007f;
		int pri = (~source[offs+2] >> 6) & 2;
		int flipx = (source[offs+2] >> 8) & 1;
		int flipy = (source[offs+2] >> 9) & 1;
		int numcols = 1 << ((source[offs+0] >> 11) & 3);
		int numrows = 1 << ((source[offs+0] >> 9) & 3);

		offs += 4 * numcols;

		y = 384 - 16 - y - 7;
		y -= 128;
		if (y < 0) y += 512;

		if (flipx) x += 16 * (numcols - 1);

		for (int col = 0; col < numcols; col++)
		{
			int s_ptr = 8 * col;
			if (!flipy) s_ptr += numrows - 1;

			for (int row = 0; row < numrows; row++)
			{
				if (flip_screen())
				{
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, !flipx, !flipy,
							464 - x, 240 - (y - row * 16),
							screen.priority(), pri, 0);

					// wrap around x
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, !flipx, !flipy,
							464 - x + 512, 240 - (y - row * 16),
							screen.priority(), pri, 0);
				}
				else
				{
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, flipx, flipy,
							x, y - row * 16,
							screen.priority(), pri, 0);

					// wrap around x
					m_gfxdecode->gfx(1)->prio_transpen(bitmap,cliprect,
							code + s_ptr, color, flipx, flipy,
							x - 512, y - row * 16,
							screen.priority(), pri, 0);
				}
				if (flipy) s_ptr++;
				else s_ptr--;
			}
			if (flipx) x -= 16;
			else x += 16;
		}
	}
}

/*****************************************************************************/

void m92_state::m92_update_scroll_positions()
{
	/*
	    Playfield 3 rowscroll data is 0xdfc00 - 0xdffff
	    Playfield 2 rowscroll data is 0xdf800 - 0xdfbff
	    Playfield 1 rowscroll data is 0xdf400 - 0xdf7ff

	    It appears to be hardwired to those locations.

	    In addition, each playfield is staggered 2 pixels horizontally from the
	    previous one.  This is most obvious in Hook & Blademaster.
	*/

	for (int laynum = 0; laynum < 3; laynum++)
	{
		M92_pf_layer_info *layer = &m_pf_layer[laynum];

		if (m_pf_master_control[laynum] & 0x40)
		{
			const u16 *scrolldata = m_vram_data + (0xf400 + 0x400 * laynum) / 2;

			layer->tmap->set_scroll_rows(512);
			layer->wide_tmap->set_scroll_rows(512);
			for (int i = 0; i < 512; i++)
			{
				layer->tmap->set_scrollx(i, scrolldata[i]);
				layer->wide_tmap->set_scrollx(i, scrolldata[i]);
			}
		}
		else
		{
			layer->tmap->set_scroll_rows(1);
			layer->wide_tmap->set_scroll_rows(1);
			layer->tmap->set_scrollx(0, layer->control[2]);
			layer->wide_tmap->set_scrollx(0, layer->control[2]);
		}

		layer->tmap->set_scrolly(0, layer->control[0]);
		layer->wide_tmap->set_scrolly(0, layer->control[0]);
	}
}

/*****************************************************************************/

void m92_state::m92_draw_tiles(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	if ((~m_pf_master_control[2] >> 4) & 1)
	{
		m_pf_layer[2].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
		m_pf_layer[2].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
		m_pf_layer[2].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);
		m_pf_layer[2].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);
	}

	m_pf_layer[1].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
	m_pf_layer[1].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
	m_pf_layer[1].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);
	m_pf_layer[1].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);

	m_pf_layer[0].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
	m_pf_layer[0].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER1, 0);
	m_pf_layer[0].wide_tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);
	m_pf_layer[0].tmap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 1);
}


u32 m92_state::screen_update_m92(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	/* Flipscreen appears hardwired to the dipswitch - strange */
	if (m_dsw->read() & 0x100)
		flip_screen_set(0);
	else
		flip_screen_set(1);

	screen.priority().fill(0, cliprect);
	bitmap.fill(0, cliprect);

	m92_update_scroll_positions();
	m92_draw_tiles(screen, bitmap, cliprect);

	if (~m_videocontrol & 0x80)
		draw_sprites(screen, bitmap, cliprect);

	if (retrodepth_active())
		retrodepth_export(screen, cliprect);

	return 0;
}

/*****************************************************************************

    RetroDepth layer export (SPATIAL EMULATOR)

    Back to front:
        0 background   pen 0 fill
        1 pf3          playfield 3 (usually the opaque back scenery)
        2 pf2          playfield 2
        3 pf1          playfield 1
        4 sprites      sprites that go under the tiles' front halves
        5 front        the tile pixels that appear over those sprites (tile
                       priority 1-3: a tile's top pens or the whole tile)
        6 sprites_top  sprites flagged to appear over everything

    Seen from the front this matches MAME's frame: a front-half tile pixel
    is the only thing that hides a normal sprite, and it moves from its
    playfield to the front layer. The screen is redrawn in strips (raster
    IRQs change scroll mid-frame), so each strip is drawn into the layer
    bitmaps here and the frame is exported after its last strip.

*****************************************************************************/

void m92_state::retrodepth_export(screen_device &screen, const rectangle &cliprect)
{
	static constexpr u16 TRANSP = 0xffff;
	static bitmap_ind16 s_pf[3], s_spr, s_top;
	static bitmap_ind8 s_tileprio;

	const int bw = screen.width(), bh = screen.height();
	if (s_spr.width() != bw || s_spr.height() != bh)
	{
		for (auto &b : s_pf) b.allocate(bw, bh);
		s_spr.allocate(bw, bh);
		s_top.allocate(bw, bh);
		s_tileprio.allocate(bw, bh);
	}

	// Playfields, drawn as m92_draw_tiles does but each into its own bitmap;
	// the priority bitmap collects every playfield's front halves.
	bitmap_ind8 &prio = screen.priority();
	prio.fill(0, cliprect);
	for (int laynum = 2; laynum >= 0; laynum--)
	{
		bitmap_ind16 &dest = s_pf[laynum];
		dest.fill(TRANSP, cliprect);
		if (laynum == 2 && ((m_pf_master_control[2] >> 4) & 1))
			continue;
		M92_pf_layer_info &layer = m_pf_layer[laynum];
		layer.wide_tmap->draw(screen, dest, cliprect, TILEMAP_DRAW_LAYER1, 0);
		layer.tmap->draw(screen, dest, cliprect, TILEMAP_DRAW_LAYER1, 0);
		layer.wide_tmap->draw(screen, dest, cliprect, TILEMAP_DRAW_LAYER0, 1);
		layer.tmap->draw(screen, dest, cliprect, TILEMAP_DRAW_LAYER0, 1);
	}
	for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
		std::copy_n(&prio.pix(y, cliprect.min_x), cliprect.width(), &s_tileprio.pix(y, cliprect.min_x));

	// Sprites, split by priority. The normal ones are drawn whole (the
	// front layer hides what the hardware hides), so they show behind
	// foreground tiles when the layers are spread apart.
	s_spr.fill(TRANSP, cliprect);
	s_top.fill(TRANSP, cliprect);
	if (~m_videocontrol & 0x80)
	{
		prio.fill(0, cliprect);
		m_rd_sprite_pri = 2;
		draw_sprites(screen, s_spr, cliprect);
		prio.fill(0, cliprect); // drawn sprite pixels block later sprites
		m_rd_sprite_pri = 0;
		draw_sprites(screen, s_top, cliprect);
		m_rd_sprite_pri = -1;
	}

	const rectangle &vis = screen.visible_area();
	if (cliprect.max_y < vis.max_y)
		return;

	const rgb_t *colors = m_palette->palette()->entry_list_adjusted();
	const int n_pens = (int)m_palette->palette()->num_colors();
	const int w = vis.width(), h = vis.height();
	static std::vector<uint32_t> packed;
	static std::vector<uint16_t> owners;
	static std::vector<uint16_t> front;
	packed.resize(w * h);
	owners.resize(w * h);
	front.assign(w * h, TRANSP);

	// Move each front-half pixel out of the topmost playfield showing there.
	for (int row = 0; row < h; ++row)
	{
		const int y = vis.min_y + row;
		for (int col = 0; col < w; ++col)
		{
			const int x = vis.min_x + col;
			if (!(s_tileprio.pix(y, x) & 1))
				continue;
			for (int laynum = 0; laynum < 3; laynum++)
			{
				u16 &pen = s_pf[laynum].pix(y, x);
				if (pen == TRANSP)
					continue;
				front[row * w + col] = pen;
				pen = TRANSP;
				break;
			}
		}
	}

	auto write_layer = [&](uint32_t z_order, const char *name, auto &&pen_at) {
		for (int row = 0; row < h; ++row)
		{
			for (int col = 0; col < w; ++col)
			{
				const u16 pen = pen_at(row, col);
				const int idx = row * w + col;
				if (pen == TRANSP)
				{
					packed[idx] = 0x00000001u; // transparent sentinel
					owners[idx] = RD_OWNER_NONE;
				}
				else
				{
					packed[idx] = (pen < n_pens) ? (uint32_t)colors[pen] : 0u;
					owners[idx] = pen / 16;
				}
			}
		}
		retrodepth_write_layer(z_order, name, packed.data(), owners.data(), w, h);
	};
	auto from = [&](bitmap_ind16 &bmp) {
		return [&bmp, &vis](int row, int col) { return bmp.pix(vis.min_y + row, vis.min_x + col); };
	};

	const u16 bg_pen = 0;
	write_layer(0, "background", [&](int, int) { return bg_pen; });
	write_layer(1, "pf3", from(s_pf[2]));
	write_layer(2, "pf2", from(s_pf[1]));
	write_layer(3, "pf1", from(s_pf[0]));
	write_layer(4, "sprites", from(s_spr));
	write_layer(5, "front", [&](int row, int col) { return front[row * w + col]; });
	write_layer(6, "sprites_top", from(s_top));
	retrodepth_commit();
}
