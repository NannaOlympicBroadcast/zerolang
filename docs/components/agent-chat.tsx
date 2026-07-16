"use client";

import { useState } from "react";

type ToolCall = { command: string; output?: string };

type Message =
  | { role: "user"; text: string }
  | { role: "assistant"; text: string }
  | { role: "skill"; name: string }
  | { role: "tools"; calls: ToolCall[] }
  | { role: "output"; text: string };

export type ChatSpec = { title?: string; messages: Message[] };

function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <button
      type="button"
      aria-label="Copy message"
      onClick={async () => {
        try {
          await navigator.clipboard.writeText(text);
          setCopied(true);
          setTimeout(() => setCopied(false), 1400);
        } catch {}
      }}
      className="shrink-0 cursor-pointer rounded-md p-1.5 text-muted transition hover:bg-surface-muted hover:text-fg"
    >
      {copied ? (
        <svg width="13" height="13" viewBox="0 0 16 16" fill="none">
          <path d="M13.5 4.5 6.5 11.5 3 8" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      ) : (
        <svg width="13" height="13" viewBox="0 0 16 16" fill="currentColor">
          <path d="M5 1.75A1.75 1.75 0 0 1 6.75 0h5.5A1.75 1.75 0 0 1 14 1.75v5.5A1.75 1.75 0 0 1 12.25 9H11V7.5h1.25a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5a.25.25 0 0 0-.25.25V3H5V1.75Z" />
          <path d="M2 4.75A1.75 1.75 0 0 1 3.75 3h5.5A1.75 1.75 0 0 1 11 4.75v5.5A1.75 1.75 0 0 1 9.25 12h-5.5A1.75 1.75 0 0 1 2 10.25v-5.5Zm1.75-.25a.25.25 0 0 0-.25.25v5.5c0 .138.112.25.25.25h5.5a.25.25 0 0 0 .25-.25v-5.5a.25.25 0 0 0-.25-.25h-5.5Z" />
        </svg>
      )}
    </button>
  );
}

function ChevronIcon({ open }: { open: boolean }) {
  return (
    <svg
      width="12"
      height="12"
      viewBox="0 0 16 16"
      fill="none"
      className={`shrink-0 text-muted transition-transform duration-200 ${open ? "rotate-90" : ""}`}
    >
      <path
        d="M6 4l4 4-4 4"
        stroke="currentColor"
        strokeWidth="1.75"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function ToolCalls({ calls }: { calls: ToolCall[] }) {
  const [open, setOpen] = useState<number[]>([]);
  const toggle = (i: number) =>
    setOpen((prev) => (prev.includes(i) ? prev.filter((n) => n !== i) : [...prev, i]));

  return (
    <div className="flex flex-col gap-2">
      {calls.map((call, i) => {
        const isOpen = open.includes(i);
        const hasOutput = typeof call.output === "string" && call.output.length > 0;
        return (
          <div
            key={call.command}
            className="overflow-hidden rounded-lg border border-border bg-surface"
          >
            <button
              type="button"
              onClick={() => hasOutput && toggle(i)}
              aria-expanded={isOpen}
              disabled={!hasOutput}
              className="flex w-full items-center gap-2 px-3 py-2 font-mono text-[0.78125rem] leading-relaxed text-muted transition-colors enabled:cursor-pointer enabled:hover:bg-surface-muted"
            >
              {hasOutput ? <ChevronIcon open={isOpen} /> : <span className="w-3 shrink-0" />}
              <span
                className={`min-w-0 flex-1 text-left text-fg/80 ${
                  isOpen ? "whitespace-pre-wrap break-all" : "truncate"
                }`}
              >
                {call.command}
              </span>
            </button>
            {isOpen && hasOutput && (
              <pre className="m-0 overflow-x-auto border-t border-border/50 bg-bg px-3 py-2.5 text-[0.78125rem] leading-relaxed text-fg/70">
                {call.output}
              </pre>
            )}
          </div>
        );
      })}
    </div>
  );
}

function ChatMessage({ message }: { message: Message }) {
  if (message.role === "user") {
    return (
      <div className="flex items-center justify-end gap-1">
        <CopyButton text={message.text} />
        <div className="max-w-[82%] rounded-2xl rounded-br-md bg-fg px-4 py-2.5 text-[0.875rem] leading-relaxed text-bg">
          {message.text}
        </div>
      </div>
    );
  }
  if (message.role === "assistant") {
    return (
      <div className="max-w-[92%] text-[0.875rem] leading-[1.65] text-fg">{message.text}</div>
    );
  }
  if (message.role === "skill") {
    return (
      <div className="flex items-center gap-2 text-[0.78125rem] text-muted">
        <svg width="13" height="13" viewBox="0 0 16 16" fill="none" className="shrink-0">
          <path
            d="M6 2 3 8h3l-1 6 5-8H6l2-4H6Z"
            fill="currentColor"
            className="text-fg/50"
          />
        </svg>
        <span>
          Loaded <span className="font-medium text-fg">{message.name}</span> skill
        </span>
      </div>
    );
  }
  if (message.role === "tools") {
    return <ToolCalls calls={message.calls} />;
  }
  return (
    <pre className="m-0 overflow-x-auto rounded-lg border border-border bg-code-bg px-4 py-3 font-mono text-[0.8125rem] leading-relaxed text-code-fg">
      {message.text}
    </pre>
  );
}

export function AgentChat({ spec }: { spec: ChatSpec }) {
  return (
    <div className="not-prose my-6 overflow-hidden rounded-2xl border border-border bg-bg">
      <div className="flex flex-col gap-5 p-5 sm:p-6">
        {spec.messages.map((message, i) => (
          <ChatMessage key={i} message={message} />
        ))}
      </div>
    </div>
  );
}
