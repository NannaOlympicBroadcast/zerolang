import { ArrowRightIcon, LogoIcon } from "@/components/icons";
import type { CSSProperties, ReactNode } from "react";
import { ButtonLink } from "@/components/button";
import { InstallCopy } from "@/components/install-copy";
import { HomeChatViewport } from "@/components/home-chat-viewport";
import { ChatToolRuns } from "@/components/home-chat-tools";
import { LoopDiagram } from "@/components/loop-diagram";
import { highlight } from "@/lib/highlight";
import { pageMetadata } from "@/lib/page-metadata";

export const metadata = pageMetadata("");

const CODE_EXAMPLE = `<span class="hl-keyword">pub</span> <span class="hl-keyword">fn</span> <span class="hl-variable">main</span>(world: <span class="hl-type">World</span>) -> <span class="hl-type">Void</span> <span class="hl-keyword">raises</span> {
    <span class="hl-keyword">check</span> world.out.<span class="hl-variable">write</span>(<span class="hl-string">"hello from zero\\n"</span>)
}`;

const GRAPH_EXAMPLE = `zero-graph v1
origin source-text
module "hello"
hash "graph:a7f7e6899a73f3b4"

node #decl_ad8d9028 Function name:"main" type:"Void" public:true fallible:true
node #param_4610ae76 Param name:"world" type:"World"
node #expr_c403020c MethodCall name:"write" type:"Void"
node #expr_653eeb6e Literal type:"String" value:"hello from zero\\n"
edge #expr_c403020c arg #expr_653eeb6e order:0`;

const PATCH_EXAMPLE = `zero patch \\
  --expect-graph-hash graph:a7f7e6899a73f3b4 \\
  --op 'set node="#expr_653eeb6e" \\
    field="value" \\
    expect="hello from zero\\n" \\
    value="hello graph\\n"'`;

const CONSTRAINTS = [
  "Token efficiency",
  "Low memory",
  "Fast startup",
  "Fast builds",
  "Low latency",
  "Zero dependencies",
];

const ARCHITECTURE_STEPS = [
  {
    label: "Human",
    title: "Asks for an outcome",
    body: "Build a CRM API, add auth, or fix a failing route",
  },
  {
    label: "Agent",
    title: "Uses Graph",
    body: "Symbols, calls, types, effects, node IDs, and graph hashes",
  },
  {
    label: "Compiler",
    title: "Checks patches",
    body: "Shape, type, stale-state, and repository metadata checks",
  },
  {
    label: "Human",
    title: "Reviews projection",
    body: "Readable .0 projections stay available for review and rare manual edits",
  },
];

/* ─── Primitives ──────────────────────────────────── */

function GridGlow() {
  return (
    <div aria-hidden className="pointer-events-none absolute inset-0 -z-10 overflow-hidden">
      <div
        className="absolute inset-0 opacity-[0.4] dark:opacity-[0.25]"
        style={{
          backgroundImage:
            "linear-gradient(to right, var(--color-border) 1px, transparent 1px), linear-gradient(to bottom, var(--color-border) 1px, transparent 1px)",
          backgroundSize: "64px 64px",
          maskImage:
            "radial-gradient(ellipse 80% 60% at 50% 0%, #000 30%, transparent 75%)",
          WebkitMaskImage:
            "radial-gradient(ellipse 80% 60% at 50% 0%, #000 30%, transparent 75%)",
        }}
      />
    </div>
  );
}

function SectionHeader({
  title,
  description,
}: {
  title: string;
  description: string;
}) {
  return (
    <div className="mb-12 max-w-[44rem]">
      <h2 className="m-0 max-w-[14ch] text-[clamp(2.25rem,6vw,4.25rem)] font-semibold leading-[0.98] tracking-[-0.045em]">
        {title}
      </h2>
      <p className="mt-6 max-w-[34rem] text-pretty text-[1.0625rem] leading-[1.65] text-muted">
        {description}
      </p>
    </div>
  );
}

function VisualGlow({ className = "", children }: { className?: string; children: ReactNode }) {
  return <div className={className}>{children}</div>;
}

function Panel({ className = "", children }: { className?: string; children: ReactNode }) {
  return (
    <div
      className={`relative overflow-hidden rounded-2xl border border-fg/30 ${className}`}
      style={{ boxShadow: "0 1px 0 0 color-mix(in srgb, var(--color-fg) 4%, transparent)" }}
    >
      {children}
    </div>
  );
}

function WindowBar({ title }: { title: string }) {
  return (
    <div className="relative flex items-center border-b border-fg/30 px-4 py-3">
      <div className="flex gap-1.5">
        <span className="h-2.5 w-2.5 rounded-full bg-border" />
        <span className="h-2.5 w-2.5 rounded-full bg-border" />
        <span className="h-2.5 w-2.5 rounded-full bg-border" />
      </div>
      <span className="absolute left-1/2 -translate-x-1/2 font-mono text-xs font-medium text-muted">
        {title}
      </span>
    </div>
  );
}

function CodeWindow({
  title,
  html,
  className = "",
}: {
  title: string;
  html: string;
  className?: string;
}) {
  return (
    <Panel className={`bg-bg ${className}`}>
      <WindowBar title={title} />
      <pre className="m-0 overflow-x-auto bg-bg p-5 text-[0.8125rem] leading-[1.7]">
        <code className="text-code-fg" dangerouslySetInnerHTML={{ __html: html }} />
      </pre>
    </Panel>
  );
}

function ArchitectureDiagram() {
  return (
    <div className="mt-12 w-full text-left">
      <div className="grid grid-cols-1 gap-px overflow-hidden rounded-2xl border border-border bg-border sm:grid-cols-2 lg:grid-cols-4">
        {ARCHITECTURE_STEPS.map((item, index) => (
          <div
            key={item.title}
            className="group relative flex flex-col bg-bg p-7 transition-colors duration-200 hover:bg-surface-muted"
          >
            <div className="mb-7 flex items-center gap-2.5">
              <span className="flex h-6 w-6 items-center justify-center rounded-md border border-border font-mono text-[0.6875rem] tabular-nums text-muted transition-colors group-hover:border-fg/40 group-hover:text-fg">
                {index + 1}
              </span>
              <span className="font-mono text-[0.6875rem] font-semibold uppercase tracking-[0.16em] text-muted">
                {item.label}
              </span>
            </div>
            <h2 className="m-0 text-[1.0625rem] font-semibold leading-snug tracking-[-0.02em] text-fg">
              {item.title}
            </h2>
            <p className="m-0 mt-2.5 text-[0.8125rem] leading-[1.65] text-muted">
              {item.body}
            </p>
            <span
              aria-hidden
              className="pointer-events-none absolute right-0 top-1/2 z-10 hidden -translate-y-1/2 translate-x-[calc(50%+1px)] lg:flex"
            >
              {index < ARCHITECTURE_STEPS.length - 1 ? (
                <span className="flex h-6 w-6 items-center justify-center rounded-full border border-border bg-bg text-muted">
                  <ArrowRightIcon width={12} height={12} />
                </span>
              ) : null}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}

/* ─── Chat mockup ─────────────────────────────────── */

function chatRevealStyle(delay: string): CSSProperties {
  return { "--chat-delay": delay } as CSSProperties;
}

function ChatMockup() {
  return (
    <HomeChatViewport>
      <Panel className="home-chat-shell bg-bg shadow-card">
        <WindowBar title="your agent" />

        <div className="flex flex-col gap-5 p-5 sm:p-6">
          <div className="home-chat-reveal flex justify-end" style={chatRevealStyle("80ms")}>
            <div className="max-w-[82%] rounded-2xl rounded-br-md bg-fg px-4 py-2.5 text-[0.875rem] leading-relaxed text-bg">
build hello world in zerolang
            </div>
          </div>

          <div
            className="home-chat-reveal max-w-[92%] text-[0.875rem] leading-[1.65] text-fg"
            style={chatRevealStyle("360ms")}
          >
            I&apos;ll set up the package, patch the graph, and run it.
          </div>

          <ChatToolRuns startDelayMs={640} />

          <div
            className="home-chat-reveal max-w-[92%] text-[0.875rem] leading-[1.65] text-fg"
            style={chatRevealStyle("1320ms")}
          >
            Done. <code className="home-chat-code">zero.graph</code> validated at{" "}
            <code className="home-chat-code">graph:a7f7e689</code> with symbol{" "}
            <code className="home-chat-code">main</code>. It prints:
          </div>

          <div
            className="home-chat-reveal home-chat-output rounded-lg border border-border bg-code-bg px-4 py-3 font-mono text-[0.8125rem] text-code-fg"
            style={chatRevealStyle("1520ms")}
          >
            hello from zero
          </div>
        </div>
      </Panel>
    </HomeChatViewport>
  );
}

/* ─── Section wrapper ─────────────────────────────── */

function Section({ children }: { children: ReactNode }) {
  return (
    <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(5rem,11vh,8rem)]">
      {children}
    </section>
  );
}

export default function HomePage() {
  return (
    <div className="relative min-h-screen overflow-hidden">
      <main>
        {/* Hero */}
        <section className="relative overflow-hidden px-6 pb-24 pt-[clamp(4rem,11vh,7rem)]">
          <GridGlow />
          <div className="relative z-10 mx-auto flex max-w-[54rem] flex-col items-center text-center">
            <div className="mb-7 inline-flex items-center rounded-full border border-border bg-surface/70 px-3.5 py-1 font-mono text-[0.6875rem] uppercase tracking-[0.16em] text-muted backdrop-blur">
              Experimental
            </div>
            <h1 className="m-0 text-[clamp(2.5rem,7vw,5rem)] font-semibold leading-[0.98] tracking-[-0.05em]">
              The Programming
              <br />
              Language for Agents
            </h1>
            <p className="mt-7 max-w-[40rem] text-pretty text-[clamp(1.0625rem,2vw,1.25rem)] leading-[1.6] text-muted">
              A programming language where the graph is the program. Humans ask
              for outcomes. Agents query the program graph, submit checked
              edits, and prove the result.
            </p>
            <InstallCopy />
            <ArchitectureDiagram />
            <p className="mt-5 max-w-[34rem] text-[0.8125rem] leading-relaxed text-muted">
              Expect breaking changes. Run it in a safe environment, not against
              production systems.
            </p>
          </div>
        </section>

        {/* 01 — Chat */}
        <Section>
          <SectionHeader
            title="Start with a request."
            description="The expected workflow is a normal conversation. The graph discipline lives in the agent skills and compiler commands, not in stiff human prompts."
          />
          <VisualGlow className="mx-auto max-w-[40rem]">
            <ChatMockup />
          </VisualGlow>
        </Section>

        {/* 02 — Loop */}
        <Section>
          <SectionHeader
            title="Tighter agent loop."
            description="A traditional loop writes text, then runs tools to learn what the edit meant. Zerolang puts the compiler in the loop, so an edit is a checked change to the graph."
          />
          <LoopDiagram />
        </Section>

        {/* 03 — Patch */}
        <Section>
          <SectionHeader
            title="Checked by default."
            description="Graph patches target semantic nodes and fields, guarded by graph hashes and expected values. Stale or invalid edits fail before they touch the store."
          />
          <VisualGlow className="grid grid-cols-1 gap-5 lg:grid-cols-[1.4fr_1fr]">
            <CodeWindow title="zero patch" html={highlight(PATCH_EXAMPLE, "sh")} />
            <Panel className="bg-bg">
              <dl className="m-0 divide-y divide-border/50 font-mono text-[0.8125rem]">
                {[
                  ["graph hash", "graph:b3c1d04f"],
                  ["node", "#expr_653eeb6e"],
                  ["field", "value"],
                  ["symbols", "main"],
                  ["projection", "src/main.0"],
                ].map(([k, v]) => (
                  <div key={k} className="flex items-center justify-between gap-4 px-5 py-3">
                    <dt className="text-muted">{k}</dt>
                    <dd className="m-0 truncate text-code-fg">{v}</dd>
                  </div>
                ))}
              </dl>
            </Panel>
          </VisualGlow>
        </Section>

        {/* 04 — Graph */}
        <Section>
          <SectionHeader
            title="The program database."
            description="Readable text stays useful for review. The compiler-owned graph is the program database agents edit and the compiler consumes."
          />
          <VisualGlow className="grid grid-cols-1 gap-5 lg:grid-cols-2">
            <CodeWindow title="zero query · graph" html={highlight(GRAPH_EXAMPLE, "zero-graph")} />
            <CodeWindow title="src/main.0 · projection" html={CODE_EXAMPLE} />
          </VisualGlow>
        </Section>

        {/* 04 — Constraints */}
        <Section>
          <SectionHeader
            title="Built for runtime constraints."
            description="The graph model should reduce guessing without relaxing the runtime goals. Zerolang still aims to stay small, fast, explicit, and dependency-free."
          />
          <VisualGlow>
            <div className="grid grid-cols-2 gap-px overflow-hidden rounded-2xl border border-border bg-border md:grid-cols-3">
              {CONSTRAINTS.map((c) => (
                <div
                  key={c}
                  className="flex items-center gap-3 bg-bg px-6 py-7 text-[0.9375rem] font-medium tracking-[-0.01em] transition-colors hover:bg-surface-muted"
                >
                  <span className="h-1.5 w-1.5 shrink-0 rounded-full bg-fg/30" />
                  {c}
                </div>
              ))}
            </div>
          </VisualGlow>
        </Section>

        {/* CTA */}
        <section className="relative z-10 border-t border-border">
          <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center px-6 py-[clamp(6rem,14vh,9rem)] text-center">
            <h2 className="m-0 text-[clamp(2rem,6vw,3.5rem)] font-semibold leading-[1.02] tracking-[-0.045em]">
              Explore with us.
            </h2>
            <p className="mx-auto mt-6 max-w-[38rem] text-pretty text-[1.0625rem] leading-[1.6] text-muted">
              Start with the getting started guide, then read the graph architecture
              and compile-path pages to see why the program database matters.
            </p>
            <div className="mt-9 flex flex-wrap items-center justify-center gap-3">
              <ButtonLink href="/getting-started" variant="primary" size="lg">
                Get started <ArrowRightIcon />
              </ButtonLink>
              <ButtonLink href="/concepts/graph-architecture" variant="default" size="lg">
                Graph architecture <ArrowRightIcon />
              </ButtonLink>
            </div>
          </div>
        </section>
      </main>

      <footer className="relative z-10 border-t border-border py-8">
        <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center gap-2 text-center md:flex-row md:justify-between md:text-left">
          <div className="flex items-center gap-2 text-sm font-semibold text-fg">
            <LogoIcon width="14" height="12" />
            <span>zerolang</span>
          </div>
          <p className="m-0 text-[0.8125rem] text-muted">
            Experimental graph-first language design.
          </p>
        </div>
      </footer>
    </div>
  );
}
