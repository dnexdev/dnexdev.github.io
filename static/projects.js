(() => {
  const carousel = document.querySelector("[data-carousel]");
  const reduceMotionMq = window.matchMedia("(prefers-reduced-motion: reduce)");
  let reduceMotion = reduceMotionMq.matches;

  function setupHeroCopy() {
    if (!carousel) return;
    const panel = carousel.querySelector("[data-hero-copy]");
    const dismissBtn = carousel.querySelector("[data-dismiss-copy]");
    const restoreBtn = carousel.querySelector("[data-restore-copy]");
    if (!panel || !dismissBtn || !restoreBtn) return;

    const key = "dnex-hero-copy-dismissed";

    function setDismissed(dismissed) {
      panel.classList.toggle("is-dismissed", dismissed);
      carousel.classList.toggle("copy-dismissed", dismissed);
      restoreBtn.hidden = !dismissed;
      try {
        sessionStorage.setItem(key, dismissed ? "1" : "0");
      } catch (_) {
        /* ignore */
      }
    }

    dismissBtn.addEventListener("click", (event) => {
      event.stopPropagation();
      setDismissed(true);
    });

    restoreBtn.addEventListener("click", (event) => {
      event.stopPropagation();
      setDismissed(false);
    });

    let stored = "0";
    try {
      stored = sessionStorage.getItem(key) || "0";
    } catch (_) {
      /* ignore */
    }
    setDismissed(stored === "1");
  }

  function setupCarousel() {
    if (!carousel) return;

    const slides = Array.from(carousel.querySelectorAll("[data-slide]"));
    const metas = Array.from(carousel.querySelectorAll("[data-meta]"));
    const dots = Array.from(carousel.querySelectorAll("[data-dot]"));
    const prevBtn = carousel.querySelector("[data-prev]");
    const nextBtn = carousel.querySelector("[data-next]");
    const actions = carousel.querySelector("[data-actions]");
    const demoBtn = carousel.querySelector("[data-action-demo]");
    const codeBtn = carousel.querySelector("[data-action-code]");
    const viewport = carousel.querySelector(".carousel-viewport");
    if (!slides.length) return;

    let index = Math.max(
      0,
      slides.findIndex((slide) => slide.classList.contains("is-active"))
    );

    function syncActions() {
      if (!actions || !demoBtn || !codeBtn) return;
      const slide = slides[index];
      const demo = slide?.dataset.demo || "";
      const code = slide?.dataset.code || "";

      if (demo) {
        demoBtn.href = demo;
        demoBtn.hidden = false;
      } else {
        demoBtn.removeAttribute("href");
        demoBtn.hidden = true;
      }

      if (code) {
        codeBtn.href = code;
        codeBtn.hidden = false;
      } else {
        codeBtn.removeAttribute("href");
        codeBtn.hidden = true;
      }
    }

    function openActions() {
      if (!actions) return;
      syncActions();
      if (demoBtn.hidden && codeBtn.hidden) return;
      actions.hidden = false;
      carousel.classList.add("actions-open");
    }

    function closeActions() {
      if (!actions) return;
      actions.hidden = true;
      carousel.classList.remove("actions-open");
    }

    function setActive(nextIndex) {
      const total = slides.length;
      index = ((nextIndex % total) + total) % total;

      slides.forEach((slide, i) => {
        const active = i === index;
        slide.classList.toggle("is-active", active);
        slide.hidden = !active;

        const video = slide.querySelector("[data-carousel-video]");
        if (!video) return;
        if (active && !reduceMotion) {
          const play = video.play();
          if (play && typeof play.catch === "function") play.catch(() => {});
        } else {
          video.pause();
          if (!active) video.currentTime = 0;
        }
      });

      metas.forEach((meta, i) => {
        const active = i === index;
        meta.classList.toggle("is-active", active);
        meta.hidden = !active;
      });

      dots.forEach((dot, i) => {
        const active = i === index;
        dot.classList.toggle("is-active", active);
        dot.setAttribute("aria-selected", active ? "true" : "false");
      });

      syncActions();
    }

    prevBtn?.addEventListener("click", () => setActive(index - 1));
    nextBtn?.addEventListener("click", () => setActive(index + 1));

    dots.forEach((dot) => {
      dot.addEventListener("click", () => {
        const i = Number(dot.dataset.index);
        if (!Number.isNaN(i)) setActive(i);
      });
    });

    carousel.addEventListener("keydown", (event) => {
      if (event.key === "ArrowLeft") {
        event.preventDefault();
        setActive(index - 1);
      } else if (event.key === "ArrowRight") {
        event.preventDefault();
        setActive(index + 1);
      } else if (event.key === "Escape") {
        closeActions();
      }
    });

    viewport?.addEventListener("click", (event) => {
      if (event.target.closest("a, button")) return;
      if (actions && !actions.hidden) closeActions();
      else openActions();
    });

    document.addEventListener("click", (event) => {
      if (!carousel.classList.contains("actions-open")) return;
      if (carousel.contains(event.target) && event.target.closest("[data-actions], .carousel-viewport")) {
        return;
      }
      closeActions();
    });

    setActive(index);

    const onMotionPreferenceChange = (event) => {
      reduceMotion = event.matches;
      setActive(index);
    };
    if (typeof reduceMotionMq.addEventListener === "function") {
      reduceMotionMq.addEventListener("change", onMotionPreferenceChange);
    } else if (typeof reduceMotionMq.addListener === "function") {
      reduceMotionMq.addListener(onMotionPreferenceChange);
    }
  }

  setupHeroCopy();
  setupCarousel();
})();
