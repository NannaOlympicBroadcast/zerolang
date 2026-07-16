// Rewrites ```json-render fenced code blocks into custom elements carrying
// base64-encoded JSON, so the docs can embed structured React components
// without switching the pipeline to MDX. Runs before rehype-pretty-code so the
// block is never treated as a normal code fence.

type HastNode = {
  type?: string;
  tagName?: string;
  value?: string;
  properties?: Record<string, unknown>;
  children?: HastNode[];
};

function getNodeText(node?: HastNode): string {
  if (!node) return "";
  if (node.type === "text") return node.value ?? "";
  if (!Array.isArray(node.children)) return "";
  return node.children.map(getNodeText).join("");
}

function getCodeLanguage(node: HastNode): string {
  const className = node.properties?.className;
  const classes = Array.isArray(className)
    ? className
    : typeof className === "string"
      ? className.split(/\s+/)
      : [];
  const languageClass = classes.find(
    (value) => typeof value === "string" && value.startsWith("language-"),
  );
  return typeof languageClass === "string" ? languageClass.slice("language-".length) : "";
}

function visit(node: HastNode | undefined, callback: (node: HastNode) => void): void {
  if (!node) return;
  callback(node);
  if (Array.isArray(node.children)) {
    for (const child of node.children) visit(child, callback);
  }
}

export function rehypeJsonRender(): (tree: HastNode) => void {
  return (tree: HastNode) => {
    visit(tree, (node) => {
      if (node.type !== "element" || node.tagName !== "pre") return;
      const code = node.children?.find(
        (child) => child.type === "element" && child.tagName === "code",
      );
      if (!code || getCodeLanguage(code) !== "json-render") return;

      const raw = getNodeText(code).replace(/\n$/, "");
      let tagName = "agentchat";
      try {
        const parsed = JSON.parse(raw) as { type?: string };
        if (parsed.type === "flow") tagName = "flowchart";
      } catch {}
      node.tagName = tagName;
      node.properties = { value: Buffer.from(raw, "utf8").toString("base64") };
      node.children = [];
    });
  };
}
