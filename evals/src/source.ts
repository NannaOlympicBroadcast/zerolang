export function extractZeroSource(text: string): string {
  const fenced = text.match(/```(?:zero|0)?\s*([\s\S]*?)```/i);
  const source = fenced ? fenced[1] : extractUnfencedZeroSource(text);
  return `${source.trim()}\n`;
}

export function sourcePatternFailures(
  source: string,
  patterns: RegExp[],
): string[] {
  return patterns
    .filter((pattern) => !pattern.test(source))
    .map((pattern) => pattern.toString());
}

const ZERO_SOURCE_START_PATTERN =
  /^\s*(?:(?:pub\s+)?(?:export\s+c\s+fn|extern\s+(?:c|type)|packed\s+type|fn|const|type|enum|choice|alias|interface)\s+|use\s+|test\s+)/;

export function finalSourceResponseFailures(
  responseText: string,
  source: string,
): string[] {
  if (!ZERO_SOURCE_START_PATTERN.test(source.trim())) {
    return ["final response did not include Zero source"];
  }
  return [];
}

function extractUnfencedZeroSource(text: string) {
  const lines = text.split(/\r?\n/);
  const start = lines.findIndex((line) => ZERO_SOURCE_START_PATTERN.test(line));
  if (start === -1) return text;
  return lines.slice(start).join("\n");
}
