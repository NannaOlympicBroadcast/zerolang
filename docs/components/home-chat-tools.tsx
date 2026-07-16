"use client";

import type { CSSProperties } from "react";
import { useState } from "react";
import { highlight } from "@/lib/highlight";

const STEPS = [
  {
    command: "zero init",
    output: 'Created zero.toml\nCreated zero.graph  graph:e3b0c442\nInitialized package "hello"',
  },
  {
    command: 'zero patch --op addMain --op \'addCheckWrite fn="main" text="hello from zero\\n"\'',
    output: "Patched zero.graph\n  graph hash  graph:a7f7e689\n  symbols     main",
  },
  {
    command: "zero run",
    output: "hello from zero",
  },
];

function TerminalIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 24 24"
      fill="none"
      className="shrink-0 text-muted"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <polyline points="4 17 10 11 4 5" />
      <line x1="12" y1="19" x2="20" y2="19" />
    </svg>
  );
}

export function ChatToolRuns({ startDelayMs = 0 }: { startDelayMs?: number }) {
  const [open, setOpen] = useState<number[]>([]);

  const toggle = (i: number) =>
    setOpen((prev) => (prev.includes(i) ? prev.filter((n) => n !== i) : [...prev, i]));

  return (
    <div className="flex flex-col gap-2">
      {STEPS.map((step, i) => {
        const isOpen = open.includes(i);
        return (
          <div
            key={step.command}
            className="home-tool-row overflow-hidden rounded-lg border border-border bg-surface"
            style={{ "--chat-delay": `${startDelayMs + i * 180}ms` } as CSSProperties}
          >
            <button
              type="button"
              onClick={() => toggle(i)}
              aria-expanded={isOpen}
              className="flex w-full cursor-pointer items-center gap-2 px-3 py-2 font-mono text-[0.78125rem] leading-relaxed text-muted transition-colors hover:bg-surface-muted"
            >
              <TerminalIcon />
              <span
                className={`min-w-0 flex-1 text-left text-fg/80 ${
                  isOpen ? "whitespace-pre-wrap break-all" : "truncate"
                }`}
                dangerouslySetInnerHTML={{ __html: highlight(step.command, "sh") }}
              />
            </button>
            {isOpen && (
              <pre className="m-0 overflow-x-auto border-t border-border/50 bg-bg px-3 py-2.5 text-[0.78125rem] leading-relaxed text-fg/70">
                {step.output}
              </pre>
            )}
          </div>
        );
      })}
    </div>
  );
}
