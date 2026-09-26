// retrodepth.cpp — In-process layer export for SPATIAL EMULATOR mobile.
// See retrodepth.h for the transport rationale (no IPC needed in-process).
//
// Threading: retro_run() (and therefore every driver's screen_update()) is
// expected to be called synchronously from Unity's own update loop on a
// single thread, so no locking is used here — this mirrors how every other
// libretro frontend drives a core. If a future build moves emulation to a
// background thread, this file is the place to add a lock or a double
// buffer around g_layers/g_pixel_store.

#include "retrodepth.h"

namespace {

struct LayerSlot
{
	RDLayerDesc desc{};
	uint32_t    pixels[RD_MAX_WIDTH * RD_MAX_HEIGHT]{};
	uint16_t    owners[RD_MAX_WIDTH * RD_MAX_HEIGHT]{};
};

bool        g_active = false;
uint32_t    g_frame_id = 0;
// g_write_cursor advances as the CURRENT frame's write_layer() calls come
// in; g_layer_count is the last-committed, publicly-readable count. Kept
// separate so a read right after retro_run() returns (i.e. right after
// commit()) still sees the frame that was just written, not zero - if
// commit() reset g_layer_count directly, Unity would always observe zero
// layers since it always reads after commit.
uint32_t    g_write_cursor = 0;
uint32_t    g_layer_count = 0;
// Set once the running game's driver commits layers itself.
bool        g_driver_exports = false;
LayerSlot   g_layers[RD_MAX_LAYERS];
uint32_t    g_palette_argb[256 * 16]{};
RDPaletteRoute g_route{};

} // namespace

// Called at every machine start, so each game starts without layers.
void retrodepth_init()
{
	g_write_cursor = 0;
	g_layer_count = 0;
	g_driver_exports = false;
	if (g_active) return;
	g_active = true;
	g_frame_id = 0;
	for (auto& r : g_route.route) r = 0xFF;
}

bool retrodepth_active()
{
	return g_active;
}

void retrodepth_write_layer(uint32_t z_order, const char* name,
                            const uint32_t* bgra_pixels,
                            const uint16_t* owner_ids,
                            uint32_t width, uint32_t height)
{
	if (!g_active) return;
	if (g_write_cursor >= RD_MAX_LAYERS) return;
	if (width > RD_MAX_WIDTH || height > RD_MAX_HEIGHT) return;

	uint32_t slot = g_write_cursor++;
	LayerSlot& s = g_layers[slot];

	std::memset(s.desc.name, 0, sizeof(s.desc.name));
	std::strncpy(s.desc.name, name, sizeof(s.desc.name) - 1);
	s.desc.z_order   = z_order;
	s.desc.width     = width;
	s.desc.height    = height;
	s.desc.has_owner = owner_ids != nullptr;

	std::memcpy(s.pixels, bgra_pixels, width * height * sizeof(uint32_t));
	if (owner_ids)
		std::memcpy(s.owners, owner_ids, width * height * sizeof(uint16_t));
}

void retrodepth_commit()
{
	if (!g_active) return;
	g_driver_exports = true;
	++g_frame_id;
	g_layer_count = g_write_cursor; // publish this frame's count
	g_write_cursor = 0;             // next frame's writes start from slot 0
}

void retrodepth_frame_fallback(const uint32_t* xrgb, uint32_t width, uint32_t height, uint32_t pitch_pixels)
{
	if (!g_active || g_driver_exports || !xrgb) return;
	if (width == 0 || height == 0 || width > RD_MAX_WIDTH || height > RD_MAX_HEIGHT) return;
	LayerSlot& s = g_layers[0];
	std::memset(s.desc.name, 0, sizeof(s.desc.name));
	std::strncpy(s.desc.name, "screen", sizeof(s.desc.name) - 1);
	s.desc.z_order   = 0;
	s.desc.width     = width;
	s.desc.height    = height;
	s.desc.has_owner = false;
	for (uint32_t y = 0; y < height; ++y)
		for (uint32_t x = 0; x < width; ++x)
			s.pixels[y * width + x] = xrgb[y * pitch_pixels + x] | 0xff000000u;
	++g_frame_id;
	g_layer_count = 1;
	g_write_cursor = 0;
}

void retrodepth_write_palette_data(const uint32_t* argb_data, uint32_t count)
{
	if (!g_active) return;
	uint32_t copy_count = (count < 256u * 16u) ? count : 256u * 16u;
	std::memcpy(g_palette_argb, argb_data, copy_count * sizeof(uint32_t));
}

void retrodepth_read_palette_route(RDPaletteRoute* out)
{
	*out = g_route;
}

extern "C" {

uint32_t retrodepth_frame_id()
{
	return g_frame_id;
}

uint32_t retrodepth_layer_count()
{
	return g_layer_count;
}

bool retrodepth_get_layer_desc(uint32_t index, RDLayerDesc* out)
{
	if (index >= g_layer_count) return false;
	*out = g_layers[index].desc;
	return true;
}

bool retrodepth_get_layer_pixels(uint32_t index, uint32_t* dst, uint32_t dst_size_bytes)
{
	if (index >= g_layer_count) return false;
	const LayerSlot& s = g_layers[index];
	uint32_t needed = s.desc.width * s.desc.height * sizeof(uint32_t);
	if (dst_size_bytes < needed) return false;
	std::memcpy(dst, s.pixels, needed);
	return true;
}

// Owner ids (palette index per pixel, RD_OWNER_NONE where transparent) -
// only meaningful when the layer's RDLayerDesc.has_owner is true; the
// buffer is otherwise left as whatever the last owner-bearing layer wrote
// to that slot, so callers must check has_owner first.
bool retrodepth_get_layer_owners(uint32_t index, uint16_t* dst, uint32_t dst_size_bytes)
{
	if (index >= g_layer_count) return false;
	const LayerSlot& s = g_layers[index];
	if (!s.desc.has_owner) return false;
	uint32_t needed = s.desc.width * s.desc.height * sizeof(uint16_t);
	if (dst_size_bytes < needed) return false;
	std::memcpy(dst, s.owners, needed);
	return true;
}

void retrodepth_set_palette_route(uint32_t palette_index, uint8_t group)
{
	if (palette_index >= 256) return;
	g_route.route[palette_index] = group;
}

} // extern "C"
