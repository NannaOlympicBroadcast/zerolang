/* Theme-aware, responsive version of the "agent coding loop" comparison.
   Scales to fit its container (never upscales past natural size). */

type Tone = "step" | "compiler" | "optional";
type Node = { label: string; tone: Tone };
type Loop = { from: number; to: number; side: "left" | "right" };
type Col = { x: number; kicker: string; title: string; loop: Loop; nodes: Node[] };

const W = 372;
const H = 68;
const NSTART = 88;
const STEP = 104;

const COLS: Col[] = [
  {
    x: 44,
    kicker: "Traditional",
    title: "Text is the source of truth",
    loop: { from: 5, to: 0, side: "left" },
    nodes: [
      { label: "agent writes source text", tone: "step" },
      { label: "format", tone: "step" },
      { label: "check", tone: "step" },
      { label: "build", tone: "step" },
      { label: "test", tone: "step" },
      { label: "inspect failures", tone: "step" },
    ],
  },
  {
    x: 520,
    kicker: "Zerolang",
    title: "Graph is the program",
    loop: { from: 1, to: 0, side: "right" },
    nodes: [
      { label: "agent writes graph patch", tone: "step" },
      { label: "compiler checks patch", tone: "compiler" },
      { label: "projection available for review", tone: "optional" },
    ],
  },
];

const TONE: Record<Tone, string> = {
  step: "h-full w-full border border-border bg-surface text-fg",
  compiler: "h-full w-full bg-fg font-semibold text-bg",
  optional: "h-full w-full border border-dashed border-border bg-transparent text-muted",
};

const VB_W = 936;
const MAX = Math.max(...COLS.map((c) => c.nodes.length));
const VB_H = NSTART + (MAX - 1) * STEP + H;

const STROKE = "color-mix(in srgb, var(--color-fg) 30%, transparent)";
const ARROW = "color-mix(in srgb, var(--color-fg) 55%, transparent)";
const nodeTop = (i: number) => NSTART + i * STEP;

export function LoopDiagram() {
  const verticals: { key: string; d: string }[] = [];
  const loops: { key: string; d: string; lx: number; ly: number }[] = [];

  for (const col of COLS) {
    const cx = col.x + W / 2;
    col.nodes.forEach((_, i) => {
      if (i < col.nodes.length - 1) {
        verticals.push({
          key: `${col.x}-${i}`,
          d: `M ${cx} ${nodeTop(i) + H + 2} L ${cx} ${nodeTop(i + 1) - 4}`,
        });
      }
    });
    const sy = nodeTop(col.loop.from) + H / 2;
    const ty = nodeTop(col.loop.to) + H / 2;
    const r = 12;
    let d: string;
    let lx: number;
    if (col.loop.side === "right") {
      const edge = col.x + W;
      const bulge = edge + 28;
      d = `M ${edge} ${sy} H ${bulge - r} Q ${bulge} ${sy} ${bulge} ${sy - r} V ${ty + r} Q ${bulge} ${ty} ${bulge - r} ${ty} H ${edge + 3}`;
      lx = bulge;
    } else {
      const edge = col.x;
      const bulge = edge - 36;
      d = `M ${edge} ${sy} H ${bulge + r} Q ${bulge} ${sy} ${bulge} ${sy - r} V ${ty + r} Q ${bulge} ${ty} ${bulge + r} ${ty} H ${edge - 3}`;
      lx = bulge;
    }
    loops.push({ key: `loop-${col.x}`, d, lx, ly: (sy + ty) / 2 });
  }

  return (
    <svg
      viewBox={`-40 0 ${VB_W + 80} ${VB_H}`}
      className="mx-auto block h-auto w-full"
      style={{ maxWidth: VB_W + 80 }}
      role="img"
      aria-label="Traditional source loop versus the Zerolang graph loop"
    >
      <defs>
        <marker id="loop-ah" markerUnits="userSpaceOnUse" markerWidth="10" markerHeight="10" refX="7" refY="5" orient="auto">
          <path d="M1.5 1.5 L8 5 L1.5 8.5 Z" fill={ARROW} />
        </marker>
      </defs>

      {/* column headers */}
      {COLS.map((col) => (
        <foreignObject key={`h-${col.x}`} x={col.x} y={0} width={W} height={NSTART - 24}>
          <div className="font-mono text-[12px] font-medium uppercase tracking-[0.2em] text-muted">
            {col.kicker}
          </div>
          <div className="mt-1.5 text-[20px] font-medium tracking-[-0.02em] text-fg">{col.title}</div>
        </foreignObject>
      ))}

      {verticals.map((v) => (
        <path key={v.key} d={v.d} fill="none" stroke={STROKE} strokeWidth={1.75} markerEnd="url(#loop-ah)" />
      ))}
      {loops.map((l) => (
        <path key={l.key} d={l.d} fill="none" stroke={STROKE} strokeWidth={1.75} markerEnd="url(#loop-ah)" />
      ))}

      {loops.map((l) => (
        <foreignObject key={`lbl-${l.key}`} x={l.lx - 40} y={l.ly - 16} width={80} height={32}>
          <div className="flex h-full w-full items-center justify-center">
            <span className="rounded border border-border bg-bg px-1.5 py-0.5 font-mono text-[11px] text-muted">
              repeat
            </span>
          </div>
        </foreignObject>
      ))}

      {COLS.map((col) =>
        col.nodes.map((n, i) => (
          <foreignObject key={`${col.x}-${i}`} x={col.x} y={nodeTop(i)} width={W} height={H}>
            <div
              className={`flex items-center justify-center rounded-2xl px-4 text-center text-[16px] font-medium leading-[1.2] tracking-[-0.01em] ${TONE[n.tone]}`}
            >
              {n.label}
            </div>
          </foreignObject>
        )),
      )}
    </svg>
  );
}
