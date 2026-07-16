"use client";

import type { ReactNode } from "react";
import { useEffect, useRef, useState } from "react";

export function HomeChatViewport({ children }: { children: ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    if (visible) {
      return;
    }

    const element = ref.current;
    if (!element) {
      return;
    }

    const isReady = () => {
      const rect = element.getBoundingClientRect();
      const viewportHeight = window.innerHeight || document.documentElement.clientHeight;
      const fitsInViewport = rect.height <= viewportHeight;

      if (fitsInViewport) {
        return rect.top >= 0 && rect.bottom <= viewportHeight;
      }

      return rect.top <= viewportHeight * 0.12 && rect.bottom >= viewportHeight * 0.88;
    };

    const update = () => {
      if (!isReady()) {
        return;
      }

      setVisible(true);
      window.removeEventListener("scroll", update);
      window.removeEventListener("resize", update);
    };

    update();
    window.addEventListener("scroll", update, { passive: true });
    window.addEventListener("resize", update);

    return () => {
      window.removeEventListener("scroll", update);
      window.removeEventListener("resize", update);
    };
  }, [visible]);

  return (
    <div ref={ref} className="home-chat-viewport" data-chat-visible={visible ? "true" : "false"}>
      {children}
    </div>
  );
}
