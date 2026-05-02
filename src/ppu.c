#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/interrupt.h"
#include "cgba/memory.h"
#include "cgba/ppu.h"
#include "SDL.h"

#define WINDOW_SCALE 3
#define CLOCKS_PER_DOT 4

#define HBLANK_START (CLOCKS_PER_DOT*240)
#define SCANLINE_END (CLOCKS_PER_DOT*308)

#define VBLANK_START  160 /* scanline start for vblank */
#define VBLANK_END    227 /* scanline end for vblank */
#define NUM_SCANLINES 228

#define PX_PER_TILE 8
#define TILES_PER_SCANLINE (FRAME_WIDTH / PX_PER_TILE)

#define KB 1024

/* XBGR1555 */
#define WHITE 0xffff

#define PRAM_START 0x05000000
#define VRAM_START 0x06000000

/* screen block dimensions */
#define SB_PX_SIDE_LENGTH 256
#define SB_TILE_SIDE_LENGTH (SB_PX_SIDE_LENGTH / PX_PER_TILE)

enum PPU_BGNO {
    PPU_BG0,
    PPU_BG1,
    PPU_BG2,
    PPU_BG3,
};

typedef struct scanline_data {
    uint16_t px_colors[FRAME_WIDTH];
    bool px_transparency[FRAME_WIDTH];
    uint32_t px_color_addrs[FRAME_WIDTH];
} scanline_data;

typedef struct tile_entry_data {
    int tileno;
    uint32_t palette_bank;
    bool yflip;
    bool xflip;
} tile_entry_data;

static const int text_bg_px_widths[4] = {256, 512, 256, 512};
static const int text_bg_px_heights[4] = {256, 256, 512, 512};

/* Calculates the effective vcount within a BG map using the vertical scroll of the given BG */
static inline int get_effective_vcount(gba_ppu *ppu, enum PPU_BGNO bgno, int bgsize)
{
    int h = text_bg_px_heights[bgsize];
    int yoff = ppu->bgvoffsets[bgno];
    return ((int)ppu->vcount + yoff) & (h - 1);
}

static inline int get_effective_pixelno(gba_ppu *ppu, enum PPU_BGNO bgno, int bgsize, int pixelno)
{
    int w = text_bg_px_widths[bgsize];
    int xoff = ppu->bghoffsets[bgno];
    return (pixelno + xoff) & (w - 1);
}

static inline void populate_tile_data(uint16_t tile_map_entry, tile_entry_data *tile_data)
{
    tile_data->tileno = tile_map_entry & 0x3ff;
    tile_data->palette_bank = (tile_map_entry >> 12) & 0xf; // not used in 8-bit color mode
    tile_data->yflip = tile_map_entry & (1 << 11);
    tile_data->xflip = tile_map_entry & (1 << 10);
}

gba_ppu *init_ppu(void)
{
    gba_ppu *ppu = malloc(sizeof(gba_ppu));
    if (ppu == NULL)
        return NULL;

    ppu->dispcnt = 0x0080; // force blank -> all white lines drawn
    ppu->dispstat = 0;
    ppu->vcount = 0;
    ppu->scanline_clock = 0;
    ppu->curr_frame_rendered = false;

    // white screen on startup
    for (size_t i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
        ppu->frame_buffer[i] = WHITE;

    return ppu;
}

void deinit_ppu(gba_ppu *ppu)
{
    if (ppu->screen != NULL)
        SDL_DestroyTexture(ppu->screen);

    if (ppu->renderer != NULL)
        SDL_DestroyRenderer(ppu->renderer);

    if (ppu->window != NULL)
        SDL_DestroyWindow(ppu->window);

    free(ppu);
}

void init_screen_or_die(gba_ppu *ppu)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        goto init_error;

    ppu->window = SDL_CreateWindow("CGBA -- A Game Boy Advance Emulator",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   WINDOW_SCALE * FRAME_WIDTH,
                                   WINDOW_SCALE * FRAME_HEIGHT,
                                   SDL_WINDOW_OPENGL);

    if (ppu->window == NULL)
        goto init_error;

    ppu->renderer = SDL_CreateRenderer(ppu->window, -1, 0);

    if (ppu->renderer == NULL)
        goto init_error;

    ppu->screen = SDL_CreateTexture(ppu->renderer,
                                    SDL_PIXELFORMAT_XBGR1555,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    FRAME_WIDTH,
                                    FRAME_HEIGHT);

    if (ppu->screen == NULL)
        goto init_error;

    void *pixels;
    int pitch;
    if (SDL_LockTexture(ppu->screen, NULL, &pixels, &pitch) < 0)
        goto init_error;

    memcpy(pixels, ppu->frame_buffer, FRAME_HEIGHT * pitch);
    SDL_UnlockTexture(ppu->screen);
    SDL_RenderClear(ppu->renderer);
    SDL_RenderCopy(ppu->renderer, ppu->screen, NULL, NULL);
    SDL_RenderPresent(ppu->renderer);

    return;

init_error:
    fprintf(stderr, "Failed to initialize screen: %s\n", SDL_GetError());
    deinit_ppu(ppu);
    exit(1);
}

static void render_frame(gba_ppu *ppu)
{
    void *pixels;
    int pitch;

    if (SDL_LockTexture(ppu->screen, NULL, &pixels, &pitch) < 0)
        goto frame_render_error;

    memcpy(pixels, ppu->frame_buffer, pitch * FRAME_HEIGHT);
    SDL_UnlockTexture(ppu->screen);
    SDL_RenderClear(ppu->renderer);
    SDL_RenderCopy(ppu->renderer, ppu->screen, NULL, NULL);
    SDL_RenderPresent(ppu->renderer);
    ppu->frame_presented_signal = true;

    return;

frame_render_error:
    fprintf(stderr, "Error rendering frame: %s\n", SDL_GetError());
    exit(1);
}

static uint16_t get_bgcnt(gba_ppu *ppu, enum PPU_BGNO bgno)
{
    uint16_t bgcnt;
    switch (bgno)
    {
        case PPU_BG0: bgcnt = ppu->bg0cnt; break;
        case PPU_BG1: bgcnt = ppu->bg1cnt; break;
        case PPU_BG2: bgcnt = ppu->bg2cnt; break;
        case PPU_BG3: bgcnt = ppu->bg3cnt; break;
    }

    return bgcnt;
}

static uint16_t fetch_tile_map_entry(gba_ppu *ppu, enum PPU_BGNO bgno, int tile_idx)
{
    uint16_t bgcnt = get_bgcnt(ppu, bgno);
    int bgsize = (bgcnt >> 14) & 0x3;

    int w = text_bg_px_widths[bgsize];
    int effective_vcount = get_effective_vcount(ppu, bgno, bgsize);

    // the effective vcount, and tile index within the
    // currently addressed screen block of the bg map
    int sb_vcount = effective_vcount & (SB_PX_SIDE_LENGTH - 1);
    int sb_tile_idx = tile_idx & (SB_TILE_SIDE_LENGTH - 1);

    // The currently addressed screen block within the tile map.
    // For calculation details, see: https://www.coranac.com/tonc/text/regbg.htm#ssec-map-layout
    int screen_block_number = (effective_vcount / SB_PX_SIDE_LENGTH) * (w / SB_PX_SIDE_LENGTH)
                              + (tile_idx / SB_TILE_SIDE_LENGTH);

    int map_base_offset = (bgcnt >> 8) & 0x1f;
    uint32_t map_base_addr = VRAM_START + 2*KB*(map_base_offset + screen_block_number);
    uint32_t scanline_start = map_base_addr + 2*SB_TILE_SIDE_LENGTH*(sb_vcount / PX_PER_TILE);

    return read_halfword(ppu->mem, scanline_start + 2*sb_tile_idx);
}

static void fetch_pixel_data(gba_ppu *ppu, enum PPU_BGNO bgno, scanline_data *scdata)
{
    uint16_t bgcnt = get_bgcnt(ppu, bgno);
    int bgsize = (bgcnt >> 14) & 0x3;
    bool eight_bit_color = bgcnt & (1 << 7);
    int effective_vcount = get_effective_vcount(ppu, bgno, bgsize);
    int tile_vcount = effective_vcount % PX_PER_TILE;
    uint32_t tile_base_offset = (bgcnt >> 2) & 0x3;
    uint32_t tile_base_addr = VRAM_START + 16*KB*tile_base_offset;

    int tile_map_entry_number = -1;
    uint16_t tile_map_entry;
    tile_entry_data tile_data = {0};
    for (int pixels_fetched = 0; pixels_fetched < FRAME_WIDTH; ++pixels_fetched)
    {
        int effective_pixelno = get_effective_pixelno(ppu, bgno, bgsize, pixels_fetched);
        int tile_pixelno = effective_pixelno % PX_PER_TILE;

        int tmp_tile_entry_no = effective_pixelno / PX_PER_TILE;
        if (tmp_tile_entry_no != tile_map_entry_number)
        {
            tile_map_entry_number = tmp_tile_entry_no;
            tile_map_entry = fetch_tile_map_entry(ppu, bgno, tile_map_entry_number);
            populate_tile_data(tile_map_entry, &tile_data);
        }

        uint32_t yoffset = tile_data.yflip ? 7 - tile_vcount : tile_vcount;
        uint32_t line_addr = tile_base_addr;

        // NOTE: color 0 of each palette bank is not used and the pixel is instead transparent
        // This is encoded here as a palette color address of null (0)
        uint32_t color_addr = 0;
        uint32_t xoffset;
        uint32_t colorno;
        if (eight_bit_color)
        {
            line_addr += 64*tile_data.tileno + 8*yoffset;
            xoffset = tile_data.xflip ? 7 - tile_pixelno : tile_pixelno;
            colorno = read_byte(ppu->mem, line_addr + xoffset);

            if (colorno)
                color_addr = PRAM_START + 2*colorno;
        }
        else
        {
            int tile_byte = tile_pixelno / 2;
            line_addr += 32*tile_data.tileno + 4*yoffset;
            xoffset = tile_data.xflip ? 3 - tile_byte : tile_byte;
            uint8_t pixel_info = read_byte(ppu->mem, line_addr + xoffset);

            // each palette bank is 16 16-bit colors in size
            uint32_t palette_bank_addr = PRAM_START + 32 * tile_data.palette_bank;

            // pixel info arrangement: upper nibble = right, lower nibble = left
            if (tile_pixelno & 1 || tile_data.xflip)
                pixel_info >>= 4;
            colorno = pixel_info & 0xf;

            if (colorno)
                color_addr = palette_bank_addr + 2*colorno;
        }
        scdata->px_color_addrs[pixels_fetched] = color_addr;
    }
}

static void render_tile_data(gba_ppu *ppu, enum PPU_BGNO bgno, scanline_data *scdata)
{
    uint16_t bgcnt = get_bgcnt(ppu, bgno);
    if (bgcnt & (1 << 6))
    {
        fputs("Mosaic effect not implemented yet\n", stderr);
        exit(1);
    }

    fetch_pixel_data(ppu, bgno, scdata);

    for (int i = 0; i < FRAME_WIDTH; ++i)
    {
        uint32_t color_addr = scdata->px_color_addrs[i];

        if (scdata->px_transparency[i] && color_addr)
        {
            scdata->px_colors[i] = read_halfword(ppu->mem, color_addr);
            scdata->px_transparency[i] = false;
        }
    }
}

static void render_background(gba_ppu *ppu, enum PPU_BGNO bgno, scanline_data *scdata)
{
    render_tile_data(ppu, bgno, scdata);
}

static void render_mode0_scanline(gba_ppu *ppu)
{
    bool bg3_enabled = ppu->dispcnt & (1 << 11);
    bool bg2_enabled = ppu->dispcnt & (1 << 10);
    bool bg1_enabled = ppu->dispcnt & (1 << 9);
    bool bg0_enabled = ppu->dispcnt & (1 << 8);

    bool any_bg = bg0_enabled || bg1_enabled || bg2_enabled || bg3_enabled;
    if (!any_bg)
    {
        uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
        return;
    }

    scanline_data scdata;
    memset(scdata.px_transparency, 1, sizeof scdata.px_transparency);

    uint16_t backdrop = read_halfword(ppu->mem, PRAM_START);
    for (int i = 0; i < FRAME_WIDTH; ++i)
        scdata.px_colors[i] = backdrop;

    // Hardcoding the BG render order works for
    // Kirby - Nightmare in Dream Land because
    // the priority is fixed at BG0 > BG1 > BG2 > BG3.
    // TODO: actually take priority into account
    // instead of hardcoding draw order
    if (bg0_enabled)
        render_background(ppu, PPU_BG0, &scdata);

    if (bg1_enabled)
        render_background(ppu, PPU_BG1, &scdata);

    if (bg2_enabled)
        render_background(ppu, PPU_BG2, &scdata);

    if (bg3_enabled)
        render_background(ppu, PPU_BG3, &scdata);

    size_t framebuff_offset = FRAME_WIDTH * ppu->vcount;
    memcpy(ppu->frame_buffer + framebuff_offset, scdata.px_colors, sizeof scdata.px_colors);
}

static void render_mode3_scanline(gba_ppu *ppu)
{
    uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
    bool bg2_enabled = ppu->dispcnt & (1 << 10);

    if (bg2_enabled)
    {
        uint32_t addr;
        for (int i = 0; i < FRAME_WIDTH; ++i)
        {
            // XBGR1555 color format
            addr = VRAM_START + 2*(base_offset + i);
            ppu->frame_buffer[base_offset + i] = read_halfword(ppu->mem, addr);
        }
    }
    else
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
    }
}

static void render_mode4_scanline(gba_ppu *ppu)
{
    uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
    bool bg2_enabled = ppu->dispcnt & (1 << 10);
    bool frameno = ppu->dispcnt & (1 << 4);

    if (bg2_enabled)
    {
        uint32_t pixel_addr, palette_idx_addr;
        uint8_t palette_idx;
        for (int i = 0; i < FRAME_WIDTH; ++i)
        {
            palette_idx_addr = VRAM_START + frameno*0xa000 + base_offset + i;
            palette_idx = read_byte(ppu->mem, palette_idx_addr);
            pixel_addr = PRAM_START + 2*palette_idx;
            ppu->frame_buffer[base_offset + i] = read_halfword(ppu->mem, pixel_addr);
        }
    }
    else
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
    }
}

static void render_scanline(gba_ppu *ppu)
{
    if (ppu->dispcnt & (1 << 7)) // forced blank
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[FRAME_WIDTH * ppu->vcount + i] = WHITE;
    }
    else switch (ppu->dispcnt & 0x7) // PPU mode
    {
        case 0x3:
            render_mode3_scanline(ppu);
            break;

        case 0x4:
            render_mode4_scanline(ppu);
            break;

        case 0x0:
            render_mode0_scanline(ppu);
            break;

        default:
            fprintf(stderr,
                    "Error: Unimplemented BG mode: %d\n",
                    ppu->dispcnt & 0x7);
            exit(1);
    }
}

// Called on entering HBlank, including during VBlank scanlines
static void enter_hblank(gba_ppu *ppu)
{
    ppu->dispstat |= 0x2; // set HBlank flag

    if (ppu->dispstat & (1 << 4))
        ppu->mem->irq_request |= IRQ_HBLANK;

    if (ppu->vcount < VBLANK_START)
        render_scanline(ppu);
}

static void enter_vblank(gba_ppu *ppu)
{
    ppu->dispstat |= 0x1; // VBlank flag
    if (ppu->dispstat & (1 << 3))
        ppu->mem->irq_request |= IRQ_VBLANK;
    render_frame(ppu);
}

static void update_vcount(gba_ppu *ppu)
{
    ppu->scanline_clock = 0;
    ppu->dispstat &= ~0x2; // unset HBlank flag
    ppu->vcount = (ppu->vcount + 1) % NUM_SCANLINES;

    uint8_t lyc = ppu->dispstat >> 8;
    if (lyc == ppu->vcount)
    {
        ppu->dispstat |= 0x4; // V-Counter flag
        if (ppu->dispstat & (1 << 5))
            ppu->mem->irq_request |= IRQ_VCOUNT;
    }
    else
    {
        ppu->dispstat &= ~0x4;
    }
}

void run_ppu(gba_ppu *ppu, int num_clocks)
{
    for (; num_clocks; --num_clocks)
    {
        ++ppu->scanline_clock;

        if (ppu->scanline_clock == HBLANK_START)
            enter_hblank(ppu);
        else if (ppu->scanline_clock == SCANLINE_END)
            update_vcount(ppu);

        // beginning of new scanline
        if (!ppu->scanline_clock)
        {
            // VBlank is scanlines 160..226 (not 227)
            if (ppu->vcount == VBLANK_START)
                enter_vblank(ppu);
            else if (ppu->vcount == VBLANK_END)
                ppu->dispstat &= ~0x1;
        }
    }
}
