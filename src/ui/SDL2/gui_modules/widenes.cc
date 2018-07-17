#include "widenes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

WideNESModule::WideNESModule(SharedState& gui)
: GUIModule(gui)
{
  // register callbacks
  gui.nes._callbacks.cart_changed.add_cb(WideNESModule::cb_mapper_changed, this);
  gui.nes._ppu()._callbacks.frame_end.add_cb(WideNESModule::cb_ppu_frame_end, this);
  gui.nes._ppu()._callbacks.write_start.add_cb(WideNESModule::cb_ppu_write_start, this);
  gui.nes._ppu()._callbacks.write_end.add_cb(WideNESModule::cb_ppu_write_end, this);

  /*-------------------------------  SDL init  -------------------------------*/

  fprintf(stderr, "[GUI][wideNES] Initializing...\n");

  // make window
  this->sdl.window = SDL_CreateWindow(
    "anese",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    256 * this->gui.config.window_scale * 2.25,
    240 * this->gui.config.window_scale * 2.25,
    SDL_WINDOW_RESIZABLE
  );

  // make renderer
  this->sdl.renderer = SDL_CreateRenderer(
    this->sdl.window,
    -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );

  this->nes_screen = SDL_CreateTexture(
    this->sdl.renderer,
    SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING,
    256, 240
  );

  // Allow opacity
  SDL_SetRenderDrawBlendMode(this->sdl.renderer, SDL_BLENDMODE_BLEND);

  this->sdl.inprint = new SDL2_inprint(this->sdl.renderer);

  // make menu submodule
  this->menu_submodule = new MenuSubModule(gui, this->sdl.window, this->sdl.renderer);
}

WideNESModule::~WideNESModule() {
  fprintf(stderr, "[GUI][wideNES] Shutting down...\n");

  for (auto col : this->tiles) {
    for (auto row : col.second) {
      const Tile* tile = row.second;
      delete tile;
    }
  }

  /*------------------------------  SDL Cleanup  -----------------------------*/

  SDL_DestroyTexture(this->nes_screen);

  delete this->sdl.inprint;

  SDL_DestroyRenderer(this->sdl.renderer);
  SDL_DestroyWindow(this->sdl.window);
}

void WideNESModule::input(const SDL_Event& event) {
  this->menu_submodule->input(event);
  if (this->gui.status.in_menu) return;

  // Update from Mouse
  if (event.type == SDL_MOUSEBUTTONDOWN) {
    this->pan.last_mouse_pos.x = event.button.x;
    this->pan.last_mouse_pos.y = event.button.y;
    this->pan.active = true;
  }

  if (event.type == SDL_MOUSEBUTTONUP) {
    this->pan.active = false;
  }

  if (event.type == SDL_MOUSEMOTION) {
    if (this->pan.active) {
      this->pan.dx += event.motion.x - this->pan.last_mouse_pos.x;
      this->pan.dy += event.motion.y - this->pan.last_mouse_pos.y;
      this->pan.last_mouse_pos.x = event.motion.x;
      this->pan.last_mouse_pos.y = event.motion.y;
    }
  }

  if (event.type == SDL_MOUSEWHEEL) {
    int scroll = event.wheel.y;
    if (scroll > 0) { // scroll up
      while (scroll--) this->pan.zoom *= 1.25;
    } else if(scroll < 0) { // scroll down
      while (scroll++) this->pan.zoom /= 1.25;
    }
  }

  bool forward_to_emu_module = true;

  if (event.type == SDL_KEYDOWN) {
    bool mod_shift = event.key.keysym.mod & KMOD_SHIFT;
    switch (event.key.keysym.sym) {
    case SDLK_ESCAPE: forward_to_emu_module = false; break;
    case SDLK_k: {
      for (auto col : this->tiles)
        for (auto row : col.second)
          delete row.second;
      this->tiles.clear();
    } break;
    case SDLK_e: this->pad.offset.t += mod_shift ? 1 : 8; break;
    case SDLK_3: this->pad.offset.t -= mod_shift ? 1 : 8; break;
    case SDLK_d: this->pad.offset.b += mod_shift ? 1 : 8; break;
    case SDLK_c: this->pad.offset.b -= mod_shift ? 1 : 8; break;
    case SDLK_s: this->pad.offset.l += mod_shift ? 1 : 8; break;
    case SDLK_a: this->pad.offset.l -= mod_shift ? 1 : 8; break;
    case SDLK_f: this->pad.offset.r += mod_shift ? 1 : 8; break;
    case SDLK_g: this->pad.offset.r -= mod_shift ? 1 : 8; break;
    }
  }

  if (forward_to_emu_module)
    this->gui.modules.at("emu")->input(event);
}

void WideNESModule::update() {
  this->menu_submodule->update();
  // aside from that, nothing.
  // updates happen in callbacks
}

/*----------------------------  Tile Definitions  ----------------------------*/

WideNESModule::Tile::Tile(SDL_Renderer* renderer, int x, int y) {
  this->x = x;
  this->y = y;
  this->texture_done = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING,
    256, 240
  );
  this->texture_curr = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING,
    256, 240
  );

  SDL_SetTextureBlendMode(this->texture_done, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(this->texture_curr, SDL_BLENDMODE_BLEND);
}

WideNESModule::Tile::~Tile() {
  SDL_DestroyTexture(this->texture_done);
  SDL_DestroyTexture(this->texture_curr);
}

/*-------------------------------  Callbacks  --------------------------------*/

void WideNESModule::cb_ppu_write_start(void* self, u16 addr, u8 val) {
  ((WideNESModule*)self)->ppu_write_start_handler(addr, val);
}

void WideNESModule::cb_ppu_write_end(void* self, u16 addr, u8 val) {
  ((WideNESModule*)self)->ppu_write_end_handler(addr, val);
}

void WideNESModule::cb_mapper_changed(void* self, Mapper* mapper) {
  if (mapper && mapper->mapper_number() == 4) {
    Mapper_004* mmc3 = static_cast<Mapper_004*>(mapper);
    mmc3->_did_irq_callbacks.add_cb(WideNESModule::cb_mmc3_irq, self);
  }
}

void WideNESModule::cb_mmc3_irq(void* self, Mapper_004* mmc3, bool irq_enabled) {
  ((WideNESModule*)self)->mmc3_irq_handler(mmc3, irq_enabled);
}

void WideNESModule::cb_ppu_frame_end(void* self) {
  ((WideNESModule*)self)->ppu_frame_end_handler();
}

/*----------------------------  Callback Handlers  ---------------------------*/

#include "nes/ppu/ppu.h"

void WideNESModule::ppu_write_start_handler(u16 addr, u8 val) {
  (void)addr;
  (void)val;
  // nothing... for now
}

void WideNESModule::ppu_write_end_handler(u16 addr, u8 val) {
  const PPU& ppu = this->gui.nes._ppu();
  const PPU::Registers& ppu_reg = ppu._reg();

  using namespace PPURegisters;

  switch (addr) {
  case PPUSCROLL: {
    if (ppu_reg.scroll_latch == 1) this->h.ppuscroll.curr.x = val;
    if (ppu_reg.scroll_latch == 0) this->h.ppuscroll.curr.y = val;
  } break;
  case PPUADDR: {
    this->h.ppuaddr.did_change = true;

    if (ppu._scanline() < 241 && ppu_reg.ppumask.is_rendering)
      fprintf(stderr, "%3u | 0x%04X <- 0x%02X\n", ppu._scanline(), addr, val);

    this->h.ppuaddr.changed.while_rendering = ppu_reg.ppumask.is_rendering;
    this->h.ppuaddr.changed.on_scanline = ppu._scanline();

    if (ppu_reg.scroll_latch == 1) this->h.ppuaddr.new_scroll.x = 8 * ppu_reg.t.coarse_x;
    if (ppu_reg.scroll_latch == 0) this->h.ppuaddr.new_scroll.y = 8 * ppu_reg.t.coarse_y;
  } break;
  }
}

void WideNESModule::mmc3_irq_handler(Mapper_004* mmc3, bool irq_enabled) {
  this->h.mmc3_irq.scroll_pre_irq = this->h.ppuscroll.curr;

  this->h.mmc3_irq.happened = true;
  this->h.mmc3_irq.on_scanline = irq_enabled
    ? mmc3->peek_irq_latch()
    : 239; // cancels out later, give 0 padding
}

void WideNESModule::ppu_frame_end_handler() {
  const PPU& ppu = this->gui.nes._ppu();

  // save copy of OG screen
  const u8* framebuffer_true;
  ppu.getFramebuff(&framebuffer_true);
  SDL_UpdateTexture(this->nes_screen, nullptr, framebuffer_true, 256 * 4);

  /*-----------------------------  Heuristics  -----------------------------*/

  // - the OG - set current scroll values based off of the PPUSCROLL register
  this->curr_scroll.x = this->h.ppuscroll.curr.x;
  this->curr_scroll.y = this->h.ppuscroll.curr.y;

  // - if the left-column bit is enabled, odds are the game is hiding visual
  // artifacts, so we can slice that bit off.
  this->pad.guess.l = ppu._reg().ppumask.m ? 0 : 8;

  // - Zelda scrolling: writes to PPUADDR midframe are probably some creative
  // scroll implementation?
  if (this->h.ppuaddr.did_change) {
    // ppuaddr is written to a lot, but we only care about writes that occur
    // mid-frame, since those are the one that lead to scrolling / status bars.
    if (this->h.ppuaddr.changed.on_scanline < 241 &&
        this->h.ppuaddr.changed.while_rendering) {
      // note that this heuristic is in-play, and mark the scanline...
      this->h.ppuaddr.active = true;
      this->h.ppuaddr.cut_scanline = this->h.ppuaddr.changed.on_scanline;

      // lob off the chunk of the screen not being scrolled...
      if (this->h.ppuaddr.cut_scanline < 240 / 2) {
        // top of screen
        this->pad.guess.t = this->h.ppuaddr.cut_scanline;
      } else {
        // bottom of screen
        this->pad.guess.b = 239 - this->h.mmc3_irq.on_scanline;
      }

      // set the scroll...
      this->curr_scroll.y = this->h.ppuaddr.new_scroll.y;
    }
  }
  this->h.ppuaddr.did_change = false; // reset each frame


  // - Mappers sometimes use a scanline IRQ to split the screen, many times for
  // making a static status bar at the bottom of the screen (kirby, smb3)
  // TODO: make this more robust, i.e: get rid of false positives (Megaman IV)
  if (this->h.mmc3_irq.happened) {
    // depending on if the menu is at the top / bottom of the screen, different
    // scroll values should be used, and different parts of the screen should be
    // cut-off
    if (this->h.mmc3_irq.on_scanline < 240 / 2) {
      // top of screen
      this->pad.guess.t = this->h.mmc3_irq.on_scanline;
      // this->curr_scroll stays the same
    } else {
      // bottom of screen
      this->pad.guess.b = 239 - this->h.mmc3_irq.on_scanline;
      this->curr_scroll = this->h.mmc3_irq.scroll_pre_irq;
    }
  }
  this->h.mmc3_irq.happened = false; // reset each frame

  // - vertically scrolling + vertical mirroring usually leads to artifacting
  // at the top of the screen
  // TODO: implement me
  // TODO: implement inverse

  /*------------------  Padding / Scrolling Calculations  ------------------*/

  // calculate final padding
  this->pad.total.l = std::max(0, this->pad.guess.l + this->pad.offset.l);
  this->pad.total.r = std::max(0, this->pad.guess.r + this->pad.offset.r);
  this->pad.total.t = std::max(0, this->pad.guess.t + this->pad.offset.t);
  this->pad.total.b = std::max(0, this->pad.guess.b + this->pad.offset.b);

  // calculate the new scroll position

  int scroll_dx = this->curr_scroll.x - this->last_scroll.x;
  int scroll_dy = this->curr_scroll.y - this->last_scroll.y;

  const int fuzz = 10;
  const int thresh_w = (256 - this->pad.total.l - this->pad.total.r) - fuzz;
  const int thresh_h = (240 - this->pad.total.t - this->pad.total.b) - fuzz;

  // 255 -> 0 | scroll_dx is negative | means we are going right
  // 0 -> 255 | scroll_dx is positive | means we are going left
  if (::abs(scroll_dx) > thresh_w) {
    if (scroll_dx < 0) scroll_dx += 256; // going right
    else               scroll_dx -= 256; // going left
  }
  // 239 -> 0 | scroll_dy is negative | means we are going down
  // 0 -> 239 | scroll_dy is positive | means we are going up
  if (::abs(scroll_dy) > thresh_h) {
    if (scroll_dy < 0) scroll_dy += 240; // going down
    else               scroll_dy -= 240; // going up
  }

  // Zelda scroll heuristic
  // not entirely sure why this jump happens... but this fixes it?
  if (this->h.ppuaddr.active) {
    if (::abs(scroll_dy) > (int)this->h.ppuaddr.cut_scanline) {
      scroll_dy = 0;
    }
  }

  this->scroll.x += scroll_dx;
  this->scroll.y += scroll_dy;
  this->scroll.dx = scroll_dx;
  this->scroll.dy = scroll_dy;

  this->last_scroll.x = this->curr_scroll.x;
  this->last_scroll.y = this->curr_scroll.y;

  /*----------------------------  Tile Updates  ----------------------------*/

  // use the background framebuffer (sprites leave artifacts)
  const u8* framebuffer;
  ppu.getFramebuffBgr(&framebuffer);

  // 1) For every source-pixel on the NES screen, update the associated pixel
  //    within the appropriate tile
  // - also keep track of how many pixels are written to every 16x16 segment of
  //   the screen, as we want to sample on a 16x16 tile basis. This prevents
  //   smearing on animated tiles

  // sx/sy = soruce pixel (from NES screen)
  for (int sx = this->pad.total.l; sx < 256 - this->pad.total.r; sx++) {
    for (int sy = this->pad.total.t; sy < 240 - this->pad.total.b; sy++) {
      assert(sx >= 0 && sx < 256 && sy >= 0 && sy < 240);

      // tx/ty = "big tile" that sx/sy is currently in
      const int tx = ::floor((this->scroll.x + sx) / 256.0);
      const int ty = ::floor((this->scroll.y + sy) / 240.0);
      Tile* tile = this->tiles[tx][ty];
      if (!tile)
        tile = this->tiles[tx][ty] = new Tile (this->sdl.renderer, tx, ty);

      // dx/dy = destination pixel within big-tile
      const uint dx = (this->scroll.x - tx * 256) + sx;
      const uint dy = (this->scroll.y - ty * 240) + sy;

      // bx/by = block index dx/dy is currently in
      // i.e: NES screen is made of 16x16 blocks, 16 wide 15 tall
      const int bx = ::floor(dx / 16.0);
      const int by = ::floor(dy / 16.0);

      // There are 2 ways to recrod the screen: sample the _first_ thing that
      //  appears, or sample the _last_ thing that appears.
      // Sampling the _first_ thing that appears is cool beacause the generated
      //  scene is representative of the "initial state" of the map
      // Sampling the _last_ thing that appears is cool because that matches the
      //  progress you (as a player) have made.
      // There is no clear answer which is "better," so i'm leaving both options
      // TODO: make this toggleable with a flag

      // if (tile->done[bx][by]) continue; // comment out to sample _last_ thing

      tile->fill[bx][by] += 1;

      const uint spx_i = (256 * 4 * sy) + (4 * sx);
      const uint dpx_i = (256 * 4 * dy) + (4 * dx);
      tile->fb_new[dpx_i + 0] = framebuffer[spx_i + 0];
      tile->fb_new[dpx_i + 1] = framebuffer[spx_i + 1];
      tile->fb_new[dpx_i + 2] = framebuffer[spx_i + 2];
      tile->fb_new[dpx_i + 3] = framebuffer[spx_i + 3];
    }
  }

  // 2) Save any 16x16 blocks that have been captured this frame
  // Note: there can be 4 big tiles that intersect the screen at any given time,
  // so all of them should be updated.
  const int tx = ::floor(this->scroll.x / 256.0);
  const int ty = ::floor(this->scroll.y / 240.0);
  for (int dx : {0, 1}) {
    for (int dy : {0, 1}) {
      Tile* tile = this->tiles[tx + dx][ty + dy];
      if (tile == nullptr) continue;

      for (int bx = 0; bx < 16; bx++) {
        for (int by = 0; by < 15; by++) {
          if (tile->fill[bx][by] == 256) {
            tile->done[bx][by] = true; // mark block as fully filled-in

            // update the framebuffer with the captured 16x16 tile
            for (int x = 0; x < 16; x++) {
              for (int y = 0; y < 16; y++) {
                const uint px_i = (256 * 4 * (by * 16 + y))
                                +       (4 * (bx * 16 + x));
                tile->fb[px_i + 0] = tile->fb_new[px_i + 0];
                tile->fb[px_i + 1] = tile->fb_new[px_i + 1];
                tile->fb[px_i + 2] = tile->fb_new[px_i + 2];
                tile->fb[px_i + 3] = tile->fb_new[px_i + 3];
              }
            }
          }

          tile->fill[bx][by] = 0; // clear fill state on every iter
        }
      }
    }
  }

  // 3) Actually update the textures
  for (int dx : {0, 1}) {
    for (int dy : {0, 1}) {
      Tile* tile = this->tiles[tx + dx][ty + dy];
      if (tile == nullptr) continue;

      SDL_UpdateTexture(tile->texture_curr, nullptr, tile->fb_new, 256 * 4);
      SDL_UpdateTexture(tile->texture_done, nullptr, tile->fb,     256 * 4);
    }
  }
}

/*---------------------------------  Output  ---------------------------------*/

void WideNESModule::output() {
  // calculate origin (where to render NES screen / where to offset tiles from)
  const int nes_w = 256 * this->pan.zoom;
  const int nes_h = 240 * this->pan.zoom;
  int window_w, window_h;
  SDL_GetWindowSize(this->sdl.window, &window_w, &window_h);

  const SDL_Rect origin {
    (window_w - nes_w) / 2 + this->pan.dx,
    (window_h - nes_h) / 2 + this->pan.dy,
    nes_w, nes_h
  };

  SDL_SetRenderDrawColor(this->sdl.renderer, 0, 0, 0, 0xff);
  SDL_RenderClear(this->sdl.renderer);

  // wideNES tiles
  for (auto col : this->tiles) {
    for (auto row : col.second) {
      const Tile* tile = row.second;

      if (tile == nullptr) continue;

      SDL_Rect offset = origin;
      offset.x -= this->pan.zoom * (this->scroll.x - tile->x * 256);
      offset.y -= this->pan.zoom * (this->scroll.y - tile->y * 240);

      // render the latest state for the tile first, but then overlay the 16x16
      // block version over that.
      // thus, we get "smooth" edges, but also clean blocks (except at edges)
      SDL_RenderCopy(this->sdl.renderer, tile->texture_curr, nullptr, &offset);
      SDL_RenderCopy(this->sdl.renderer, tile->texture_done, nullptr, &offset);

      SDL_SetRenderDrawColor(this->sdl.renderer, 0x60, 0x60, 0x60, 0xff);
      SDL_RenderDrawRect(this->sdl.renderer, &offset);

      this->sdl.inprint->set_color(0xff0000);
      char buf [64];
      sprintf(buf, "(%d, %d)", tile->x, tile->y);
      this->sdl.inprint->print(buf, offset.x + 8, offset.y + 8);
    }
  }

  // actual NES screen

  // draw most of the screen (minus left-col) transparently
  SDL_Rect col_origin = origin;
  col_origin.x += this->pan.zoom * (this->pad.total.l);
  col_origin.w -= this->pan.zoom * (this->pad.total.l + this->pad.total.r);

  SDL_Rect col_clip;
  col_clip.x = (this->pad.total.l);
  col_clip.y = 0;
  col_clip.w = 256 - (this->pad.total.l + this->pad.total.r);
  col_clip.h = 240;
  SDL_SetTextureBlendMode(this->nes_screen, SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaMod(this->nes_screen, 100);
  SDL_RenderCopy(this->sdl.renderer, this->nes_screen, &col_clip, &col_origin);

  // draw clipped part of the screen with no transparency
  SDL_Rect padded_origin = origin;
  padded_origin.x += this->pan.zoom * (this->pad.total.l);
  padded_origin.y += this->pan.zoom * (this->pad.total.t);
  padded_origin.w -= this->pan.zoom * (this->pad.total.l + this->pad.total.r);
  padded_origin.h -= this->pan.zoom * (this->pad.total.t + this->pad.total.b);

  SDL_Rect padded_clip;
  padded_clip.x = (this->pad.total.l);
  padded_clip.y = (this->pad.total.t);
  padded_clip.w = 256 - (this->pad.total.l + this->pad.total.r);
  padded_clip.h = 240 - (this->pad.total.t + this->pad.total.b);

  SDL_SetTextureAlphaMod(this->nes_screen, 255);
  SDL_RenderCopy(this->sdl.renderer, this->nes_screen, &padded_clip, &padded_origin);

  // full-screen box
  // SDL_SetRenderDrawColor(this->sdl.renderer, 0xff, 0, 0, 0xff);
  // SDL_RenderDrawRect(this->sdl.renderer, &origin);

  // clipped-screen box
  SDL_SetRenderDrawColor(this->sdl.renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderDrawRect(this->sdl.renderer, &padded_origin);

  // debug values
  this->sdl.inprint->set_color(0xff0000);
  char buf [128];
  sprintf(buf,
    "total scroll: %-3d %-3d\n"
    " last scroll: %-3d %-3d\n"
    "       dx dy: %-3d %-3d\n",
    this->scroll.x, this->scroll.y,
    this->last_scroll.x, this->last_scroll.y,
    this->scroll.dx, this->scroll.dy
  );
  this->sdl.inprint->print(buf, 8, 8);

  this->menu_submodule->output();

  SDL_RenderPresent(this->sdl.renderer);
}
