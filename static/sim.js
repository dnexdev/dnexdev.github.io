(() => {
  const THEME_KEY = "dnex-theme";
  const STATE_KEY = "dnex-sim-state-v6";
  const DARK_BG = "#181715";
  const LIGHT_BG = "#f5f4f1";
  const reduceMotionMq = window.matchMedia("(prefers-reduced-motion: reduce)");
  let reduceMotion = reduceMotionMq.matches;

  // ---- 1. Dual Theme System (follows OS theme by default, manual toggle persisted) ----
  const systemLightMq = window.matchMedia("(prefers-color-scheme: light)");
  let isLight = document.documentElement.dataset.theme
    ? document.documentElement.dataset.theme === "light"
    : systemLightMq.matches;
  document.documentElement.dataset.theme = isLight ? "light" : "dark";

  function syncThemeUI() {
    const btn = document.getElementById("theme-toggle");
    if (btn) {
      btn.setAttribute("aria-pressed", isLight ? "true" : "false");
      btn.setAttribute("aria-label", isLight ? "Switch to dark mode" : "Switch to light mode");
    }
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", isLight ? LIGHT_BG : DARK_BG);
  }

  function applyTheme(light, persist = false) {
    isLight = light;
    document.documentElement.dataset.theme = light ? "light" : "dark";
    if (persist) {
      try {
        localStorage.setItem(THEME_KEY, light ? "light" : "dark");
      } catch (_) {}
    }
    syncThemeUI();
    if (wasm) {
      wasm.sim_set_theme(isLight ? 1 : 0);
      wasm.sim_step_once();
      if (renderNow) renderNow();
    }
  }

  document.addEventListener("click", (e) => {
    const btn = e.target.closest("#theme-toggle");
    if (!btn) return;
    applyTheme(!isLight, true);
  });

  // Follow live OS dark/light switches when the user hasn't set a manual override
  systemLightMq.addEventListener("change", (e) => {
    let saved = null;
    try { saved = localStorage.getItem(THEME_KEY); } catch (_) {}
    if (saved !== "light" && saved !== "dark") {
      applyTheme(e.matches, false);
    }
  });
  syncThemeUI();

  function normPath(p) {
    if (!p || p === "/") return "/";
    return p.endsWith("/") ? p : p + "/";
  }

  // ---- 2. Page-specific Enhancements (/projects/ rail + single-active video) ----
  let pageCleanup = null;
  let railHashJump = null;

  function setupPage() {
    if (pageCleanup) {
      pageCleanup();
      pageCleanup = null;
    }
    railHashJump = null;

    const cleanups = [];
    const videos = Array.from(document.querySelectorAll("[data-project-video]"));
    const rail = document.querySelector("[data-projects-rail]");
    const scrollBehavior = () => (reduceMotion ? "auto" : "smooth");

    // Single most-visible looping video playback
    if (videos.length) {
      const ratios = new Map();
      let activeVideo = null;

      const pickActive = () => {
        if (document.hidden || reduceMotion) {
          videos.forEach((v) => v.pause());
          activeVideo = null;
          return;
        }
        let best = null;
        let bestRatio = 0.25;
        for (const v of videos) {
          const r = ratios.get(v) || 0;
          if (r > bestRatio) {
            bestRatio = r;
            best = v;
          }
        }
        if (best !== activeVideo) {
          if (activeVideo) activeVideo.pause();
          activeVideo = best;
          if (activeVideo) {
            const candidate = activeVideo;
            const p = candidate.play();
            if (p && typeof p.catch === "function") {
              p.catch(() => {
                if (activeVideo === candidate) activeVideo = null;
              });
            }
          }
        }
      };

      if ("IntersectionObserver" in window) {
        const io = new IntersectionObserver(
          (entries) => {
            for (const entry of entries) ratios.set(entry.target, entry.intersectionRatio);
            pickActive();
          },
          { threshold: [0, 0.25, 0.5, 0.75, 1] }
        );
        videos.forEach((v) => io.observe(v));
        cleanups.push(() => io.disconnect());
      } else if (!reduceMotion) {
        const p = videos[0].play();
        if (p && typeof p.catch === "function") p.catch(() => {});
      }

      const onVis = () => pickActive();
      const onMotion = () => pickActive();
      document.addEventListener("visibilitychange", onVis);
      reduceMotionMq.addEventListener("change", onMotion);
      cleanups.push(() => {
        document.removeEventListener("visibilitychange", onVis);
        reduceMotionMq.removeEventListener("change", onMotion);
        videos.forEach((v) => v.pause());
      });
    }

    // Horizontal left-right gallery rail (Dao-Ho exact glideBy wheel + keyboard + drag)
    if (rail) {
      const cards = Array.from(rail.querySelectorAll(".project-card"));

      const stepSize = () => {
        if (cards.length >= 2) {
          const d = cards[1].offsetLeft - cards[0].offsetLeft;
          if (d > 0) return d;
        }
        const card = cards[0];
        if (!card) return 520;
        const gap = parseFloat(getComputedStyle(rail).columnGap || "14") || 14;
        return card.getBoundingClientRect().width + gap;
      };

      const scrollToHash = (hash, instant = false) => {
        if (!hash) return;
        const target = document.getElementById(hash.slice(1));
        if (target && rail.contains(target)) {
          target.scrollIntoView({
            inline: "start",
            block: "nearest",
            behavior: instant || reduceMotion ? "auto" : "smooth"
          });
        }
      };
      railHashJump = scrollToHash;

      // Dao-Ho smooth glideBy wheel handler (works anywhere on desktop /projects/)
      const GLIDE = 0.16;
      let wheelTarget = rail.scrollLeft;
      let wheelRaf = 0;
      let snapTimer = 0;

      const scheduleSnapRestore = (ms = 140) => {
        clearTimeout(snapTimer);
        snapTimer = setTimeout(() => {
          if (!wheelRaf) rail.classList.remove("is-wheeling");
        }, ms);
      };

      const runGlide = () => {
        const diff = wheelTarget - rail.scrollLeft;
        if (Math.abs(diff) < 0.7) {
          rail.scrollLeft = wheelTarget;
          wheelRaf = 0;
          scheduleSnapRestore(80);
          return;
        }
        rail.scrollLeft += diff * GLIDE;
        wheelRaf = requestAnimationFrame(runGlide);
      };

      const glideBy = (delta) => {
        const maxScroll = Math.max(0, rail.scrollWidth - rail.clientWidth);
        if (maxScroll <= 4) return;
        rail.classList.add("is-wheeling");
        if (reduceMotion) {
          rail.scrollLeft = Math.max(0, Math.min(maxScroll, rail.scrollLeft + delta));
          scheduleSnapRestore(120);
          return;
        }
        if (!wheelRaf) wheelTarget = rail.scrollLeft;
        wheelTarget = Math.max(0, Math.min(maxScroll, wheelTarget + delta));
        clearTimeout(snapTimer);
        if (!wheelRaf) wheelRaf = requestAnimationFrame(runGlide);
      };

      const onWheel = (e) => {
        if (window.innerWidth <= 768 || window.innerHeight < 520 || e.ctrlKey) return;
        const sideways = Math.abs(e.deltaX) > Math.abs(e.deltaY);
        if (sideways && rail.contains(e.target)) {
          if (wheelRaf) {
            cancelAnimationFrame(wheelRaf);
            wheelRaf = 0;
          }
          return;
        }
        e.preventDefault();
        let delta = sideways ? e.deltaX : e.deltaY;
        if (e.deltaMode === 1) delta *= 16;
        else if (e.deltaMode === 2) delta *= window.innerHeight;
        glideBy(delta);
      };

      window.addEventListener("wheel", onWheel, { passive: false });
      cleanups.push(() => {
        window.removeEventListener("wheel", onWheel);
        if (wheelRaf) cancelAnimationFrame(wheelRaf);
        clearTimeout(snapTimer);
      });

      // Mouse-only click-and-drag panning
      let dragging = false;
      let moved = false;
      let startX = 0;
      let startScroll = 0;

      const onDragStart = (e) => e.preventDefault();
      const onDown = (e) => {
        if (e.pointerType !== "mouse" || e.button !== 0) return;
        if (e.target.closest("button, .overlay-pill")) return;
        if (wheelRaf) {
          cancelAnimationFrame(wheelRaf);
          wheelRaf = 0;
        }
        dragging = true;
        moved = false;
        startX = e.clientX;
        startScroll = rail.scrollLeft;
        rail.classList.add("is-dragging");
      };
      const onMove = (e) => {
        if (!dragging) return;
        const dx = e.clientX - startX;
        if (Math.abs(dx) > 6) moved = true;
        rail.scrollLeft = startScroll - dx;
      };
      const onUp = () => {
        if (!dragging) return;
        dragging = false;
        rail.classList.remove("is-dragging");
        setTimeout(() => { moved = false; }, 0);
      };
      const onClickCapture = (e) => {
        if (moved) {
          e.preventDefault();
          e.stopPropagation();
          moved = false;
        }
      };

      rail.addEventListener("dragstart", onDragStart);
      rail.addEventListener("pointerdown", onDown);
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
      window.addEventListener("pointercancel", onUp);
      rail.addEventListener("click", onClickCapture, true);
      cleanups.push(() => {
        rail.removeEventListener("dragstart", onDragStart);
        rail.removeEventListener("pointerdown", onDown);
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
        window.removeEventListener("pointercancel", onUp);
        rail.removeEventListener("click", onClickCapture, true);
      });

      // Scoped keyboard navigation
      const onKeyDown = (e) => {
        if (window.innerWidth <= 768 || e.defaultPrevented || e.repeat) return;
        if (e.altKey || e.metaKey || e.ctrlKey || e.shiftKey) return;
        const active = document.activeElement;
        if (active && active !== document.body && active !== rail && !rail.contains(active)) return;
        if (e.key === "ArrowRight") {
          e.preventDefault();
          glideBy(stepSize());
        } else if (e.key === "ArrowLeft") {
          e.preventDefault();
          glideBy(-stepSize());
        }
      };
      window.addEventListener("keydown", onKeyDown);
      cleanups.push(() => window.removeEventListener("keydown", onKeyDown));

      if (window.location.hash) {
        requestAnimationFrame(() => scrollToHash(window.location.hash, true));
      }
    }

    const onScroll = () => syncTextRect();
    window.addEventListener("scroll", onScroll, { passive: true });
    cleanups.push(() => window.removeEventListener("scroll", onScroll));

    requestAnimationFrame(() => syncTextRect());
    pageCleanup = () => cleanups.forEach((fn) => fn());
  }

  // ---- 3. Race-Safe Zero-Respawn Soft Navigation across /, /projects/, /now/ ----
  let navSeq = 0;
  let activeAbort = null;
  let activeTransition = null;

  async function softNavigate(urlStr, push = true) {
    const seq = ++navSeq;
    if (activeAbort) activeAbort.abort();
    const controller = new AbortController();
    activeAbort = controller;

    try {
      const res = await fetch(urlStr, { credentials: "same-origin", signal: controller.signal });
      if (seq !== navSeq) return;
      if (!res.ok) {
        window.location.href = urlStr;
        return;
      }
      const html = await res.text();
      if (seq !== navSeq) return;

      const doc = new DOMParser().parseFromString(html, "text/html");
      const nextMain = doc.querySelector("main");
      const curMain = document.querySelector("main");
      if (!nextMain || !curMain) {
        window.location.href = urlStr;
        return;
      }

      if (push) {
        history.replaceState({ ...(history.state || {}), scrollY: window.scrollY }, "");
        history.pushState({ scrollY: 0 }, "", urlStr);
      }

      const applySwap = () => {
        document.title = doc.title;
        document.body.dataset.page = doc.body.dataset.page || "";
        curMain.replaceWith(nextMain);

        const nextPath = normPath(new URL(urlStr, window.location.origin).pathname);
        document.querySelectorAll(".site-nav a").forEach((a) => {
          const hrefPath = normPath(new URL(a.href, window.location.origin).pathname);
          if (hrefPath === nextPath) a.setAttribute("aria-current", "page");
          else a.removeAttribute("aria-current");
        });

        const restoreY = !push && history.state && typeof history.state.scrollY === "number"
          ? history.state.scrollY
          : 0;
        window.scrollTo(0, restoreY);
        setupPage();
      };

      if (activeTransition && typeof activeTransition.skipTransition === "function") {
        activeTransition.skipTransition();
      }

      if (document.startViewTransition && !reduceMotion) {
        activeTransition = document.startViewTransition(applySwap);
        try {
          await activeTransition.updateCallbackDone;
          requestAnimationFrame(() => syncTextRect());
          await activeTransition.finished;
        } catch (_) {}
        activeTransition = null;
      } else {
        applySwap();
      }
    } catch (err) {
      if (err && err.name === "AbortError") return;
      if (seq === navSeq) window.location.href = urlStr;
    }
  }

  document.addEventListener("click", (e) => {
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) {
      return;
    }
    const link = e.target.closest("a[href]");
    if (!link || link.target === "_blank" || link.hasAttribute("download")) return;
    const url = new URL(link.href, window.location.origin);
    if (url.origin !== window.location.origin) return;

    const targetPath = normPath(url.pathname);
    const curPath = normPath(window.location.pathname);

    if (targetPath === curPath) {
      e.preventDefault();
      if (url.hash) {
        history.pushState({ scrollY: window.scrollY }, "", targetPath + url.hash);
        if (railHashJump) railHashJump(url.hash, false);
        else document.getElementById(url.hash.slice(1))?.scrollIntoView();
      }
      return;
    }

    if (targetPath === "/" || targetPath === "/projects/" || targetPath === "/now/") {
      e.preventDefault();
      softNavigate(targetPath + url.hash, true);
    }
  });

  window.addEventListener("popstate", () => {
    const p = normPath(window.location.pathname);
    if (p === "/" || p === "/projects/" || p === "/now/") {
      softNavigate(p + window.location.hash, false);
    }
  });

  // ---- 4. C++20 WebAssembly 24-bit RGB Koi + Jellyfish Pond Host ----
  const canvas = document.getElementById("sim-canvas");
  let wasm = null;
  let renderNow = null;

  function syncTextRect() {
    if (!wasm) return;
    const box = document.querySelector("[data-sim-soften]");
    if (!box) {
      wasm.sim_set_text_rect(0, 0, 0, 0, 0);
      return;
    }
    const r = box.getBoundingClientRect();
    wasm.sim_set_text_rect(r.left, r.top, r.width, r.height, 1);
  }

  if (!canvas) {
    setupPage();
    return;
  }
  const ctx = canvas.getContext("2d");
  if (!ctx) {
    setupPage();
    return;
  }

  const MAX_COLS = 280;
  const MAX_ROWS = 180;
  let cw = 12;
  let ch = 14;
  let W = 0;
  let H = 0;
  let cols = 0;
  let rows = 0;

  // Cached 24-bit RGB -> CSS string map (same technique as Dao-Ho's renderer.ts!)
  const rgbCache = new Map();
  function rgbStyle(packed) {
    let s = rgbCache.get(packed);
    if (!s) {
      s = `rgb(${(packed >>> 16) & 255},${(packed >>> 8) & 255},${packed & 255})`;
      rgbCache.set(packed, s);
      if (rgbCache.size > 4096) rgbCache.clear();
    }
    return s;
  }

  let cachedBuffer = null;
  let tileRgbView = null;
  let glyphRgbView = null;
  let tileAlphaView = null;
  let glyphAlphaView = null;
  let glyphView = null;
  let shadowView = null;
  let wakeView = null;

  function refreshViews() {
    if (!wasm) return;
    const buf = wasm.memory.buffer;
    const total = cols * rows;
    if (buf !== cachedBuffer || !glyphView || glyphView.length !== total) {
      cachedBuffer = buf;
      tileRgbView = new Uint32Array(buf, wasm.sim_tile_rgb_ptr(), total);
      glyphRgbView = new Uint32Array(buf, wasm.sim_glyph_rgb_ptr(), total);
      tileAlphaView = new Uint8Array(buf, wasm.sim_tile_alpha_ptr(), total);
      glyphAlphaView = new Uint8Array(buf, wasm.sim_glyph_alpha_ptr(), total);
      glyphView = new Uint8Array(buf, wasm.sim_glyph_ptr(), total);
      shadowView = new Uint8Array(buf, wasm.sim_shadow_ptr(), total);
      wakeView = new Uint8Array(buf, wasm.sim_wake_ptr(), total);
    }
  }

  function computeGrid() {
    W = Math.max(320, Math.round(canvas.clientWidth || window.innerWidth));
    H = Math.max(320, Math.round(canvas.clientHeight || window.innerHeight));
    // Near-square editorial cells (Dao-Ho style: cw 10..13, ch = cw + 2)
    cw = Math.max(10, Math.min(13, Math.round(W / 108)));
    ch = cw + 2;
    while (Math.ceil(W / cw) > MAX_COLS || Math.ceil(H / ch) > MAX_ROWS) {
      cw++;
      ch = cw + 2;
    }
    cols = Math.min(MAX_COLS, Math.ceil(W / cw));
    rows = Math.min(MAX_ROWS, Math.ceil(H / ch));

    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.imageSmoothingEnabled = false;
  }

  function render() {
    if (!wasm) return;
    refreshViews();

    const total = cols * rows;
    const halfW = cw * 0.5;
    const halfH = ch * 0.5;

    ctx.clearRect(0, 0, W, H);
    ctx.font = `600 ${Math.round(ch * 0.72)}px "IBM Plex Mono", ui-monospace, SFMono-Regular, Menlo, monospace`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";

    // 1. Pond-floor shadows beneath koi & jellyfish
    ctx.fillStyle = isLight ? "rgb(60,58,56)" : "rgb(0,0,0)";
    ctx.globalAlpha = isLight ? 0.16 : 0.48;
    for (let i = 0; i < total; i++) {
      if (!shadowView[i] || tileAlphaView[i] > 20) continue;
      const x = (i % cols) * cw;
      const y = ((i / cols) | 0) * ch;
      ctx.fillRect(x + 1, y + 1, cw - 1, ch - 1);
    }

    // 2. Diffused water wake
    ctx.fillStyle = isLight ? "rgb(38,37,35)" : "rgb(255,247,240)";
    for (let i = 0; i < total; i++) {
      const w = wakeView[i];
      if (!w || tileAlphaView[i] > 20) continue;
      const x = (i % cols) * cw;
      const y = ((i / cols) | 0) * ch;
      ctx.globalAlpha = (w / 255) * (isLight ? 0.14 : 0.11);
      ctx.fillText(w > 95 ? "~" : ".", x + halfW, y + halfH + 1);
    }

    // 3. Opaque & translucent tiles + etched ASCII glyphs
    for (let i = 0; i < total; i++) {
      const ta = tileAlphaView[i];
      const ga = glyphAlphaView[i];
      if (ta === 0 && ga === 0) continue;

      const x = (i % cols) * cw;
      const y = ((i / cols) | 0) * ch;

      if (ta > 0) {
        ctx.globalAlpha = ta / 255;
        ctx.fillStyle = rgbStyle(tileRgbView[i]);
        ctx.fillRect(x + 1, y + 1, cw - 1, ch - 1);
      }

      const code = glyphView[i];
      if (code > 32 && ga > 0) {
        ctx.globalAlpha = ga / 255;
        ctx.fillStyle = rgbStyle(glyphRgbView[i]);
        ctx.fillText(String.fromCharCode(code), x + halfW, y + halfH + 1);
      }
    }
    ctx.globalAlpha = 1;
  }

  renderNow = render;

  function saveState() {
    if (!wasm) return;
    try {
      const bytes = new Uint8Array(wasm.memory.buffer, wasm.sim_state_ptr(), wasm.sim_state_bytes());
      let bin = "";
      for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      sessionStorage.setItem(STATE_KEY, btoa(bin));
    } catch (_) {}
  }

  function tryRestoreState() {
    if (!wasm) return false;
    try {
      const b64 = sessionStorage.getItem(STATE_KEY);
      if (!b64) return false;
      const bin = atob(b64);
      const expected = wasm.sim_state_bytes();
      if (bin.length !== expected) {
        sessionStorage.removeItem(STATE_KEY);
        return false;
      }
      const dst = new Uint8Array(wasm.memory.buffer, wasm.sim_state_ptr(), expected);
      for (let i = 0; i < expected; i++) dst[i] = bin.charCodeAt(i);
      if (wasm.sim_validate_restored() === 1) return true;
      sessionStorage.removeItem(STATE_KEY);
      return false;
    } catch (_) {
      try { sessionStorage.removeItem(STATE_KEY); } catch (__) {}
      return false;
    }
  }

  async function loadWasm() {
    if (WebAssembly.instantiateStreaming) {
      try {
        const res = await fetch("/static/sim.wasm");
        const out = await WebAssembly.instantiateStreaming(res, {});
        return out.instance;
      } catch (_) {}
    }
    const res = await fetch("/static/sim.wasm");
    const bytes = await res.arrayBuffer();
    const out = await WebAssembly.instantiate(bytes, {});
    return out.instance;
  }

  async function boot() {
    setupPage();
    computeGrid();

    let instance;
    try {
      instance = await loadWasm();
    } catch (_) {
      return;
    }

    wasm = instance.exports;
    const seed = (Date.now() & 0xffffffff) >>> 0 || 1337;
    wasm.sim_set_theme(isLight ? 1 : 0);
    wasm.sim_init(cols, rows, cw, ch, seed);
    syncTextRect();

    const restored = tryRestoreState();
    if (!restored) {
      wasm.sim_init(cols, rows, cw, ch, seed);
      syncTextRect();
      for (let i = 0; i < 45; i++) wasm.sim_step_once();
    } else {
      wasm.sim_step_once();
    }

    if (document.fonts && document.fonts.ready) {
      document.fonts.ready.then(() => {
        syncTextRect();
        render();
      });
    }
    render();

    window.addEventListener("resize", () => {
      computeGrid();
      wasm.sim_resize(cols, rows, cw, ch);
      syncTextRect();
      wasm.sim_step_once();
      render();
    });

    window.addEventListener("pagehide", saveState);

    const pointer = { x: -999, y: -999, vx: 0, vy: 0, active: false };
    const clearPointer = () => {
      pointer.active = false;
      wasm.sim_pointer(-999, -999, 0, 0, 0);
    };

    window.addEventListener(
      "pointermove",
      (e) => {
        if (reduceMotion) return;
        if (pointer.active) {
          pointer.vx = e.clientX - pointer.x;
          pointer.vy = e.clientY - pointer.y;
        }
        pointer.x = e.clientX;
        pointer.y = e.clientY;
        pointer.active = true;
        wasm.sim_pointer(pointer.x, pointer.y, pointer.vx, pointer.vy, 1);
      },
      { passive: true }
    );

    document.documentElement.addEventListener("pointerleave", clearPointer);
    document.documentElement.addEventListener("pointercancel", clearPointer);
    window.addEventListener("pointerup", (e) => {
      if (e.pointerType === "touch") clearPointer();
    });

    window.addEventListener("pointerdown", (e) => {
      if (reduceMotion) return;
      if (e.target && e.target.closest("a, button, video, [data-projects-rail]")) return;
      wasm.sim_pulse(e.clientX, e.clientY);
    });

    let rafId = 0;
    let last = performance.now();
    let lastPaint = 0;
    let hidden = document.hidden;

    function tick(now) {
      rafId = 0;
      if (reduceMotion) return;
      if (!hidden) {
        const dtMs = now - last;
        last = now;
        wasm.sim_step(dtMs);
        if (now - lastPaint >= 30) {
          lastPaint = now;
          render();
        }
      }
      rafId = requestAnimationFrame(tick);
    }

    function startLoop() {
      if (rafId || reduceMotion) return;
      last = performance.now();
      rafId = requestAnimationFrame(tick);
    }

    function stopLoop() {
      if (rafId) {
        cancelAnimationFrame(rafId);
        rafId = 0;
      }
    }

    document.addEventListener("visibilitychange", () => {
      hidden = document.hidden;
      if (!hidden) last = performance.now();
    });

    reduceMotionMq.addEventListener("change", (e) => {
      reduceMotion = e.matches;
      if (reduceMotion) {
        clearPointer();
        stopLoop();
      } else {
        startLoop();
      }
    });

    startLoop();
  }

  boot();
})();
