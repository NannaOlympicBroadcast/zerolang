import { readFile } from "node:fs/promises";

const headerPath = "native/zero-c/include/zero.h";
const contractsHeaderPath = "native/zero-c/include/zero_contracts.h";
const accessEffectPattern = /\bZ_(?:IN|OUT|INOUT|SINK)\b/;
const returnOwnershipPattern = /\bZ_RET_(?:OWNED|BORROWED)\b/;
const returnOptionalPattern = /\bZ_RET_OPTIONAL\b/;
const contractMacros = ["Z_IN", "Z_OUT", "Z_INOUT", "Z_SINK", "Z_OPTIONAL", "Z_RET_OWNED", "Z_RET_BORROWED", "Z_RET_OPTIONAL"];

type Violation = {
  kind: string;
  line?: number;
  declaration?: string;
  parameter?: string;
  macro?: string;
};

function stripLineComment(line: string): string {
  const index = line.indexOf("//");
  return index >= 0 ? line.slice(0, index) : line;
}

function pointerParameters(declaration: string): string[] {
  const open = declaration.indexOf("(");
  const close = declaration.lastIndexOf(")");
  if (open < 0 || close < open) return [];
  const params = declaration.slice(open + 1, close).trim();
  if (params === "" || params === "void") return [];
  return params
    .split(",")
    .map((param) => param.trim())
    .filter((param) => param !== "..." && param.includes("*"));
}

function pointerReturn(declaration: string): string | null {
  const beforeParams = declaration.slice(0, declaration.indexOf("(")).trim();
  if (!beforeParams.includes("*")) return null;
  return beforeParams.replace(/\s+z_[A-Za-z0-9_]+$/, "").trim();
}

function exportedDeclarations(header: string): { line: number; declaration: string }[] {
  return header
    .split("\n")
    .map((line, index) => ({ line: index + 1, declaration: stripLineComment(line).trim() }))
    .filter(({ declaration }) => /\bz_[A-Za-z0-9_]+\s*\([^;]*\);$/.test(declaration));
}

function contractViolations(header: string, contractsHeader: string): Violation[] {
  const violations: Violation[] = [];
  for (const macro of contractMacros) {
    if (!new RegExp(`#define\\s+${macro}\\b`).test(contractsHeader)) {
      violations.push({ kind: "missing-contract-macro", macro });
    }
  }
  for (const { line, declaration } of exportedDeclarations(header)) {
    const returned = pointerReturn(declaration);
    if (returned) {
      if (!returnOwnershipPattern.test(returned)) {
        violations.push({ kind: "missing-pointer-return-ownership", line, declaration, parameter: returned });
      }
      if (returnOptionalPattern.test(returned) && !returnOwnershipPattern.test(returned)) {
        violations.push({ kind: "optional-return-without-ownership", line, declaration, parameter: returned });
      }
    }
    for (const parameter of pointerParameters(declaration)) {
      if (!accessEffectPattern.test(parameter)) {
        violations.push({ kind: "missing-pointer-access-effect", line, declaration, parameter });
      }
      if (/\bZ_OPTIONAL\b/.test(parameter) && !accessEffectPattern.test(parameter)) {
        violations.push({ kind: "optional-without-access-effect", line, declaration, parameter });
      }
    }
  }
  return violations;
}

const header = await readFile(headerPath, "utf8");
const contractsHeader = await readFile(contractsHeaderPath, "utf8");
const declarations = exportedDeclarations(header);
const pointerParamCount = declarations.reduce((count, { declaration }) => count + pointerParameters(declaration).length, 0);
const pointerReturnCount = declarations.reduce((count, { declaration }) => count + (pointerReturn(declaration) ? 1 : 0), 0);
const violations = contractViolations(header, contractsHeader);

const result = {
  schema: 1,
  ok: violations.length === 0,
  header: headerPath,
  declarations: declarations.length,
  pointerParameters: pointerParamCount,
  pointerReturns: pointerReturnCount,
  contractMacros,
  violations,
};

if (violations.length > 0) {
  console.error(JSON.stringify(result, null, 2));
  process.exitCode = 1;
} else {
  console.log(JSON.stringify(result, null, 2));
}
