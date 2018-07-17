#pragma once

#include <map>

#include <SDL.h>
#include <SDL_inprint2.h>

#include "../config.h"
#include "module.h"

#include "submodules/menu.h"

#include "nes/cartridge/mappers/mapper_004.h"

class WideNESModule : public GUIModule {
private:
  /*---------------------------  SDL / GUI stuff  ----------------------------*/

  struct {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL2_inprint* inprint  = nullptr;
  } sdl;

  SDL_Texture* nes_screen; // copy of actual NES screen

  // zoom/pan info
  struct {
    bool active = false;
    struct { int x; int y; } last_mouse_pos { 0, 0 };
    int dx = 0;
    int dy = 0;
    float zoom = 2.0;
  } pan;

  /*----------------------------  Tile Rendering  ----------------------------*/

  struct Tile {
    int x, y;

    bool done [16][15] = {{false}};
    int  fill [16][15] = {{0}};

    SDL_Texture* texture_done = nullptr;
    SDL_Texture* texture_curr = nullptr;

    u8 fb     [256 * 240 * 4] = {0};
    u8 fb_new [256 * 240 * 4] = {0};

    // misc: not initializing the framebuffer leads to a really cool effect
    // where any raw memory is visualized as a texture!

    ~Tile();
    Tile(SDL_Renderer* renderer, int x, int y);
  };

  // tilemap
  std::map<int, std::map<int, Tile*>> tiles;

  // there tend to be graphical artifacts at the edge of the screen, so it's
  // prudent to sample sliglty away from the edge.
  // moreover, some games have static menus on screen that impair sampling
  // (eg: smb3, mc kids)
  struct {
    struct {
      int l, r, t, b;
    } guess  { 0, 0, 0, 0 }  // intelligent guess
    , offset { 0, 0, 0, 0 }  // user offset
    , total  { 0, 0, 0, 0 }; // sum of the two
  } pad;

  struct nes_scroll { u8 x; u8 y; };
  nes_scroll last_scroll { 0, 0 };
  nes_scroll curr_scroll { 0, 0 };

  // total scroll (offset from origin)
  struct {
    int x, y;
    int dx, dy;
  } scroll { 0, 0, 0, 0 };

  /*------------------------------  Heuristics  ------------------------------*/

  struct {
    // The OG heuristic: Sniffing the PPUSCROLL registers for changes
    struct {
      nes_scroll curr { 0, 0 };
    } ppuscroll;

    // MMC3 Interrupt handling (i.e: intelligently chopping the screen)
    // (SMB3, M.C Kids)
    struct {
      bool happened = false;
      uint on_scanline = 239;
      nes_scroll scroll_pre_irq = { 0, 0 };
    } mmc3_irq;

    // PPUADDR mid-frame changes
    // (Zelda)
    struct {
      bool did_change = false;
      struct {
        uint on_scanline = 0;
        bool while_rendering = 0;
      } changed;

      bool active = false;

      uint frame_counter = 0; // TEMP: should not be needed with scene detection

      uint cut_scanline = 0;
      nes_scroll new_scroll = { 0, 0 };
    } ppuaddr;
  } h;

  /*---------------------------  Callback Handlers  --------------------------*/

  void ppu_frame_end_handler();
  void ppu_write_start_handler(u16 addr, u8 val);
  void ppu_write_end_handler(u16 addr, u8 val);

  void mmc3_irq_handler(Mapper_004* mapper, bool active);

  static void cb_mapper_changed(void* self, Mapper* cart);

  static void cb_ppu_frame_end(void* self);
  static void cb_ppu_write_start(void* self, u16 addr, u8 val);
  static void cb_ppu_write_end(void* self, u16 addr, u8 val);

  static void cb_mmc3_irq(void* self, Mapper_004* mapper, bool active);

  MenuSubModule* menu_submodule;

public:
  virtual ~WideNESModule();
  WideNESModule(SharedState& gui);

  void input(const SDL_Event&) override;
  void update() override;
  void output() override;

  uint get_window_id() override { return SDL_GetWindowID(this->sdl.window); }
};
