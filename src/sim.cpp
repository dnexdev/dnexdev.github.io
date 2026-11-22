#include <stdint.h>

#define WASM_EXPORT(name) __attribute__((export_name(name)))

extern "C" {
void* memset(void* dst, int value, unsigned long n) {
  auto* p = static_cast<uint8_t*>(dst);
  uint8_t b = static_cast<uint8_t>(value);
  for (unsigned long i = 0; i < n; ++i) p[i] = b;
  return dst;
}
void* memcpy(void* dst, const void* src, unsigned long n) {
  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  for (unsigned long i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}
void* memmove(void* dst, const void* src, unsigned long n) {
  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  if (d < s) {
    for (unsigned long i = 0; i < n; ++i) d[i] = s[i];
  } else if (d > s) {
    for (unsigned long i = n; i > 0; --i) d[i - 1] = s[i - 1];
  }
  return dst;
}
}

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;

constexpr int MAX_COLS = 280;
constexpr int MAX_ROWS = 180;
constexpr int MAX_CELLS = MAX_COLS * MAX_ROWS;

constexpr int MAX_JELLIES = 4;
constexpr int TENTACLES_PER_JELLY = 9;
constexpr int MAX_NODES = 18;

constexpr int MAX_KOI = 5;
constexpr int KOI_NODES = 20;

constexpr int MAX_PELLETS = 12;
constexpr int MAX_RIPPLES = 24;
constexpr float FIXED_DT = 1.0f; // 60Hz fixed physics tick

constexpr char TILE_GLYPHS[7] = {'t', '%', 'S', 'X', '8', '@', '#'};
constexpr float SUBSAMPLES[8] = {0.25f, 0.25f, 0.75f, 0.25f, 0.25f, 0.75f, 0.75f, 0.75f};

// ---- Fast freestanding math ----
inline float f_abs(float x) { return x < 0.0f ? -x : x; }
inline bool f_finite(float x) { return (x == x) && (f_abs(x) < 1e20f); }
inline float f_sqrt(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }
inline float f_hypot(float x, float y) { return f_sqrt(x * x + y * y); }
inline float f_min(float a, float b) { return a < b ? a : b; }
inline float f_max(float a, float b) { return a > b ? a : b; }
inline float f_clamp(float x, float lo, float hi) { return f_max(lo, f_min(hi, x)); }
inline int i_floor(float x) {
  int i = static_cast<int>(x);
  return (x < static_cast<float>(i)) ? (i - 1) : i;
}
inline int i_ceil(float x) {
  int i = static_cast<int>(x);
  return (x > static_cast<float>(i)) ? (i + 1) : i;
}
inline float smoothstep(float e0, float e1, float x) {
  float t = f_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline float wrap_pi(float x) {
  x -= TAU * static_cast<float>(static_cast<int>(x / TAU));
  if (x > PI) x -= TAU;
  if (x < -PI) x += TAU;
  return x;
}

inline float wrap_tau(float x) {
  x -= TAU * static_cast<float>(static_cast<int>(x / TAU));
  if (x < 0.0f) x += TAU;
  return x;
}

inline float f_sin(float x) {
  x = wrap_pi(x);
  constexpr float B = 4.0f / PI;
  constexpr float C = -4.0f / (PI * PI);
  float y = B * x + C * x * f_abs(x);
  return 0.225f * (y * f_abs(y) - y) + y;
}

inline float f_cos(float x) { return f_sin(x + HALF_PI); }

inline float f_atan2(float y, float x) {
  if (x == 0.0f) {
    if (y > 0.0f) return HALF_PI;
    if (y < 0.0f) return -HALF_PI;
    return 0.0f;
  }
  float ay = f_abs(y);
  float ax = f_abs(x);
  float mn = f_min(ax, ay);
  float mx = f_max(ax, ay);
  float a = mn / mx;
  float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = HALF_PI - r;
  if (x < 0.0f) r = PI - r;
  if (y < 0.0f) r = -r;
  return r;
}

// Fast approximate pow(x, p) for x in [0, 1] using exp2/log2 polynomial or direct roots
inline float f_pow_055(float x) {
  if (x <= 0.0f) return 0.0f;
  float s = f_sqrt(x);       // x^0.5
  return s * f_sqrt(f_sqrt(f_sqrt(f_sqrt(x)))); // x^0.5625 (virtually identical to x^0.55)
}
inline float f_pow_080(float x) {
  if (x <= 0.0f) return 0.0f;
  float s = f_sqrt(x);
  return s * f_sqrt(s) * f_sqrt(f_sqrt(f_sqrt(x))); // x^0.8125
}

inline float hash3(int a, int b, int c) {
  uint32_t h = static_cast<uint32_t>(a * 374761393 + b * 668265263 + c * 1442695041);
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
}

inline float value_noise(float x, float y, int seed) {
  int xi = i_floor(x);
  int yi = i_floor(y);
  float u = smoothstep(0.0f, 1.0f, x - static_cast<float>(xi));
  float v = smoothstep(0.0f, 1.0f, y - static_cast<float>(yi));
  float a = hash3(xi, yi, seed);
  float b = hash3(xi + 1, yi, seed);
  float c = hash3(xi, yi + 1, seed);
  float d = hash3(xi + 1, yi + 1, seed);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

inline uint8_t line_glyph(float dx, float dy) {
  float d = f_hypot(dx, dy);
  if (d < 1e-4f) d = 1.0f;
  if (f_abs(dy) < 0.38f * d) return '-';
  if (f_abs(dx) < 0.38f * d) return '|';
  return (dx * dy > 0.0f) ? '\\' : '/';
}

// ---- RGB Color & Theme Palettes (Dao-Ho exact DARK & LIGHT + Jellyfish ramps) ----
struct Rgb {
  uint8_t r, g, b;
};

inline uint32_t pack_rgb(Rgb c) {
  return (static_cast<uint32_t>(c.r) << 16) |
         (static_cast<uint32_t>(c.g) << 8) |
         static_cast<uint32_t>(c.b);
}

inline Rgb mix_rgb(Rgb a, Rgb b, float k) {
  k = f_clamp(k, 0.0f, 1.0f);
  return {
      static_cast<uint8_t>(static_cast<float>(a.r) + (static_cast<float>(b.r) - static_cast<float>(a.r)) * k),
      static_cast<uint8_t>(static_cast<float>(a.g) + (static_cast<float>(b.g) - static_cast<float>(a.g)) * k),
      static_cast<uint8_t>(static_cast<float>(a.b) + (static_cast<float>(b.b) - static_cast<float>(a.b)) * k),
  };
}

inline Rgb shade_rgb(Rgb c, float k) {
  float r = f_min(255.0f, static_cast<float>(c.r) * k);
  float g = f_min(255.0f, static_cast<float>(c.g) * k);
  float b = f_min(255.0f, static_cast<float>(c.b) * k);
  return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
}

inline Rgb ramp6(const Rgb stops[6], float t) {
  float x = f_clamp(t, 0.0f, 1.0f) * 5.0f;
  int i = static_cast<int>(x);
  if (i > 4) i = 4;
  return mix_rgb(stops[i], stops[i + 1], x - static_cast<float>(i));
}

struct Palette {
  Rgb bg;
  Rgb white;
  Rgb black;
  Rgb fin;
  Rgb eye;
  Rgb water;
  Rgb pellet;
  Rgb shadow;
  float shadow_alpha;
  float alpha;
  float depth_floor;
  float tile0;
  float tile1;
  float ink;
  Rgb hi[6];
  // 4 jellyfish species ramps (shadow -> mid -> bright dome)
  Rgb jelly_ramps[4][6];
  Rgb jelly_core[4];
};

constexpr Palette PAL_DARK = {
    /*bg*/ {24, 23, 21},
    /*white*/ {236, 228, 218},
    /*black*/ {92, 90, 108},
    /*fin*/ {214, 206, 198},
    /*eye*/ {20, 20, 26},
    /*water*/ {255, 247, 240},
    /*pellet*/ {236, 170, 92},
    /*shadow*/ {0, 0, 0},
    /*shadow_alpha*/ 0.55f,
    /*alpha*/ 0.86f,
    /*depth_floor*/ 0.45f,
    /*tile0, tile1*/ 0.68f, 0.98f,
    /*ink*/ 0.42f,
    /*hi*/ {
        {112, 22, 24},
        {168, 34, 28},
        {214, 58, 30},
        {240, 96, 34},
        {250, 138, 52},
        {255, 178, 96},
    },
    /*jelly_ramps*/ {
        // 0: Cyan-teal sea jelly
        {{22, 74, 88}, {34, 118, 136}, {56, 166, 184}, {88, 208, 222}, {132, 230, 238}, {182, 244, 248}},
        // 1: Amethyst moon jelly
        {{62, 42, 102}, {98, 66, 152}, {134, 96, 198}, {172, 132, 236}, {204, 168, 248}, {228, 204, 255}},
        // 2: Coral sea nettle
        {{108, 34, 34}, {158, 52, 46}, {204, 78, 66}, {236, 114, 94}, {248, 154, 128}, {255, 196, 170}},
        // 3: Seafoam emerald jelly
        {{24, 78, 66}, {38, 124, 102}, {58, 172, 138}, {88, 214, 172}, {132, 236, 200}, {182, 248, 224}},
    },
    /*jelly_core*/ {{250, 196, 98}, {116, 224, 236}, {252, 198, 108}, {248, 192, 96}},
};

constexpr Palette PAL_LIGHT = {
    /*bg*/ {245, 244, 241},
    /*white*/ {112, 108, 104},
    /*black*/ {36, 33, 40},
    /*fin*/ {138, 132, 126},
    /*eye*/ {24, 22, 26},
    /*water*/ {38, 37, 35},
    /*pellet*/ {176, 108, 40},
    /*shadow*/ {60, 58, 56},
    /*shadow_alpha*/ 0.22f,
    /*alpha*/ 0.92f,
    /*depth_floor*/ 0.58f,
    /*tile0, tile1*/ 0.78f, 0.55f,
    /*ink*/ 0.45f,
    /*hi*/ {
        {138, 24, 22},
        {184, 38, 24},
        {222, 62, 28},
        {238, 100, 38},
        {244, 140, 66},
        {248, 176, 116},
    },
    /*jelly_ramps*/ {
        // 0: Deep Prussian teal -> soft ocean wash
        {{14, 72, 88}, {24, 102, 122}, {44, 134, 154}, {76, 162, 180}, {118, 188, 200}, {162, 212, 220}},
        // 1: Deep royal plum -> soft lavender wash
        {{58, 34, 104}, {84, 52, 142}, {116, 78, 174}, {148, 110, 200}, {178, 148, 218}, {206, 186, 232}},
        // 2: Deep terracotta coral -> warm blush wash
        {{138, 32, 28}, {176, 52, 42}, {210, 78, 62}, {228, 112, 92}, {238, 148, 128}, {244, 184, 168}},
        // 3: Deep pine jade -> soft sage wash
        {{18, 82, 62}, {28, 112, 86}, {48, 144, 112}, {78, 174, 142}, {118, 198, 172}, {162, 218, 198}},
    },
    /*jelly_core*/ {{192, 116, 20}, {22, 116, 134}, {196, 118, 24}, {188, 112, 22}},
};

constexpr int FLAG_BODY = 1;
constexpr int FLAG_FIN = 2;
constexpr int FLAG_EYE = 3;
constexpr int FLAG_DORSAL = 4;
constexpr int FLAG_BARBEL = 5;
constexpr float BODY_END = 0.74f;

struct ShapeHit {
  int flag;
  float across;
};

inline float koi_body_half_width(float s) {
  if (s < 0.0f) return 0.0f;
  if (s < 0.12f) return 0.11f * f_pow_055(f_sin((s / 0.12f) * HALF_PI));
  if (s < 0.30f) return 0.11f + 0.025f * smoothstep(0.12f, 0.30f, s);
  return 0.033f + 0.102f * (1.0f - smoothstep(0.30f, 0.74f, s));
}

inline float koi_tail_half_width(float s) {
  return 0.03f + 0.15f * f_pow_080(smoothstep(0.70f, 1.0f, s));
}

inline bool koi_in_fin(float s, float a, float root_s, float angle, float reach, float width, ShapeHit& out) {
  float root_a = koi_body_half_width(root_s) * 0.85f;
  float cos_a = f_cos(angle);
  float sin_a = f_sin(angle);
  float du = s - root_s;
  float da = a - root_a;
  float along = du * cos_a + da * sin_a - reach;
  float perp = -du * sin_a + da * cos_a;
  float r1 = along / reach;
  float r2 = perp / width;
  if (r1 * r1 + r2 * r2 > 1.0f) return false;
  out.flag = FLAG_FIN;
  return true;
}

inline bool koi_classify(float s, float a, float past, float flare, ShapeHit& out, float ahead, float lateral) {
  out.flag = 0;
  out.across = 1.0f;

  // Barbels: short whiskers angling out from each corner of the mouth
  if (ahead > 0.0f) {
    if (ahead < 0.05f && f_abs(lateral - (0.045f + ahead * 0.7f)) < 0.016f) {
      out.flag = FLAG_BARBEL;
      return true;
    }
    return false;
  }

  float w = koi_body_half_width(s);
  if (s <= BODY_END && a <= w) {
    float across = (w > 1e-5f) ? (a / w) : 1.0f;
    out.across = across;
    if (s > 0.055f && s < 0.10f && across > 0.50f && across < 0.85f) out.flag = FLAG_EYE;
    else if (s > 0.28f && s < 0.60f && across < 0.14f) out.flag = FLAG_DORSAL;
    else out.flag = FLAG_BODY;
    return true;
  }

  // Butterfly tail: fans out from the stalk with exact V-notch in the trailing edge!
  if (s > 0.68f && past <= 0.0f) {
    float tw = koi_tail_half_width(s) * flare;
    if (tw > 1e-5f && a <= tw && s <= 0.86f + 0.14f * (a / tw)) {
      out.flag = FLAG_FIN;
      return true;
    }
  }

  return koi_in_fin(s, a, 0.19f, 0.85f + 0.5f * (flare - 1.0f), 0.066f * flare, 0.04f, out) ||
         koi_in_fin(s, a, 0.47f, 0.75f, 0.036f, 0.018f, out);
}

struct Node {
  float x, y;
  float ox, oy;
};

struct Tentacle {
  uint32_t is_oral;
  float offset;
  float seg_len;
  float phase;
  int32_t count;
  Node nodes[MAX_NODES];
};

struct Jelly {
  float x, y;
  float vx, vy;
  float ang;
  float r;
  float depth;
  float phase;
  float speed_mul;
  uint8_t species; // 0..3
  uint8_t pad[3];
  Tentacle tentacles[TENTACLES_PER_JELLY];
};

struct Patch {
  float s, v, rs, rv;
};

struct Koi {
  int32_t id;
  float x, y;
  float ang;
  float turn;
  float speed;
  float length;
  float depth;
  float phase;
  float fin_phase;
  float beat;
  float gulp;
  float turn_bias;
  uint8_t variety; // 0=kohaku, 1=sanke, 2=tancho
  uint8_t num_red;
  uint8_t num_black;
  uint8_t pad;
  Patch red[4];
  Patch black[6];
  float nx[KOI_NODES];
  float ny[KOI_NODES];
};

struct PersistentState {
  uint32_t magic;
  uint32_t rng_state;
  float time;
  int32_t active_jellies;
  int32_t active_koi;
  float saved_width;
  float saved_height;
  Jelly jellies[MAX_JELLIES];
  Koi koi[MAX_KOI];
};

constexpr uint32_t STATE_MAGIC = 0x44414F36u; // "DAO6"

PersistentState state;

struct Pellet {
  float x, y;
  float age;
  bool active;
};

struct Ripple {
  float x, y;
  float r;
  float max_r;
  float alpha;
  bool active;
};

Pellet pellets[MAX_PELLETS];
int pellet_cursor = 0;
Ripple ripples[MAX_RIPPLES];
int ripple_cursor = 0;

int g_cols = 100;
int g_rows = 60;
float g_cw = 12.0f;
float g_ch = 14.0f;
float g_width = 1200.0f;
float g_height = 840.0f;
float g_accum = 0.0f;
int g_is_light = 0;

float text_x0 = 0.0f, text_y0 = 0.0f, text_x1 = 0.0f, text_y1 = 0.0f;
int text_active = 0;
float nav_x0 = 0.0f, nav_y0 = 0.0f, nav_x1 = 0.0f, nav_y1 = 0.0f;
int nav_active = 0;

float energy[MAX_CELLS];
float energy_tmp[MAX_CELLS];

uint32_t out_tile_rgb[MAX_CELLS];
uint32_t out_glyph_rgb[MAX_CELLS];
uint8_t out_tile_alpha[MAX_CELLS];
uint8_t out_glyph_alpha[MAX_CELLS];
uint8_t out_glyph[MAX_CELLS];
uint8_t out_shadow[MAX_CELLS];
uint8_t out_wake[MAX_CELLS];

// Scratch spine buffers for current koi
float cur_spine_x[KOI_NODES];
float cur_spine_y[KOI_NODES];
float cur_arc[KOI_NODES];
float cur_arc_total = 1.0f;
float cur_flare_l = 1.0f;
float cur_flare_r = 1.0f;

// Scratch sample outputs
ShapeHit cur_hit;
float cur_hit_s = 0.0f;
float cur_hit_v = 0.0f;
float cur_hit_dist = 0.0f;
float cur_hit_tx = 1.0f;
float cur_hit_ty = 0.0f;
int cur_hit_seg = 0;

float ptr_x = -999.0f;
float ptr_y = -999.0f;
float ptr_vx = 0.0f;
float ptr_vy = 0.0f;
int ptr_active = 0;

inline const Palette& pal() { return g_is_light ? PAL_LIGHT : PAL_DARK; }

inline float rand01() {
  uint32_t x = state.rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state.rng_state = x;
  return static_cast<float>(x & 0x00FFFFFFu) / 16777216.0f;
}

inline int pick_jelly_count(float w, float h) {
  float area = w * h;
  if (w <= 540.0f || area < 340000.0f) return 2;
  if (w <= 960.0f || area < 680000.0f) return 3;
  return MAX_JELLIES;
}

inline int pick_koi_count(float w, float h) {
  float area = w * h;
  if (w <= 540.0f || area < 340000.0f) return 2;
  if (w <= 960.0f || area < 680000.0f) return 4;
  return MAX_KOI;
}

inline float rect_dim_at(float px, float py, float x0, float y0, float x1, float y1) {
  float pad = g_cw * 4.0f;
  float dx = f_max(x0 - px, f_max(0.0f, px - x1));
  float dy = f_max(y0 - py, f_max(0.0f, py - y1));
  float d = f_hypot(dx, dy);
  if (d >= pad) return 1.0f;
  // Exact Dao-Ho DIM_UNDER_CONTENT easing (gentle fade under bio text)
  return 0.22f + 0.78f * smoothstep(0.0f, pad, d);
}

inline float content_dim_at(float px, float py) {
  float d = 1.0f;
  if (text_active) d = rect_dim_at(px, py, text_x0, text_y0, text_x1, text_y1);
  if (nav_active) d = f_min(d, rect_dim_at(px, py, nav_x0, nav_y0, nav_x1, nav_y1));
  return d;
}

inline void add_ripple(float x, float y, float max_r, float alpha) {
  int slot = -1;
  for (int i = 0; i < MAX_RIPPLES; ++i) {
    if (!ripples[i].active) { slot = i; break; }
  }
  if (slot < 0) {
    slot = ripple_cursor;
    ripple_cursor = (ripple_cursor + 1) % MAX_RIPPLES;
  }
  ripples[slot] = {x, y, 4.0f, max_r, alpha, true};
}

inline void clear_buffers() {
  int n = g_cols * g_rows;
  for (int i = 0; i < n; ++i) {
    energy[i] = 0.0f;
    energy_tmp[i] = 0.0f;
    out_tile_rgb[i] = 0;
    out_glyph_rgb[i] = 0;
    out_tile_alpha[i] = 0;
    out_glyph_alpha[i] = 0;
    out_glyph[i] = 0;
    out_shadow[i] = 0;
    out_wake[i] = 0;
  }
}

inline void write_cell(int cx, int cy, Rgb tile_rgb, float tile_a, uint8_t glyph,
                       Rgb glyph_rgb, float glyph_a, bool overwrite = true) {
  if (cx < 0 || cx >= g_cols || cy < 0 || cy >= g_rows) return;
  int i = cy * g_cols + cx;
  float px = (static_cast<float>(cx) + 0.5f) * g_cw;
  float py = (static_cast<float>(cy) + 0.5f) * g_ch;
  float d = content_dim_at(px, py);
  uint8_t ta = static_cast<uint8_t>(f_clamp(tile_a * d, 0.0f, 1.0f) * 255.0f);
  uint8_t ga = (glyph > 32) ? static_cast<uint8_t>(f_clamp(glyph_a * d, 0.0f, 1.0f) * 255.0f) : 0;
  if (ta == 0 && ga == 0) return;
  if (!overwrite && ta <= out_tile_alpha[i] && ga <= out_glyph_alpha[i]) return;
  out_tile_rgb[i] = pack_rgb(tile_rgb);
  out_tile_alpha[i] = ta;
  out_glyph[i] = glyph;
  out_glyph_rgb[i] = pack_rgb(glyph_rgb);
  out_glyph_alpha[i] = ga;
}

inline void deposit_energy(float x, float y, float amt) {
  int c = i_floor(x / g_cw);
  int r = i_floor(y / g_ch);
  if (c < 0 || r < 0 || c >= g_cols || r >= g_rows) return;
  int i = r * g_cols + c;
  energy[i] = f_min(1.5f, energy[i] + amt);
}

void init_koi_markings(Koi& k) {
  auto r = [&](int j) { return hash3(k.id, j, 41); };
  k.num_red = 0;
  k.num_black = 0;
  if (k.variety == 2) {
    // Tancho: iconic single crimson disc on pure white crown
    k.red[0] = {0.11f, 0.0f, 0.055f, 0.058f};
    k.num_red = 1;
    return;
  }
  if (r(0) < 0.80f) {
    k.red[k.num_red++] = {0.10f, (r(1) - 0.5f) * 0.03f, 0.055f + r(2) * 0.03f, 0.11f};
  }
  int saddles = 2 + static_cast<int>(r(3) * 2.0f);
  if (saddles > 3) saddles = 3;
  float span = 0.42f / static_cast<float>(saddles);
  for (int j = 0; j < saddles && k.num_red < 4; ++j) {
    k.red[k.num_red++] = {
        0.25f + span * (static_cast<float>(j) + 0.5f) + (r(10 + j) - 0.5f) * span * 0.3f,
        (r(20 + j) - 0.5f) * 0.05f,
        span * (0.34f + r(30 + j) * 0.12f),
        0.09f + r(40 + j) * 0.05f,
    };
  }
  if (k.variety == 1) {
    int spots = 4 + static_cast<int>(r(50) * 3.0f);
    if (spots > 6) spots = 6;
    for (int j = 0; j < spots; ++j) {
      k.black[k.num_black++] = {
          0.24f + r(60 + j) * 0.44f,
          (r(70 + j) - 0.5f) * 0.16f,
          0.022f,
          0.026f,
      };
    }
  }
}

void spawn_ecosystem() {
  float mobile_scale = f_clamp(g_width / 860.0f, 0.62f, 1.0f);
  state.active_jellies = pick_jelly_count(g_width, g_height);
  state.active_koi = pick_koi_count(g_width, g_height);
  state.saved_width = g_width;
  state.saved_height = g_height;

  // 1. 4 giant jellyfish across the upper & side canopy
  struct JSeed { float fx, fy, r, depth, speed_mul; uint8_t species; };
  constexpr JSeed jseeds[MAX_JELLIES] = {
      {0.80f, 0.26f, 94.0f, 0.88f, 0.90f, 0}, // Cyan-teal giant
      {0.18f, 0.26f, 86.0f, 0.78f, 0.96f, 2}, // Coral sea nettle
      {0.50f, 0.22f, 80.0f, 0.68f, 0.92f, 1}, // Amethyst moon jelly
      {0.84f, 0.72f, 82.0f, 0.82f, 0.95f, 3}, // Seafoam emerald
  };

  for (int idx = 0; idx < MAX_JELLIES; ++idx) {
    const JSeed& s = jseeds[idx];
    Jelly& j = state.jellies[idx];
    j.x = g_width * s.fx;
    j.y = g_height * s.fy;
    j.r = s.r * mobile_scale;
    j.depth = s.depth;
    j.speed_mul = s.speed_mul;
    j.species = s.species;
    j.phase = wrap_tau(static_cast<float>(idx) * 1.55f);
    j.ang = wrap_pi(-HALF_PI + (static_cast<float>(idx) - 1.5f) * 0.65f + (rand01() - 0.5f) * 0.3f);
    j.vx = f_cos(j.ang) * 0.22f;
    j.vy = f_sin(j.ang) * 0.22f;

    for (int t = 0; t < TENTACLES_PER_JELLY; ++t) {
      Tentacle& tn = j.tentacles[t];
      bool is_oral = (t >= 3 && t <= 5);
      tn.is_oral = is_oral ? 1u : 0u;
      float rel = is_oral
                      ? (static_cast<float>(t - 4) * 0.33f)
                      : ((static_cast<float>(t < 3 ? t : t - 3) / 5.0f - 0.5f) * 1.56f);
      tn.offset = rel * j.r;
      tn.seg_len = is_oral ? (j.r * 0.205f) : (j.r * 0.185f);
      tn.count = is_oral ? 15 : ((t % 2 == 0) ? 17 : 13);
      tn.phase = static_cast<float>(idx) * 1.25f + static_cast<float>(t) * 0.85f;
      for (int k = 0; k < tn.count; ++k) {
        float nx = j.x - f_cos(j.ang) * (static_cast<float>(k) * tn.seg_len);
        float ny = j.y - f_sin(j.ang) * (static_cast<float>(k) * tn.seg_len);
        tn.nodes[k] = {nx, ny, nx, ny};
      }
    }
  }

  // 2. 5 Dao-Ho authentic Kohaku / Sanke / Tancho koi (length ~245-315 px)
  struct KSeed { int id; float fx, fy, ang, len, depth, spd; uint8_t variety; };
  constexpr KSeed kseeds[MAX_KOI] = {
      {11, 0.34f, 0.48f,  0.22f, 310.0f, 0.96f, 1.12f, 0}, // Surface hero Kohaku
      {23, 0.68f, 0.50f,  2.72f, 290.0f, 0.82f, 1.00f, 1}, // Sanke (red saddles + black sumi spots)
      {37, 0.52f, 0.64f, -0.44f, 268.0f, 0.66f, 0.94f, 2}, // Tancho (pure white + single red crown disc)
      {51, 0.24f, 0.58f, -0.25f, 258.0f, 0.54f, 0.90f, 0}, // Deeper Kohaku
      {67, 0.72f, 0.38f, -2.35f, 275.0f, 0.74f, 1.04f, 1}, // Mid-depth Sanke
  };

  for (int idx = 0; idx < MAX_KOI; ++idx) {
    const KSeed& s = kseeds[idx];
    Koi& k = state.koi[idx];
    k.id = s.id;
    k.x = g_width * s.fx;
    k.y = g_height * s.fy;
    k.ang = s.ang;
    k.turn = 0.0f;
    k.length = s.len * mobile_scale;
    k.depth = s.depth;
    k.speed = s.spd;
    k.phase = wrap_tau(static_cast<float>(idx) * 1.75f);
    k.fin_phase = wrap_tau(static_cast<float>(idx) * 2.1f);
    k.beat = 0.55f;
    k.gulp = 0.0f;
    k.turn_bias = ((idx % 2 == 0) ? 1.0f : -1.0f) * (0.0038f + 0.0012f * static_cast<float>(idx % 3));
    k.variety = s.variety;
    init_koi_markings(k);

    float seg = k.length / static_cast<float>(KOI_NODES - 1);
    for (int i = 0; i < KOI_NODES; ++i) {
      k.nx[i] = k.x - f_cos(k.ang) * (static_cast<float>(i) * seg);
      k.ny[i] = k.y - f_sin(k.ang) * (static_cast<float>(i) * seg);
    }
  }
}

void step_and_draw_jellies(int j_count) {
  const Palette& P = pal();

  for (int idx = 0; idx < j_count; ++idx) {
    Jelly& j = state.jellies[idx];
    j.phase = wrap_tau(j.phase + 0.0155f * j.speed_mul);
    float thrust = (j.phase < 1.50f) ? f_sin((j.phase / 1.50f) * PI) : 0.0f;

    float steer_x = f_cos(state.time * 0.65f + j.r * 0.19f) * 0.0036f;
    float steer_y = f_sin(state.time * 0.52f + j.r * 0.19f) * 0.0036f;

    for (int other = 0; other < j_count; ++other) {
      if (other == idx) continue;
      float dx = j.x - state.jellies[other].x;
      float dy = j.y - state.jellies[other].y;
      float d = f_hypot(dx, dy);
      float sep_dist = (j.r + state.jellies[other].r) * 1.65f;
      if (d < sep_dist && d > 0.01f) {
        float s = (sep_dist - d) / sep_dist;
        steer_x += (dx / d) * s * 0.013f;
        steer_y += (dy / d) * s * 0.013f;
      }
    }

    float m = j.r * 0.85f;
    if (j.x < m) steer_x += (m - j.x) * 0.00085f;
    if (j.x > g_width - m) steer_x -= (j.x - (g_width - m)) * 0.00085f;
    if (j.y < m) steer_y += (m - j.y) * 0.00085f;
    if (j.y > g_height - m) steer_y -= (j.y - (g_height - m)) * 0.00085f;

    for (int r = 0; r < MAX_RIPPLES; ++r) {
      if (!ripples[r].active) continue;
      float dx = j.x - ripples[r].x;
      float dy = j.y - ripples[r].y;
      float d = f_hypot(dx, dy);
      if (f_abs(d - ripples[r].r) < j.r * 1.25f && d > 0.01f) {
        steer_x += (dx / d) * ripples[r].alpha * 0.055f;
        steer_y += (dy / d) * ripples[r].alpha * 0.055f;
      }
    }

    float desired = f_atan2(j.vy + steer_y * 10.5f, j.vx + steer_x * 10.5f);
    float diff = wrap_pi(desired - j.ang);
    j.ang = wrap_pi(j.ang + diff * 0.022f);

    float drive = (0.0068f + thrust * 0.0285f) * j.speed_mul;
    j.vx = (j.vx + f_cos(j.ang) * drive + steer_x) * 0.976f;
    j.vy = (j.vy + f_sin(j.ang) * drive + steer_y) * 0.976f;
    j.x += j.vx;
    j.y += j.vy;

    float perp_x = -f_sin(j.ang);
    float perp_y = f_cos(j.ang);
    float back_x = -f_cos(j.ang);
    float back_y = -f_sin(j.ang);
    float pulse = f_sin(j.phase);
    float rx = j.r * (1.0f - pulse * 0.15f);
    float ry = j.r * (0.76f + pulse * 0.15f);
    float ptr_rad = j.r * 2.25f;

    const Rgb* ramp = P.jelly_ramps[j.species & 3];
    Rgb core_rgb = P.jelly_core[j.species & 3];
    float dim = P.alpha * (P.depth_floor + (1.0f - P.depth_floor) * j.depth);

    // 1. Pale translucent veil tentacles & oral arms (Verlet physics)
    for (int t = 0; t < TENTACLES_PER_JELLY; ++t) {
      Tentacle& tn = j.tentacles[t];
      float width_scale = 1.0f - pulse * 0.14f;
      tn.nodes[0].x = j.x + perp_x * (tn.offset * width_scale) + back_x * (j.r * 0.12f);
      tn.nodes[0].y = j.y + perp_y * (tn.offset * width_scale) + back_y * (j.r * 0.12f);

      for (int k = 1; k < tn.count; ++k) {
        Node& nd = tn.nodes[k];
        float nvx = (nd.x - nd.ox) * 0.925f;
        float nvy = (nd.y - nd.oy) * 0.925f;
        nd.ox = nd.x;
        nd.oy = nd.y;
        float sway = f_sin(state.time * 3.0f + tn.phase + static_cast<float>(k) * 0.38f) *
                     (tn.is_oral ? 0.14f : 0.095f);
        nd.x += nvx + perp_x * sway + back_x * 0.03f;
        nd.y += nvy + perp_y * sway + back_y * 0.03f;

        if (ptr_active) {
          float dx = nd.x - ptr_x;
          float dy = nd.y - ptr_y;
          float d = f_hypot(dx, dy);
          if (d < ptr_rad && d > 0.01f) {
            float s = (ptr_rad - d) / ptr_rad;
            nd.x += ptr_vx * s * 0.14f + (dx / d) * s * 0.38f;
            nd.y += ptr_vy * s * 0.14f + (dy / d) * s * 0.38f;
          }
        }
      }

      for (int iter = 0; iter < 3; ++iter) {
        for (int k = 1; k < tn.count; ++k) {
          Node& a = tn.nodes[k - 1];
          Node& b = tn.nodes[k];
          float dx = b.x - a.x;
          float dy = b.y - a.y;
          float d = f_hypot(dx, dy);
          if (d < 0.001f) d = 0.001f;
          float err = (d - tn.seg_len) / d;
          if (k == 1) {
            b.x -= dx * err;
            b.y -= dy * err;
          } else {
            a.x += dx * err * 0.45f;
            a.y += dy * err * 0.45f;
            b.x -= dx * err * 0.55f;
            b.y -= dy * err * 0.55f;
          }
        }
      }

      float inv_cnt = 1.0f / static_cast<float>(tn.count);
      float frill_amp = j.r * 0.085f;
      for (int k = 1; k < tn.count; ++k) {
        float frac = 1.0f - static_cast<float>(k) * inv_cnt;
        const Node& a = tn.nodes[k - 1];
        const Node& b = tn.nodes[k];
        float bx = b.x, by = b.y;
        if (tn.is_oral) {
          float frill = f_sin(static_cast<float>(k) * 1.05f + state.time * 4.2f + tn.phase) * frill_amp * frac;
          bx += perp_x * frill;
          by += perp_y * frill;
        }
        float dist = f_hypot(bx - a.x, by - a.y);
        int steps = i_ceil(dist / (g_cw * 0.55f));
        if (steps < 1) steps = 1;
        if (steps > 256) steps = 256; // never let a stretched segment stall the frame
        Rgb veil = mix_rgb(P.bg, ramp[3], tn.is_oral ? 0.58f : 0.46f);
        Rgb ink = shade_rgb(veil, P.ink + 0.15f);
        uint8_t gl = tn.is_oral ? ((frac > 0.55f) ? '~' : ':') : ((k % 2 == 0) ? '.' : ':');
        for (int s = 0; s <= steps; ++s) {
          float u = static_cast<float>(s) / static_cast<float>(steps);
          float sx = a.x + (bx - a.x) * u;
          float sy = a.y + (by - a.y) * u;
          int cx = i_floor(sx / g_cw);
          int cy = i_floor(sy / g_ch);
          float ta = dim * (tn.is_oral ? 0.42f : 0.28f) * frac;
          float ga = dim * (tn.is_oral ? 0.56f : 0.42f) * frac;
          write_cell(cx, cy, veil, ta, gl, ink, ga, false);
        }
        if (k == tn.count - 1 && (idx % 2 == 0)) deposit_energy(bx, by, 0.025f);
      }
    }

    // 2. Opaque shaded jellyfish bell dome + translucent scalloped outer fringe
    float cos_a = f_cos(j.ang + HALF_PI);
    float sin_a = f_sin(j.ang + HALF_PI);
    float reach = f_max(rx, ry) * 1.38f + g_ch * 1.5f;
    int c0 = f_max(0, i_floor((j.x - reach) / g_cw));
    int c1 = f_min(g_cols - 1, i_ceil((j.x + reach) / g_cw));
    int r0 = f_max(0, i_floor((j.y - reach) / g_ch));
    int r1 = f_min(g_rows - 1, i_ceil((j.y + reach) / g_ch));
    int sh_dc = static_cast<int>(j.r * 0.14f * j.depth / g_cw) + 1;
    int sh_dr = static_cast<int>(j.r * 0.10f * j.depth / g_ch) + 1;
    float skirt_base = j.r * 0.12f;
    float skirt_amp = j.r * 0.075f;
    float rim_band = j.r * 0.12f;

    for (int cy = r0; cy <= r1; ++cy) {
      float py = (static_cast<float>(cy) + 0.5f) * g_ch;
      for (int cx = c0; cx <= c1; ++cx) {
        float px = (static_cast<float>(cx) + 0.5f) * g_cw;
        float wx = px - j.x;
        float wy = py - j.y;
        float lx = wx * cos_a + wy * sin_a;
        float ly = -wx * sin_a + wy * cos_a;

        float skirt_y = skirt_base + f_sin((lx / rx) * PI * 4.0f) * skirt_amp - pulse * (j.r * 0.06f);
        if (ly > skirt_y) continue;
        float nx = lx / rx;
        float ny = (ly - skirt_y) / (ry * 1.28f);
        float r2 = nx * nx + ny * ny;
        if (r2 > 1.0f) continue;

        bool is_outer = (r2 > 0.74f) || (ly > skirt_y - rim_band);
        uint32_t key = static_cast<uint32_t>(hash3(static_cast<int>(nx * 30.0f), static_cast<int>(ny * 30.0f), idx + 19) * 65535.0f);

        // Soft drop shadow cast by opaque inner dome
        if (!is_outer) {
          int sc = cx + sh_dc;
          int sr = cy + sh_dr;
          if (sc >= 0 && sr >= 0 && sc < g_cols && sr < g_rows) out_shadow[sr * g_cols + sc] = 1;
        }

        // Top-left 3D dome shading + radial ribs + shimmering wet sheen
        float theta = f_atan2(ny, nx);
        float c4 = f_abs(f_cos(theta * 4.0f));
        float rib = (c4 * c4 * c4 * c4 * c4 * c4) * 0.22f;
        float dome_z = f_sqrt(f_max(0.0f, 1.0f - r2));
        float light_dir = f_clamp(-0.55f * nx - 0.65f * ny, -0.5f, 0.5f);
        float lum = f_clamp(0.24f + 0.56f * dome_z + 0.22f * light_dir + rib, 0.0f, 1.0f);

        // Glowing 2-lobed moon-jelly horseshoe core
        float gx = f_abs(nx) - 0.24f;
        float gy = ny + 0.42f;
        float gd = f_hypot(gx, gy);
        if (gd > 0.065f && gd < 0.23f) {
          Rgb tile = mix_rgb(core_rgb, ramp[5], 0.15f);
          Rgb ink = shade_rgb(tile, P.ink);
          uint8_t gl = (gd > 0.11f && gd < 0.18f) ? '@' : '#';
          write_cell(cx, cy, tile, dim * 0.98f, gl, ink, dim, true);
          continue;
        }

        if (is_outer) {
          // Pale translucent outer rim & scalloped fringe
          Rgb tile = mix_rgb(P.bg, ramp[3], 0.55f);
          Rgb ink = shade_rgb(tile, P.ink + 0.12f);
          uint8_t gl = (ly > skirt_y - rim_band) ? '~' : ((key & 1u) ? '.' : ':');
          write_cell(cx, cy, tile, dim * 0.42f, gl, ink, dim * 0.55f, true);
        } else {
          float sheen = f_max(0.0f, f_sin(ny * 8.0f - state.time * 2.2f + static_cast<float>(idx)) - 0.72f) / 0.28f;
          Rgb tile = ramp6(ramp, 0.16f + 0.72f * lum + sheen * 0.22f);
          int gi = static_cast<int>((1.0f - lum) * 4.0f + 0.5f) + static_cast<int>((key >> 2) % 3u);
          if (gi < 0) gi = 0;
          if (gi > 6) gi = 6;
          uint8_t gl = static_cast<uint8_t>(TILE_GLYPHS[gi]);
          Rgb ink = shade_rgb(tile, P.ink);
          write_cell(cx, cy, tile, dim, gl, ink, dim, true);
        }
      }
    }
  }
}

bool sample_koi(float px, float py, float L, int from, int to) {
  float best = 1e18f;
  float best_s = 0.0f;
  float best_sign = 1.0f;
  float past = 0.0f;
  float ahead = 0.0f;
  float lateral = 0.0f;

  for (int n = from; n <= to; ++n) {
    float ax = cur_spine_x[n];
    float ay = cur_spine_y[n];
    float sx = cur_spine_x[n + 1] - ax;
    float sy = cur_spine_y[n + 1] - ay;
    float len2 = sx * sx + sy * sy;
    if (len2 < 1e-6f) len2 = 1.0f;
    float raw = ((px - ax) * sx + (py - ay) * sy) / len2;
    float u = f_clamp(raw, 0.0f, 1.0f);
    float dx = px - ax - sx * u;
    float dy = py - ay - sy * u;
    float d2 = dx * dx + dy * dy;
    if (d2 < best) {
      float len = f_sqrt(len2);
      best = d2;
      best_s = (cur_arc[n] + u * len) / cur_arc_total;
      float cross = sx * (py - ay) - sy * (px - ax);
      best_sign = (cross >= 0.0f) ? 1.0f : -1.0f;
      past = (n == KOI_NODES - 2 && raw > 1.0f) ? ((raw - 1.0f) * len) : 0.0f;
      ahead = (n == 0 && raw < 0.0f) ? (-raw * len) : 0.0f;
      lateral = f_abs(cross) / len;
      cur_hit_tx = sx / len;
      cur_hit_ty = sy / len;
      cur_hit_seg = n;
    }
  }

  float a = f_sqrt(best);
  cur_hit_dist = a;
  cur_hit_s = best_s;
  cur_hit_v = a * best_sign;
  return koi_classify(best_s, a / L, past / L, (best_sign > 0.0f) ? cur_flare_l : cur_flare_r,
                      cur_hit, ahead / L, lateral / L);
}

inline int koi_skin_type(const Koi& k, float s, float v, float across) {
  // 0 = white, 1 = red (hi), 2 = black (sumi)
  if (across > 0.90f || s > 0.74f) return 0;
  float vn = v / k.length;
  float wobble = (value_noise(s * 14.0f + static_cast<float>(k.id), vn * 30.0f, k.id + 3) - 0.5f) * 0.6f;
  for (int i = 0; i < k.num_black; ++i) {
    const Patch& m = k.black[i];
    float ds = (s - m.s) / m.rs;
    float dv = (vn - m.v) / m.rv;
    if (ds * ds + dv * dv < 1.0f + wobble) return 2;
  }
  for (int i = 0; i < k.num_red; ++i) {
    const Patch& m = k.red[i];
    float ds = (s - m.s) / m.rs;
    float dv = (vn - m.v) / m.rv;
    if (ds * ds + dv * dv < 1.0f + wobble) return 1;
  }
  return 0;
}

void step_and_draw_koi(int k_count, int j_count) {
  const Palette& P = pal();

  int order[MAX_KOI];
  for (int i = 0; i < k_count; ++i) order[i] = i;
  for (int i = 0; i < k_count; ++i) {
    for (int j = i + 1; j < k_count; ++j) {
      if (state.koi[order[j]].depth < state.koi[order[i]].depth) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  for (int oi = 0; oi < k_count; ++oi) {
    int idx = order[oi];
    Koi& k = state.koi[idx];

    float turn = f_sin(state.time * 0.85f + static_cast<float>(idx) * 2.1f) * 0.0085f + k.turn_bias * 0.30f;
    float steer_x = 0.0f;
    float steer_y = 0.0f;
    bool chasing_pellet = false;

    // 1. Seek nearest active pond pellet!
    int best_pellet = -1;
    float best_pd = 420.0f;
    for (int p = 0; p < MAX_PELLETS; ++p) {
      if (!pellets[p].active) continue;
      float d = f_hypot(pellets[p].x - k.x, pellets[p].y - k.y);
      if (d < best_pd) {
        best_pd = d;
        best_pellet = p;
      }
    }
    if (best_pellet >= 0) {
      chasing_pellet = true;
      float dx = pellets[best_pellet].x - k.x;
      float dy = pellets[best_pellet].y - k.y;
      if (best_pd < 28.0f) {
        // Bite!
        pellets[best_pellet].active = false;
        k.gulp = 1.0f;
        add_ripple(k.x, k.y, 110.0f, 0.72f);
        deposit_energy(k.x, k.y, 0.45f);
      } else {
        steer_x += (dx / best_pd) * 0.16f;
        steer_y += (dy / best_pd) * 0.16f;
      }
    }

    // 2. Soft edge return
    float m = 110.0f;
    if (k.x < m) steer_x += (m - k.x) * 0.0018f;
    if (k.x > g_width - m) steer_x -= (k.x - (g_width - m)) * 0.0018f;
    if (k.y < m) steer_y += (m - k.y) * 0.0018f;
    if (k.y > g_height - m) steer_y -= (k.y - (g_height - m)) * 0.0018f;

    // 3. Peer separation
    for (int o = 0; o < k_count; ++o) {
      if (o == idx) continue;
      float dx = k.x - state.koi[o].x;
      float dy = k.y - state.koi[o].y;
      float d = f_hypot(dx, dy);
      if (d < 170.0f && d > 0.01f) {
        float s = (170.0f - d) / 170.0f;
        steer_x += (dx / d) * s * 0.085f;
        steer_y += (dy / d) * s * 0.085f;
      }
    }

    // 4. Gracefully weave around jellyfish bells
    for (int ji = 0; ji < j_count; ++ji) {
      float dx = k.x - state.jellies[ji].x;
      float dy = k.y - state.jellies[ji].y;
      float d = f_hypot(dx, dy);
      float avoid_r = state.jellies[ji].r * 1.45f;
      if (d < avoid_r && d > 0.01f) {
        float s = (avoid_r - d) / avoid_r;
        steer_x += (dx / d) * s * 0.065f;
        steer_y += (dy / d) * s * 0.065f;
      }
    }

    // 5. Gently part away from moving cursor
    if (ptr_active && !chasing_pellet) {
      float dx = k.x - ptr_x;
      float dy = k.y - ptr_y;
      float d = f_hypot(dx, dy);
      if (d < 135.0f && d > 0.01f) {
        float s = (135.0f - d) / 135.0f;
        steer_x += (dx / d) * s * 0.14f;
        steer_y += (dy / d) * s * 0.14f;
      }
    }

    float steer_mag = f_hypot(steer_x, steer_y);
    if (steer_mag > 0.001f) {
      float target_ang = f_atan2(steer_y, steer_x);
      float diff = wrap_pi(target_ang - k.ang);
      turn += f_clamp(diff, -0.058f, 0.058f) * f_min(1.0f, steer_mag * 5.0f);
    }

    k.turn = k.turn * 0.86f + turn * 0.14f;
    k.ang = wrap_pi(k.ang + turn);
    float target_beat = chasing_pellet ? 0.92f : (0.35f + 0.45f * f_max(0.0f, f_sin(state.time * 0.7f + static_cast<float>(idx) * 1.9f)));
    k.beat = k.beat * 0.96f + target_beat * 0.04f;
    k.gulp = f_max(0.0f, k.gulp - 0.04f);

    float spd = k.speed * (0.78f + 0.55f * k.beat);
    k.phase = wrap_tau(k.phase + (0.045f + 0.065f * k.beat + f_abs(turn) * 0.55f));
    k.fin_phase = wrap_tau(k.fin_phase + 0.045f);
    k.x += f_cos(k.ang) * spd;
    k.y += f_sin(k.ang) * spd;

    // Follow-the-leader 20-node spine
    float seg = k.length / static_cast<float>(KOI_NODES - 1);
    k.nx[0] = k.x;
    k.ny[0] = k.y;
    for (int i = 1; i < KOI_NODES; ++i) {
      float dx = k.nx[i] - k.nx[i - 1];
      float dy = k.ny[i] - k.ny[i - 1];
      float d = f_hypot(dx, dy);
      if (d < 0.001f) { dx = -f_cos(k.ang); dy = -f_sin(k.ang); d = 1.0f; }
      k.nx[i] = k.nx[i - 1] + (dx / d) * seg;
      k.ny[i] = k.ny[i - 1] + (dy / d) * seg;
    }

    // Deposit tail wake into diffused water energy field
    for (int n = KOI_NODES - 4; n < KOI_NODES; ++n) {
      deposit_energy(k.nx[n], k.ny[n], 0.032f + spd * 0.015f);
    }

    float L = k.length;
    float swim = 0.2f + 1.1f * k.beat;
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (int n = 0; n < KOI_NODES; ++n) {
      float s = static_cast<float>(n) / static_cast<float>(KOI_NODES - 1);
      int a = (n > 0) ? (n - 1) : 0;
      int b = (n < KOI_NODES - 1) ? (n + 1) : (KOI_NODES - 1);
      float tx = k.nx[a] - k.nx[b];
      float ty = k.ny[a] - k.ny[b];
      float tl = f_hypot(tx, ty);
      if (tl < 1e-4f) { tx = 1.0f; ty = 0.0f; tl = 1.0f; }
      tx /= tl;
      ty /= tl;
      float s2 = s * s;
      float amp = L * 0.04f * (0.05f + 0.95f * s2 * f_sqrt(f_sqrt(s))) * swim;
      float off = amp * f_sin(k.phase - s * 4.6f);
      cur_spine_x[n] = k.nx[n] - ty * off;
      cur_spine_y[n] = k.ny[n] + tx * off;
      if (cur_spine_x[n] < min_x) min_x = cur_spine_x[n];
      if (cur_spine_x[n] > max_x) max_x = cur_spine_x[n];
      if (cur_spine_y[n] < min_y) min_y = cur_spine_y[n];
      if (cur_spine_y[n] > max_y) max_y = cur_spine_y[n];
    }

    cur_arc[0] = 0.0f;
    for (int n = 1; n < KOI_NODES; ++n) {
      cur_arc[n] = cur_arc[n - 1] + f_hypot(cur_spine_x[n] - cur_spine_x[n - 1],
                                            cur_spine_y[n] - cur_spine_y[n - 1]);
    }
    cur_arc_total = cur_arc[KOI_NODES - 1] > 1.0f ? cur_arc[KOI_NODES - 1] : 1.0f;

    float spread = 1.0f - 0.25f * k.beat;
    float scull = 0.18f * (1.0f - k.beat) * f_sin(k.fin_phase);
    float bank = f_clamp(k.turn * 12.0f, -0.35f, 0.35f);
    cur_flare_l = spread + scull - bank;
    cur_flare_r = spread - scull + bank;

    float reach = L * 0.26f;
    int c0 = f_max(0, i_floor((min_x - reach) / g_cw));
    int c1 = f_min(g_cols - 1, i_floor((max_x + reach) / g_cw));
    int r0 = f_max(0, i_floor((min_y - reach) / g_ch));
    int r1 = f_min(g_rows - 1, i_floor((max_y + reach) / g_ch));
    if (c0 > c1 || r0 > r1) continue;

    float diag = f_hypot(g_cw, g_ch);
    int shadow_dc = static_cast<int>(L * 0.07f * k.depth / g_cw + 0.5f) + 1;
    int shadow_dr = static_cast<int>(L * 0.05f * k.depth / g_ch + 0.5f) + 1;
    float dim = P.alpha * (P.depth_floor + (1.0f - P.depth_floor) * k.depth);

    for (int r = r0; r <= r1; ++r) {
      for (int c = c0; c <= c1; ++c) {
        float x = static_cast<float>(c) * g_cw;
        float y = static_cast<float>(r) * g_ch;
        bool center_hit = sample_koi(x + g_cw * 0.5f, y + g_ch * 0.5f, L, 0, KOI_NODES - 2);
        if (!center_hit && cur_hit_dist > L * ((cur_hit_s > 0.4f && cur_hit_s < 0.62f) ? 0.15f : 0.25f) + diag) {
          int skip = static_cast<int>((cur_hit_dist - L * 0.25f - diag) / g_cw);
          if (skip > 0) c += skip;
          continue;
        }

        int from = (cur_hit_seg > 2) ? (cur_hit_seg - 2) : 0;
        int to = (cur_hit_seg + 2 < KOI_NODES - 2) ? (cur_hit_seg + 2) : (KOI_NODES - 2);
        int flag = cur_hit.flag;
        float across = cur_hit.across;
        float s = cur_hit_s;
        float v = cur_hit_v;
        float tx = cur_hit_tx;
        float ty = cur_hit_ty;

        int hits = center_hit ? 1 : 0;
        float gx = 0.0f, gy = 0.0f;
        bool deep_inside = center_hit && flag != FLAG_FIN && flag != FLAG_BARBEL &&
                           s > 0.04f && s < 0.72f &&
                           koi_body_half_width(s) * L - cur_hit_dist > diag * 0.35f;
        if (deep_inside) {
          hits = 5;
        } else {
          for (int q = 0; q < 8; q += 2) {
            if (!sample_koi(x + SUBSAMPLES[q] * g_cw, y + SUBSAMPLES[q + 1] * g_ch, L, from, to)) continue;
            gx += (SUBSAMPLES[q] - 0.5f) * g_cw;
            gy += (SUBSAMPLES[q + 1] - 0.5f) * g_ch;
            if (hits == 0) {
              flag = cur_hit.flag;
              across = cur_hit.across;
              s = cur_hit_s;
              v = cur_hit_v;
              tx = cur_hit_tx;
              ty = cur_hit_ty;
            }
            ++hits;
          }
        }
        if (hits == 0) continue;

        float coverage = static_cast<float>(hits) * 0.2f;
        float side = (v >= 0.0f) ? 1.0f : -1.0f;
        uint32_t key = static_cast<uint32_t>(
            hash3(static_cast<int>(s * 90.0f + 0.5f), static_cast<int>(v / (g_cw * 0.8f)), k.id) * 4294967295.0f);
        bool solid = center_hit && coverage >= 0.6f;

        uint8_t glyph = ' ';
        Rgb rgb = P.fin;
        float alpha = dim;
        Rgb tile = P.bg;
        float tile_alpha = 0.0f;

        if (flag == FLAG_BARBEL) {
          glyph = line_glyph(-tx - side * ty * 0.7f, -ty + side * tx * 0.7f);
          rgb = shade_rgb(mix_rgb(P.bg, P.fin, 0.9f), P.ink + 0.3f);
        } else if (flag == FLAG_EYE && solid) {
          tile = mix_rgb(P.bg, P.eye, 0.85f);
          tile_alpha = dim;
          glyph = 'o';
          rgb = mix_rgb(P.bg, P.white, 0.9f);
        } else if (flag == FLAG_FIN) {
          // Pale translucent veil of tiles flecked with subtle dots
          tile = mix_rgb(P.bg, P.fin, 0.55f);
          tile_alpha = dim * 0.45f * coverage;
          if ((key & 1u) == 0u) {
            glyph = '.';
            alpha *= 0.6f * coverage;
          }
        } else {
          int skin_t = koi_skin_type(k, s, v, across);
          float facing = side * (0.6f * ty - 0.8f * tx) * across;
          float lum = f_clamp(0.22f + 0.50f * f_sqrt(f_max(0.0f, 1.0f - across * across)) + 0.18f * facing, 0.0f, 1.0f);

          if (skin_t == 1) {
            float lean = (hash3(k.id, 5, 17) - 0.5f) * 0.30f;
            float grain = (value_noise(s * 9.0f + static_cast<float>(k.id), (v / L) * 18.0f, k.id + 21) - 0.5f) * 0.25f;
            float sheen = f_max(0.0f, f_sin(s * 11.0f - state.time * 1.8f + static_cast<float>(k.id)) - 0.70f) / 0.30f;
            tile = ramp6(P.hi, 0.15f + 0.80f * lum + lean + grain + sheen * 0.35f);
          } else {
            Rgb skin_rgb = (skin_t == 2) ? P.black : P.white;
            tile = mix_rgb(P.bg, skin_rgb, P.tile0 + (P.tile1 - P.tile0) * lum);
          }

          if (hits == 5) {
            int sc = c + shadow_dc;
            int sr = r + shadow_dr;
            if (sc >= 0 && sr >= 0 && sc < g_cols && sr < g_rows) out_shadow[sr * g_cols + sc] = 1;
            tile_alpha = dim;
            if (s < 0.035f && across < 0.6f && k.gulp > 0.5f) {
              glyph = 'o';
              rgb = P.eye;
            } else {
              int jitter = static_cast<int>((key >> 4) % 3u);
              int gi = static_cast<int>((1.0f - lum) * 4.0f + 0.5f) + jitter;
              if (gi < 0) gi = 0;
              if (gi > 6) gi = 6;
              glyph = static_cast<uint8_t>(TILE_GLYPHS[gi]);
              rgb = shade_rgb(tile, P.ink);
              if (skin_t == 0) alpha *= 0.6f;
              // Subtle staggered scale glints
              int tick = static_cast<int>(state.time * 1.4f + static_cast<float>(key % 997u) / 97.0f);
              if (hash3(static_cast<int>(key & 0xFFFFu), tick, 7) < 0.03f) {
                tile = mix_rgb(tile, P.bg, 0.45f);
              }
            }
          } else {
            // Soft outer anti-aliased edge tile!
            tile_alpha = dim * coverage * 0.55f;
            if (f_hypot(gx, gy) > g_cw * 0.2f && (key % 3u) == 0u) {
              glyph = '.';
              rgb = shade_rgb(tile, P.ink);
            }
          }
        }

        if (glyph == ' ' && tile_alpha <= 0.0f) continue;
        write_cell(c, r, tile, tile_alpha, glyph, rgb, alpha, true);
      }
    }
  }
}

void diffuse_water() {
  if (ptr_active) deposit_energy(ptr_x, ptr_y, 0.10f);
  int cols = g_cols;
  int rows = g_rows;
  for (int r = 0; r < rows; ++r) {
    int r_up = (r > 0) ? (r - 1) : 0;
    int r_dn = (r + 1 < rows) ? (r + 1) : (rows - 1);
    int off = r * cols;
    for (int c = 0; c < cols; ++c) {
      int c_l = (c > 0) ? (c - 1) : 0;
      int c_r = (c + 1 < cols) ? (c + 1) : (cols - 1);
      float avg = energy[off + c] * 0.52f +
                  (energy[off + c_l] + energy[off + c_r] + energy[r_up * cols + c] + energy[r_dn * cols + c]) * 0.12f;
      avg *= 0.968f;
      energy_tmp[off + c] = (avg < 0.015f) ? 0.0f : avg;
    }
  }
  int n = cols * rows;
  for (int i = 0; i < n; ++i) {
    energy[i] = energy_tmp[i];
    float e = energy[i];
    out_wake[i] = (e > 0.03f) ? static_cast<uint8_t>(f_min(255.0f, e * 180.0f)) : 0;
  }
}

void step_pellets_and_ripples() {
  const Palette& P = pal();

  for (int i = 0; i < MAX_PELLETS; ++i) {
    if (!pellets[i].active) continue;
    pellets[i].age += 1.0f;
    if (pellets[i].age > 420.0f) {
      pellets[i].active = false;
      continue;
    }
    float fade = (pellets[i].age > 300.0f) ? (1.0f - (pellets[i].age - 300.0f) / 120.0f) : 1.0f;
    int cx = i_floor(pellets[i].x / g_cw);
    int cy = i_floor(pellets[i].y / g_ch);
    write_cell(cx, cy, P.pellet, 0.35f * fade, 'o', P.pellet, 0.92f * fade, true);
  }

  for (int i = 0; i < MAX_RIPPLES; ++i) {
    Ripple& rp = ripples[i];
    if (!rp.active) continue;
    rp.r += 1.55f;
    rp.alpha *= 0.962f;
    if (rp.r >= rp.max_r || rp.alpha < 0.03f) {
      rp.active = false;
      continue;
    }
    int steps = i_ceil((TAU * rp.r) / (g_cw * 0.95f));
    if (steps < 16) steps = 16;
    float inv = TAU / static_cast<float>(steps);
    for (int s = 0; s < steps; ++s) {
      float a = static_cast<float>(s) * inv;
      float rx = rp.x + f_cos(a) * rp.r;
      float ry = rp.y + f_sin(a) * rp.r;
      int cx = i_floor(rx / g_cw);
      int cy = i_floor(ry / g_ch);
      if (cx < 0 || cy < 0 || cx >= g_cols || cy >= g_rows) continue;
      int idx = cy * g_cols + cx;
      if (out_tile_alpha[idx] > 40) continue; // ripples stay behind solid bodies
      uint8_t gl = line_glyph(-f_sin(a), f_cos(a));
      write_cell(cx, cy, P.bg, 0.0f, gl, P.water, rp.alpha * 0.42f, false);
    }
  }
}

void step_fixed() {
  state.time = wrap_tau(state.time + 0.0166f);

  int n = g_cols * g_rows;
  for (int i = 0; i < n; ++i) {
    out_tile_alpha[i] = 0;
    out_glyph_alpha[i] = 0;
    out_glyph[i] = 0;
    out_shadow[i] = 0;
  }

  int j_count = state.active_jellies;
  if (j_count < 1) j_count = 1;
  if (j_count > MAX_JELLIES) j_count = MAX_JELLIES;

  int k_count = state.active_koi;
  if (k_count < 0) k_count = 0;
  if (k_count > MAX_KOI) k_count = MAX_KOI;

  step_and_draw_jellies(j_count);
  step_and_draw_koi(k_count, j_count);
  step_pellets_and_ripples();
  diffuse_water();

  ptr_vx *= 0.84f;
  ptr_vy *= 0.84f;
}

} // namespace

extern "C" {

WASM_EXPORT("sim_init")
void sim_init(int cols, int rows, float cw, float ch, uint32_t seed) {
  state.magic = STATE_MAGIC;
  state.rng_state = (seed != 0) ? seed : 0x5EED1234u;
  state.time = 0.0f;
  g_accum = 0.0f;
  g_cols = cols < 16 ? 16 : (cols > MAX_COLS ? MAX_COLS : cols);
  g_rows = rows < 16 ? 16 : (rows > MAX_ROWS ? MAX_ROWS : rows);
  g_cw = cw < 4.0f ? 4.0f : cw;
  g_ch = ch < 4.0f ? 4.0f : ch;
  g_width = static_cast<float>(g_cols) * g_cw;
  g_height = static_cast<float>(g_rows) * g_ch;

  clear_buffers();
  for (int i = 0; i < MAX_PELLETS; ++i) pellets[i].active = false;
  for (int i = 0; i < MAX_RIPPLES; ++i) ripples[i].active = false;
  spawn_ecosystem();
}

WASM_EXPORT("sim_set_theme")
void sim_set_theme(int is_light) {
  g_is_light = is_light ? 1 : 0;
}

WASM_EXPORT("sim_resize")
void sim_resize(int cols, int rows, float cw, float ch) {
  float prev_w = state.saved_width > 1.0f ? state.saved_width : g_width;
  float prev_h = state.saved_height > 1.0f ? state.saved_height : g_height;
  g_cols = cols < 16 ? 16 : (cols > MAX_COLS ? MAX_COLS : cols);
  g_rows = rows < 16 ? 16 : (rows > MAX_ROWS ? MAX_ROWS : rows);
  g_cw = cw < 4.0f ? 4.0f : cw;
  g_ch = ch < 4.0f ? 4.0f : ch;
  g_width = static_cast<float>(g_cols) * g_cw;
  g_height = static_cast<float>(g_rows) * g_ch;
  state.active_jellies = pick_jelly_count(g_width, g_height);
  state.active_koi = pick_koi_count(g_width, g_height);
  state.saved_width = g_width;
  state.saved_height = g_height;

  float prev_mobile = f_clamp(prev_w / 860.0f, 0.62f, 1.0f);
  float next_mobile = f_clamp(g_width / 860.0f, 0.62f, 1.0f);
  float r_scale = next_mobile / prev_mobile;
  float sx = (prev_w > 1.0f) ? (g_width / prev_w) : 1.0f;
  float sy = (prev_h > 1.0f) ? (g_height / prev_h) : 1.0f;
  bool remap = (f_abs(sx - 1.0f) > 0.15f) || (f_abs(sy - 1.0f) > 0.15f);

  for (int i = 0; i < MAX_JELLIES; ++i) {
    Jelly& j = state.jellies[i];
    if (f_abs(r_scale - 1.0f) > 0.02f) {
      j.r = f_clamp(j.r * r_scale, 38.0f, 130.0f);
      for (int t = 0; t < TENTACLES_PER_JELLY; ++t) {
        j.tentacles[t].offset *= r_scale;
        j.tentacles[t].seg_len *= r_scale;
      }
    }
    float ox = j.x, oy = j.y;
    if (remap) { j.x *= sx; j.y *= sy; }
    j.x = f_clamp(j.x, j.r * 0.6f, f_max(j.r * 0.6f, g_width - j.r * 0.6f));
    j.y = f_clamp(j.y, j.r * 0.6f, f_max(j.r * 0.6f, g_height - j.r * 0.6f));
    float dx = j.x - ox, dy = j.y - oy;
    if (dx != 0.0f || dy != 0.0f) {
      for (int t = 0; t < TENTACLES_PER_JELLY; ++t) {
        int cnt = j.tentacles[t].count;
        if (cnt < 0) cnt = 0;
        if (cnt > MAX_NODES) cnt = MAX_NODES;
        for (int n = 0; n < cnt; ++n) {
          j.tentacles[t].nodes[n].x += dx;
          j.tentacles[t].nodes[n].y += dy;
          j.tentacles[t].nodes[n].ox += dx;
          j.tentacles[t].nodes[n].oy += dy;
        }
      }
    }
  }

  for (int i = 0; i < MAX_KOI; ++i) {
    Koi& k = state.koi[i];
    if (f_abs(r_scale - 1.0f) > 0.02f) {
      k.length = f_clamp(k.length * r_scale, 130.0f, 360.0f);
    }
    float ox = k.x, oy = k.y;
    if (remap) { k.x *= sx; k.y *= sy; }
    k.x = f_clamp(k.x, 45.0f, f_max(45.0f, g_width - 45.0f));
    k.y = f_clamp(k.y, 45.0f, f_max(45.0f, g_height - 45.0f));
    float dx = k.x - ox, dy = k.y - oy;
    if (dx != 0.0f || dy != 0.0f) {
      for (int s = 0; s < KOI_NODES; ++s) {
        k.nx[s] += dx;
        k.ny[s] += dy;
      }
    }
  }
  clear_buffers();
}

WASM_EXPORT("sim_set_text_rect")
void sim_set_text_rect(float x, float y, float w, float h, int active) {
  text_x0 = x;
  text_y0 = y;
  text_x1 = x + w;
  text_y1 = y + h;
  text_active = active;
}

WASM_EXPORT("sim_set_nav_rect")
void sim_set_nav_rect(float x, float y, float w, float h, int active) {
  nav_x0 = x;
  nav_y0 = y;
  nav_x1 = x + w;
  nav_y1 = y + h;
  nav_active = active;
}

WASM_EXPORT("sim_pointer")
void sim_pointer(float x, float y, float vx, float vy, int active) {
  ptr_x = x;
  ptr_y = y;
  ptr_vx = vx;
  ptr_vy = vy;
  ptr_active = active;
}

WASM_EXPORT("sim_pulse")
void sim_pulse(float x, float y) {
  // Drop a pond pellet + expanding water ripple (Dao-Ho interaction!)
  int slot = -1;
  for (int i = 0; i < MAX_PELLETS; ++i) {
    if (!pellets[i].active) { slot = i; break; }
  }
  if (slot < 0) {
    slot = pellet_cursor;
    pellet_cursor = (pellet_cursor + 1) % MAX_PELLETS;
  }
  pellets[slot] = {x, y, 0.0f, true};
  add_ripple(x, y, 165.0f, 0.78f);
  deposit_energy(x, y, 0.35f);
}

WASM_EXPORT("sim_step")
void sim_step(float dt_ms) {
  if (dt_ms < 0.0f) dt_ms = 0.0f;
  if (dt_ms > 66.0f) dt_ms = 66.0f;
  g_accum += dt_ms / 16.666667f;
  int steps = 0;
  while (g_accum >= FIXED_DT && steps < 3) {
    step_fixed();
    g_accum -= FIXED_DT;
    ++steps;
  }
}

WASM_EXPORT("sim_step_once")
void sim_step_once() { step_fixed(); }

WASM_EXPORT("sim_state_ptr") uint8_t* sim_state_ptr() {
  return reinterpret_cast<uint8_t*>(&state);
}
WASM_EXPORT("sim_state_bytes") int sim_state_bytes() {
  return static_cast<int>(sizeof(PersistentState));
}

WASM_EXPORT("sim_validate_restored")
int sim_validate_restored() {
  if (state.magic != STATE_MAGIC || !f_finite(state.time) ||
      !f_finite(state.saved_width) || !f_finite(state.saved_height) ||
      state.saved_width < 64.0f || state.saved_height < 64.0f ||
      state.saved_width > 20000.0f || state.saved_height > 20000.0f) {
    return 0;
  }
  // Everything must sit near the saved pond; huge (but finite) values would
  // otherwise blow up rasterization loops after sim_resize remaps them.
  const float lo_x = -state.saved_width, hi_x = 2.0f * state.saved_width;
  const float lo_y = -state.saved_height, hi_y = 2.0f * state.saved_height;
  auto in_x = [&](float v) { return f_finite(v) && v >= lo_x && v <= hi_x; };
  auto in_y = [&](float v) { return f_finite(v) && v >= lo_y && v <= hi_y; };
  auto in_abs = [](float v, float lim) { return f_finite(v) && f_abs(v) <= lim; };
  for (int i = 0; i < MAX_JELLIES; ++i) {
    Jelly& j = state.jellies[i];
    if (!in_x(j.x) || !in_y(j.y) || !in_abs(j.vx, 50.0f) || !in_abs(j.vy, 50.0f) ||
        !in_abs(j.ang, 100.0f) || !f_finite(j.r) || !in_abs(j.phase, 100.0f) ||
        !in_abs(j.speed_mul, 4.0f) || !f_finite(j.depth) || j.depth < 0.0f || j.depth > 1.5f ||
        j.r < 20.0f || j.r > 140.0f || j.species >= 4) {
      return 0;
    }
    for (int t = 0; t < TENTACLES_PER_JELLY; ++t) {
      Tentacle& tn = j.tentacles[t];
      if (tn.count < 2 || tn.count > MAX_NODES || !in_abs(tn.offset, 300.0f) ||
          !f_finite(tn.seg_len) || tn.seg_len < 2.0f || tn.seg_len > 45.0f || !f_finite(tn.phase)) {
        return 0;
      }
      for (int k = 0; k < tn.count; ++k) {
        if (!in_x(tn.nodes[k].x) || !in_y(tn.nodes[k].y) ||
            !in_x(tn.nodes[k].ox) || !in_y(tn.nodes[k].oy)) {
          return 0;
        }
      }
    }
  }
  for (int i = 0; i < MAX_KOI; ++i) {
    Koi& k = state.koi[i];
    if (!in_x(k.x) || !in_y(k.y) || !in_abs(k.ang, 100.0f) || !in_abs(k.turn, 10.0f) ||
        !in_abs(k.speed, 20.0f) || !f_finite(k.length) || !in_abs(k.phase, 100.0f) ||
        !in_abs(k.fin_phase, 100.0f) || !f_finite(k.depth) || k.depth < 0.0f || k.depth > 1.5f ||
        !in_abs(k.beat, 10.0f) || !in_abs(k.gulp, 10.0f) || !in_abs(k.turn_bias, 1.0f) ||
        k.length < 60.0f || k.length > 420.0f || k.num_red > 4 || k.num_black > 6) {
      return 0;
    }
    for (int s = 0; s < KOI_NODES; ++s) {
      if (!in_x(k.nx[s]) || !in_y(k.ny[s])) return 0;
    }
  }
  sim_resize(g_cols, g_rows, g_cw, g_ch);
  return 1;
}

WASM_EXPORT("sim_tile_rgb_ptr") const uint32_t* sim_tile_rgb_ptr() { return out_tile_rgb; }
WASM_EXPORT("sim_glyph_rgb_ptr") const uint32_t* sim_glyph_rgb_ptr() { return out_glyph_rgb; }
WASM_EXPORT("sim_tile_alpha_ptr") const uint8_t* sim_tile_alpha_ptr() { return out_tile_alpha; }
WASM_EXPORT("sim_glyph_alpha_ptr") const uint8_t* sim_glyph_alpha_ptr() { return out_glyph_alpha; }
WASM_EXPORT("sim_glyph_ptr") const uint8_t* sim_glyph_ptr() { return out_glyph; }
WASM_EXPORT("sim_shadow_ptr") const uint8_t* sim_shadow_ptr() { return out_shadow; }
WASM_EXPORT("sim_wake_ptr") const uint8_t* sim_wake_ptr() { return out_wake; }
WASM_EXPORT("sim_cols") int sim_cols() { return g_cols; }
WASM_EXPORT("sim_rows") int sim_rows() { return g_rows; }

} // extern "C"
