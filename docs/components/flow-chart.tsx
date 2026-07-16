type FlowTone = "graph" | "text" | "compiler" | "human";

type FlowNodeSpec = {
  id: string;
  label: string;
  x: number;
  y: number;
  tone?: FlowTone;
};

type FlowEdgeSpec = {
  id?: string;
  source: string;
  target: string;
  label?: string;
};

export type FlowChartSpec = {
  type: "flow";
  title?: string;
  height?: number;
  nodes: FlowNodeSpec[];
  edges: FlowEdgeSpec[];
};

// x/y in the spec are the top-left of each node, in px.
const NODE_W = 224;
const NODE_H = 72;
const PAD = 80;

const TONE_CLASS: Record<FlowTone, string> = {
  // faint, "old" loop
  text: "border border-border bg-bg text-muted",
  // the enforcer: strong inverted
  compiler: "bg-fg text-bg",
  // the good path: clean and present
  graph: "border border-fg/40 bg-surface text-fg",
  // human review
  human: "border border-border bg-surface-muted text-fg",
};

export function FlowChart({ spec }: { spec: FlowChartSpec }) {
  const byId = new Map(spec.nodes.map((n) => [n.id, n]));
  const maxX = Math.max(0, ...spec.nodes.map((n) => n.x));
  const maxY = Math.max(0, ...spec.nodes.map((n) => n.y));
  const width = PAD * 2 + maxX + NODE_W;
  const height = Math.max(spec.height ?? 0, PAD * 2 + maxY + NODE_H);

  const cx = (n: FlowNodeSpec) => PAD + n.x + NODE_W / 2;
  const topY = (n: FlowNodeSpec) => PAD + n.y;
  const bottomY = (n: FlowNodeSpec) => PAD + n.y + NODE_H;

  const edges = spec.edges
    .map((edge, i) => {
      const s = byId.get(edge.source);
      const t = byId.get(edge.target);
      if (!s || !t) return null;
      const loopBack = t.y <= s.y;
      let path: string;
      let labelX: number;
      let labelY: number;
      if (loopBack) {
        const leftEdge = Math.min(PAD + s.x, PAD + t.x);
        const sy = PAD + s.y + NODE_H / 2;
        const ty = PAD + t.y + NODE_H / 2;
        const bulge = leftEdge - 40;
        path = `M ${leftEdge} ${sy} C ${bulge} ${sy}, ${bulge} ${ty}, ${leftEdge} ${ty}`;
        labelX = bulge;
        labelY = (sy + ty) / 2;
      } else {
        const sx = cx(s);
        const sy = bottomY(s);
        const tx = cx(t);
        const ty = topY(t);
        const mid = (sy + ty) / 2;
        path = `M ${sx} ${sy} C ${sx} ${mid}, ${tx} ${mid}, ${tx} ${ty}`;
        labelX = (sx + tx) / 2;
        labelY = mid;
      }
      return {
        key: edge.id ?? `${edge.source}-${edge.target}-${i}`,
        path,
        label: edge.label,
        labelX,
        labelY,
      };
    })
    .filter((e): e is NonNullable<typeof e> => e !== null);

  // Only show a connection dot where an edge actually attaches. Normal edges
  // attach to the source's bottom and the target's top; loop-backs attach to
  // the side, so they do not light up a top/bottom dot.
  const topConn = new Set<string>();
  const bottomConn = new Set<string>();
  for (const edge of spec.edges) {
    const s = byId.get(edge.source);
    const t = byId.get(edge.target);
    if (!s || !t || t.y <= s.y) continue;
    bottomConn.add(s.id);
    topConn.add(t.id);
  }

  const stroke = "color-mix(in srgb, var(--color-fg) 28%, transparent)";
  const dot = "color-mix(in srgb, var(--color-fg) 40%, transparent)";

  return (
    <div className="not-prose my-6 overflow-hidden rounded-2xl border border-border bg-bg">
      {spec.title ? (
        <div className="border-b border-border px-5 py-3.5 font-mono text-xs font-medium text-muted">
          {spec.title}
        </div>
      ) : null}
      <div
        className="overflow-hidden p-2"
        style={{
          backgroundImage: "radial-gradient(var(--color-border) 1px, transparent 1px)",
          backgroundSize: "22px 22px",
          backgroundPosition: "-1px -1px",
        }}
      >
        {/* Scales down to fit the column; never upscales past its natural size. */}
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="mx-auto block h-auto w-full"
          style={{ maxWidth: width }}
          role="img"
          aria-label={spec.title ?? "diagram"}
        >
          {edges.map((e) => (
            <path key={e.key} d={e.path} fill="none" stroke={stroke} strokeWidth={1.5} />
          ))}
          {spec.nodes.map((n) => (
            <g key={`dots-${n.id}`} fill={dot}>
              {topConn.has(n.id) && <circle cx={cx(n)} cy={topY(n)} r={3} />}
              {bottomConn.has(n.id) && <circle cx={cx(n)} cy={bottomY(n)} r={3} />}
            </g>
          ))}

          {edges
            .filter((e) => e.label)
            .map((e) => (
              <foreignObject
                key={`label-${e.key}`}
                x={e.labelX - 40}
                y={e.labelY - 12}
                width={80}
                height={24}
              >
                <div className="flex h-full w-full items-center justify-center">
                  <span className="rounded border border-border bg-bg px-1.5 py-0.5 font-mono text-[0.6875rem] text-muted">
                    {e.label}
                  </span>
                </div>
              </foreignObject>
            ))}

          {spec.nodes.map((n) => (
            <foreignObject key={n.id} x={PAD + n.x} y={PAD + n.y} width={NODE_W} height={NODE_H}>
              <div
                className={`flex h-full w-full items-center justify-center rounded-xl px-4 text-center text-[0.875rem] font-medium leading-snug ${
                  TONE_CLASS[n.tone ?? "text"]
                }`}
              >
                {n.label}
              </div>
            </foreignObject>
          ))}
        </svg>
      </div>
    </div>
  );
}
