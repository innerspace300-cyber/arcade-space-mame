// retrodepth.h — In-process layer export for SPATIAL EMULATOR mobile.
//
// Desktop-fork counterpart used POSIX shared memory because MAME ran as a
// separate process from the AR viewer. On mobile the emulator core and the
// Unity app run in the SAME process (Unity calls retro_run() directly, no
// RetroArch/IPC involved), so layer data just lives in a plain static
// buffer here and the Unity native plugin reads it via direct getter calls
// (P/Invoke) — no serialization, no shared memory, no polling a frame_id
// across a process boundary.
//
// Struct layout intentionally mirrors the desktop fork's RDHeader/RDLayerDesc
// so a Unity-side reader can share most of its layout knowledge between the
// two transports.

#pragma once

#include <cstdint>
#include <cstring>

static constexpr uint32_t RD_MAX_LAYERS  = 8;
// Sized for NeoGeo (320x224) with margin; matches every board we target.
static constexpr uint32_t RD_MAX_WIDTH   = 512;
static constexpr uint32_t RD_MAX_HEIGHT  = 256;
static constexpr uint32_t RD_THUMB_DIM   = 32;

static constexpr uint32_t RD_LAYER_FLAG_HAS_OWNER = 0x1u;
static constexpr uint16_t RD_OWNER_NONE = 0xFFFFu;

struct RDLayerDesc
{
	char     name[32];
	uint32_t z_order;
	uint32_t width;
	uint32_t height;
	bool     has_owner;
};

// Palette→depth-group routing for boards that group sprites by palette
// (Neo Geo) rather than exposing fixed hardware layers. Set from the C#
// side via retrodepth_set_palette_route(); defaults to "everything in
// group 0" when never called, matching the desktop control-shmem default.
struct RDPaletteRoute
{
	uint8_t route[256]; // palette_index -> group (0-3); 0xFF = default (grp0)
};

// ---------------------------------------------------------------------------
// MAME-side write API (same shape as the desktop fork's retrodepth_write_*)
// ---------------------------------------------------------------------------

void retrodepth_init();
bool retrodepth_active();

void retrodepth_write_layer(uint32_t z_order, const char* name,
                            const uint32_t* bgra_pixels,
                            const uint16_t* owner_ids,
                            uint32_t width, uint32_t height);

// Call once at the end of a frame's export. Advances the frame counter that
// retrodepth_frame_id() reports, so the C# side can detect new data without
// re-reading unchanged layers every tick.
void retrodepth_commit();

void retrodepth_write_palette_data(const uint32_t* argb_data, uint32_t count);

// Neo Geo palette-route read, called from the driver each frame (mirrors
// the desktop fork's retrodepth_read_palette_route signature/call sites).
void retrodepth_read_palette_route(RDPaletteRoute* out);

// ---------------------------------------------------------------------------
// Unity-side read API — plain C functions for P/Invoke, no marshalling of
// composite structs required (each field fetched individually keeps the
// P/Invoke signatures trivial and avoids struct-layout/padding mismatches
// between C++ and C#).
// ---------------------------------------------------------------------------

extern "C" {

// Frame sequence number; increments on every retrodepth_commit(). Unity
// polls this and only re-reads layers when it has changed.
uint32_t retrodepth_frame_id();

uint32_t retrodepth_layer_count();

// Fills a caller-owned RDLayerDesc for layer index i (0 <= i < layer_count).
// Returns false if i is out of range.
bool retrodepth_get_layer_desc(uint32_t index, RDLayerDesc* out);

// Copies this layer's BGRA pixels (width*height*4 bytes) into dst, which
// the caller must size from the RDLayerDesc already fetched. Returns false
// if index is out of range or dst_size is too small.
bool retrodepth_get_layer_pixels(uint32_t index, uint32_t* dst, uint32_t dst_size_bytes);

// Copies this layer's owner ids (width*height uint16_t) into dst. Only
// valid when the layer's RDLayerDesc.has_owner is true (check that first);
// returns false if index is out of range, dst_size is too small, or the
// layer has no owner data.
bool retrodepth_get_layer_owners(uint32_t index, uint16_t* dst, uint32_t dst_size_bytes);

// Called by Unity's per-game depth-config / palette editor to route a
// palette index to a depth group (Neo Geo only; harmless no-op for
// fixed-layer boards). group must be 0-3 or 0xFF (reset to default).
void retrodepth_set_palette_route(uint32_t palette_index, uint8_t group);

} // extern "C"
